# Plan de refacere a sistemului de combat

Răspuns la `COMBAT_ILLNESS.md`. Nu o listă de patch-uri pe codul existent — o
arhitectură nouă în `src/game/combat/`, cu nucleu pur, testabilă fără server,
peste care calea veche se stinge bucată cu bucată. C++17. Data: 2026-08-15.

Precedentul e `src/shared/nav/` și `src/game/motion/`: modul propriu, document
propriu, fără moștenire. Acest fișier devine `src/game/combat/COMBAT.md` când
intră etapa 1.

---

## De ce nu se repară pe loc

Cele 33 de defecte din audit nu sunt 33 de bug-uri independente. Sunt patru
decizii de structură, fiecare producând un pumn de simptome:

| Decizie | Simptome pe care le produce |
|---|---|
| Nu există stare de combat — totul se recalculează din obiecte vii la fiecare roll | §1, §21, jumătate din tabelul de alocări |
| Ordinea efectelor e ordinea instrucțiunilor din `AttackerStateUpdate` | §2, §3, §11, §22, dublul `AttackedBy` din §23 |
| Identitatea e pointer brut | tot tabelul UAF, §7, §8 |
| Reintrarea e nelimitată — un proc rulează în mijlocul swing-ului care l-a produs | §2, §3, §8, §11 |

Repari `BLOCK_CRIT` și rămâi cu celelalte 32. Repari ordinea proc/damage și
strici extra-attacks, pentru că ambele trăiesc în același corp de funcție.
Formulele Vanilla (§15, §16, §17) sunt separat, și sunt singura parte care se
poate corecta punctual — dar n-are unde fi verificată, fiindcă nu există niciun
test de combat.

Deci: nucleu nou, pur, testat; apoi mutarea apelanților.

---

## Principii

1. **Un swing e o valoare, nu o secvență de efecte secundare.** Se rezolvă
   complet înainte ca lumea să se miște.
2. **Un singur loc mutează lumea.** `StrikeCommit`. Nimic altceva din modul nu
   scrie HP, threat, timere sau pachete.
3. **Nucleul pur nu vede `Unit`.** Regulă de link, nu de bună purtare: fișierele
   din `combat/pure/` nu includ nimic din `src/game/Object/`. Se leagă în binarul
   de teste fără biblioteca de joc. Asta e ce face testele posibile.
4. **Identitatea e `ObjectGuid`.** Pointerul se rezolvă la folosire și poate
   lipsi. Clasa de UAF dispare prin construcție, nu prin atenție.
5. **Reintrarea e o coadă, nu un stack.** Procurile, scuturile, extra-attacks
   nu rulează în mijlocul unui swing. Se pun în coadă și se drenează după commit,
   cu adâncime și buget mărginite.
6. **Constantele au o singură casă și o sursă.** `CombatConstants.h`, fiecare
   valoare cu comentariu de proveniență și un test care o fixează.
7. **Fără cast-uri C.** `static_cast`, `dynamic_cast` unde chiar trebuie. Codul
   vechi e plin de `((Player*)this)`; în modul nou nu intră niciunul.
8. **Fără god-file.** Nimic peste ~400 de linii. `Unit.cpp` are 5613 și de acolo
   vine jumătate din problemă.

---

## Straturile

```
                     ┌──────────────────────────────┐
   pur, testabil     │ CombatConstants              │  numerele TBC, sursate
   fără Unit,        │ Profile      (self)          │  ce depinde doar de mine
   fără World,       │ Matchup      (pereche)       │  ce depinde de amândoi
   fără DBC          │ HitTable     (cumulativ)     │  o singură rezolvare
                     │ StrikeResolver               │  (Matchup, roll) -> Strike
                     └──────────────┬───────────────┘
                                    │  Strike (POD, imutabil)
                     ┌──────────────▼───────────────┐
   atinge lumea      │ StrikeCommit                 │  SINGURUL loc care mută
   într-o ordine     │ ReactionQueue                │  reacții amânate, mărginite
   numită            │ Engagement / CombatRegistry  │  starea de combat
                     │ ThreatLedger                 │  lista de amenințare
                     │ AutoShot                     │  driver de ranged
                     └──────────────────────────────┘
```

Fluxul unui swing alb, întreg:

```
tick hartă
  └─ Engagement::Tick            timerele de mână
       └─ Engagement::HitTableFor(hand)     rebuild doar dacă versiunea s-a schimbat
            └─ StrikeResolver::Resolve      pur; produce Strike
                 └─ StrikeCommit::Apply     log, HP, threat, moarte
                      └─ ReactionQueue::Push  proc / scut / extra / daze
  └─ ReactionQueue::Drain        după commit, cu buget
```

---

## Nucleul pur

### `Profile` — ce depinde doar de o unitate

Tot ce azi se recitește din liste de aure la fiecare swing se calculează o dată
și trăiește aici. Cache pe unitate, invalidat prin bit de murdărie plus un
contor de versiune.

```cpp
/// Everything about one unit that a strike needs, resolved once.
struct Profile
{
    uint32   version = 0;            ///< bumped on every rebuild

    // offence
    DamageRange weapon[HAND_COUNT];  ///< min/max, already AP- and aura-adjusted
    uint32   speedMs[HAND_COUNT];    ///< hasted
    uint32   normalizedSpeedMs[HAND_COUNT];
    int32    weaponSkill[HAND_COUNT];
    int32    maxSkillForLevel;
    Hundredths critChance[HAND_COUNT];
    Hundredths hitChance[HAND_COUNT];
    Hundredths expertiseReduction[HAND_COUNT];

    // defence
    int32    defenseSkill;
    Hundredths dodgeChance;
    Hundredths parryChance;
    Hundredths blockChance;
    uint32   blockValue;
    uint32   armor;

    // capabilities, decided once instead of re-derived per roll
    CombatCaps caps;
};
```

`Hundredths` e un `int32` cu nume — sutimi de procent. Tot tabelul trăiește în
întregi; nu mai există `int32(chance * 100)` presărat prin cod.

`CombatCaps` înlocuiește șirul de `GetTypeId() == TYPEID_UNIT && GetCreatureType()
== CREATURE_TYPE_HUMANOID`:

```cpp
struct CombatCaps
{
    bool canDodge     : 1;   ///< not stunned, not a totem
    bool canParry     : 1;   ///< has a parry-capable weapon or the creature flag
    bool canBlock     : 1;   ///< holds a real shield, or the creature is flagged
    bool mayBeCrushed : 1;
    bool mayBeGlanced : 1;
    bool dualWielding : 1;
};
```

Asta rezolvă §6 fără nicio ramură nouă la roll: „toți NPC-ii blochează 5%" era
o consecință a faptului că întrebarea se punea în mijlocul tabelei.

**Invalidare.** Sistemul de aure/echipament/stat ștampilează un bit; profilul se
reconstruiește leneș, la prima citire de după:

```cpp
enum class ProfileDirty : uint32
{
    None      = 0,
    Equipment = 1u << 0,
    Auras     = 1u << 1,
    Stats     = 1u << 2,
    Skill     = 1u << 3,
    Level     = 1u << 4,
    Everything = 0xFFFFFFFFu
};
```

Punctele de ștampilare sunt puține și există deja: `_ApplyItemMods`,
`SpellAuraHolder::Apply`/`Remove`, `UpdateAllStats`, `GiveLevel`. Un test de
integrare le acoperă prin diferență: reconstruiește profilul cu forța și compară
cu cel din cache după fiecare mutație. Un bit uitat cade la primul test.

### `Matchup` — ce depinde de pereche

```cpp
/// Derived from two profiles. Cheap: no aura walks, no DBC.
struct Matchup
{
    Hundredths miss, dodge, parry, block, crit, glance, crush;
    uint32     blockValue;
    uint32     armor;
    float      glanceLow, glanceHigh;
    Hand       hand;
    bool       fromBehind;
    bool       victimSitting;
};
```

`Matchup::Build(attacker, victim, hand, geometry)` e o funcție pură peste două
`Profile` plus trei fapte de geometrie. Aici se aplică diferența de skill,
expertise-ul, supresia de crit. Nicio listă de aure nu e atinsă.

### `HitTable` — o rezolvare, nu șapte `if`-uri

```cpp
/// Cumulative one-roll table in hundredths. Resolve is a scan of at most
/// eight bounds; no branching on unit type, no aura access.
class HitTable
{
    public:
        static HitTable OneRoll(Matchup const& m);   ///< white swings
        static HitTable TwoRoll(Matchup const& m);   ///< specials / yellow

        MeleeOutcome Resolve(Hundredths roll) const;

        Hundredths Bound(MeleeOutcome o) const;      ///< for tests and logs

    private:
        std::array<Hundredths, OUTCOME_COUNT> m_cumulative{};
};
```

Cele două constructori codifică diferența pe care codul actual o simulează cu
un parametru `SpellCasted` care e mereu `false`. `MELEE_HIT_BLOCK_CRIT` nu
există: în `TwoRoll` blocul și critul sunt rolluri separate, exact cum e TBC, deci
starea „block critic" nu are cum să apară ca rezultat al unei tabele one-roll.
§4 dispare prin faptul că întrebarea nu se mai pune.

`Resolve` primește roll-ul din afară. Motorul nu cheamă `urand`. Testele injectează
roll-uri exacte și verifică granițele — inclusiv fiecare graniță ±1.

### `Strike` — rezultatul

```cpp
/// A fully resolved swing. Nothing here has touched the world yet.
struct Strike
{
    ObjectGuid   attacker;
    ObjectGuid   victim;
    Hand         hand;
    MeleeOutcome outcome = MeleeOutcome::Miss;
    SpellSchoolMask school = SPELL_SCHOOL_MASK_NORMAL;

    uint32 raw       = 0;   ///< rolled weapon damage, before anything
    uint32 afterRoll = 0;   ///< after crit / glance / crush multiplier
    uint32 afterArmor = 0;
    uint32 blocked   = 0;
    uint32 absorbed  = 0;
    uint32 resisted  = 0;
    uint32 applied   = 0;   ///< what actually leaves the health bar
    uint32 clean     = 0;   ///< rage / skill-up basis

    uint32 procAttacker = 0;
    uint32 procVictim   = 0;
    uint32 procEx       = 0;
    uint32 hitInfo      = 0;
    uint8  victimState  = 0;
};
```

Fiecare etapă de mitigare e păstrată separat. Nu ca lux de logging: `clean`,
rage-ul și skill-up-ul au nevoie de numere diferite din lanț, iar azi se
reconstruiesc prin scăderi împrăștiate (§10) și ies greșit.

### `StrikeResolver` — ordinea corectă de mitigare

```cpp
Strike StrikeResolver::Resolve(Matchup const& m, HitTable const& t,
                               DamageRange const& weapon, Rng& rng);
```

Ordinea, care e și o corectură față de §17:

1. roll de rezultat din `HitTable`
2. damage brut din interval — **în întregi**, nu `urand(uint32(0.9f), uint32(1.1f))`
3. multiplicatorul rezultatului: crit ×2 (+ moduri), glance ×f, crush ×1.5
4. **armura, și numai pentru școala fizică** — azi se aplică pe orice școală și
   *înainte* de roll, deci un melee de foc e armurat în loc de rezistat
5. blocul, sumă fixă, după armură
6. absorb / resist
7. `applied`, `clean`

Rezolverul nu are efecte secundare și nu are acces la nimic global. E funcția pe
care se scriu cele mai multe teste.

---

## Ordinea de commit

Singurul loc care scrie în lume. Fazele au nume și sunt observabile:

```cpp
/// The one place a strike changes the world. Phase order is the contract.
CommitResult StrikeCommit::Apply(Strike const& s, ReactionQueue& q);
```

| # | Fază | Ce face | Ce repară |
|---|---|---|---|
| 1 | `Resolve` | rerezolvă cele două guid-uri; iese dacă vreunul a dispărut | tabelul UAF |
| 2 | `Log` | `SMSG_ATTACKERSTATEUPDATE` cu numerele din `Strike` | log-ul care azi minte când procul a omorât ținta |
| 3 | `Health` | un singur `DealDamage` | §3 |
| 4 | `Threat` | `ThreatLedger::Add`, cu modificatorii de crit aplicați și pe white | §19 |
| 5 | `Death` | dacă ținta a murit: rupe engagement-ul, **golește reacțiile care o vizează** | §3 |
| 6 | `React` | pune în coadă: proc atacator, proc victimă, scut, item combat, daze, extra | §2, §8, §11, §22 |
| 7 | `Notify` | `AttackedBy` **o singură dată** | dublul apel din §23 |

Procul e la 6, damage-ul la 3. Asta e inversarea centrală: azi procul e înainte,
și de acolo vin „white damage pe cadavru", „log care minte" și UAF-ul la despawn.

Faza 5 e cea care face reintrarea sigură fără să numere nimic: dacă victima a
murit, reacțiile spre ea nu mai există când coada ajunge la ele.

---

## Coada de reacții

```cpp
/// A deferred consequence of a strike. Resolved by guid when it runs.
struct Reaction
{
    ObjectGuid source;
    ObjectGuid target;
    uint8      depth = 0;
    std::variant<ExtraSwing, ProcCast, DamageShield, ItemCombat, Daze> what;
};

class ReactionQueue
{
    public:
        bool Push(Reaction r);        ///< false when depth or budget is exhausted
        void Drain(Map& map);         ///< std::visit, guids re-resolved per item
        void DropTargeting(ObjectGuid guid);

    private:
        std::deque<Reaction> m_pending;
        uint32 m_spentThisTick = 0;
};
```

`std::variant` + `std::visit` — nu ierarhie virtuală. Reacțiile sunt cinci forme
fixe, cunoscute la compilare; un `variant` le ține prin valoare, fără alocare.

Ce se schimbă concret:

- **Extra-attacks devin o coadă, nu un contor.** `m_extraAttacks` moare. Sword
  Spec și Windfury se adună în loc să se înghită (§2), fiecare intrare își poartă
  mâna, deci un proc de OH dă un extra de OH (§15).
- **Windfury nu mai e două `CastCustomSpell` inline** înainte de `DealMeleeDamage`.
  E o intrare `ExtraSwing{count = 2}`, deci vizibilă pentru garda de adâncime și
  ordonată după damage (§11).
- **Scutul de damage nu mai iterează o listă vie în timp ce omoară atacatorul.**
  Se face un snapshot al aurelor la commit, se pune în coadă, se rulează pe guid-uri
  rerezolvate (§8).
- **`CastItemCombatSpell` nu mai iese pe `m_extraAttacks != 0`** — otrăvurile
  nu mai sunt sărite pe swing-urile care au proc-uit (§11).

Buget: adâncime maximă 2, cel mult N reacții pe tick per unitate. Când bugetul
se termină, se scapă intrări și se contorizează — nu se rulează la nesfârșit.

---

## `Engagement` — o singură sursă de adevăr pentru „sunt în luptă"

Azi sunt trei stări care nu coincid: `m_attacking`, `UNIT_FLAG_IN_COMBAT`, lista
de threat (§7, §23). Devine un obiect:

```cpp
/// The combat relationship between two units. Owned by the map, keyed by
/// attacker guid. Everything about "am I fighting" derives from this.
class Engagement
{
    public:
        void Tick(uint32 diffMs);

        ObjectGuid Attacker() const;
        ObjectGuid Victim() const;

        HitTable const& TableFor(Hand hand);   ///< rebuilt only on version change

    private:
        ObjectGuid m_attacker, m_victim;
        std::array<uint32, HAND_COUNT> m_swingTimerMs{};
        std::optional<Matchup>  m_matchup;
        std::array<HitTable, HAND_COUNT> m_table{};
        uint32 m_attackerVersion = 0;   ///< Profile::version seen when built
        uint32 m_victimVersion   = 0;
};
```

`TableFor` compară versiunile profilurilor; dacă nimic nu s-a schimbat de la
ultimul swing, returnează tabela deja construită. **Aici se plătește §1**: costul
per swing scade de la zeci de parcurgeri de liste de aure la o comparare de doi
`uint32` și un scan de opt praguri.

`CombatRegistry` per `Map` deține engagement-urile. Consecințe:

- `m_attacking` și `AttackerSet` dispar; nu mai există pointer brut de victimă.
- Există un loc unde se pot itera toate luptele de pe hartă — azi nu există.
- **Demolarea e determinată.** Registrul se golește într-un punct numit din
  `Map::UnloadAll`, înainte de griduri. Repo-ul are deja cicatricea asta
  (`Players leave the map before its grids and nav tiles do`); modulul nou nu
  o mai adaugă.
- Flag-ul `IN_COMBAT` devine derivat: e pornit exact cât timp unitatea are un
  engagement sau o intrare de threat. Flicker-ul din §23 nu mai are unde să apară.

---

## `ThreatLedger`

`std::list<HostileReference>` cu `getReferenceByTarget` O(n) pe fiecare tick de
damage, sortată integral, cu `iCurrentVictim` pointer brut și `--end()` pe listă
posibil goală (§13, §19).

```cpp
/// Flat, guid-keyed, sorted lazily. Lists are small (tens); a vector beats a
/// list at every operation that matters here.
class ThreatLedger
{
    public:
        void Add(ObjectGuid who, float amount, ThreatKind kind);
        void Remove(ObjectGuid who);
        void ApplyPercent(ObjectGuid who, int32 percent);

        void PushTaunt(ObjectGuid who, uint32 durationMs);
        void PopTaunt(ObjectGuid who);          ///< explicit, order-independent

        std::optional<ObjectGuid> Top() const;  ///< never a raw pointer

    private:
        struct Entry { ObjectGuid who; float threat; float melee; bool online; };
        std::vector<Entry> m_entries;
        std::unordered_map<ObjectGuid, std::size_t> m_index;
        std::vector<ObjectGuid> m_tauntStack;   ///< a stack, not a scalar
        bool m_dirty = true;
};
```

Ce se repară prin formă:

- **Taunt-ul devine o stivă cu push/pop explicit.** `setTempThreat` care
  suprascrie fără să scadă precedentul, plus garda `== 0` care refuză al doilea
  taunt al aceluiași jucător (§12), dispar: nu mai există „modificator temporar"
  de urmărit, există o stivă din care se scoate exact intrarea care a expirat.
- **`Top()` întoarce `std::optional`.** Nu mai există `--end()` pe listă goală,
  nici `iCurrentVictim` care supraviețuiește lui `clearReferences`.
- **`deleteReference` pe managerul greșit (§13)** nu se poate scrie: registrul
  are o singură direcție — cine mă amenință pe mine. Reciproca se cere de la
  registrul celuilalt, explicit.
- **Predicatul GM/taxi (§13)** se rescrie ca funcție cu nume și test:
  `bool IsSelectable(...)`, nu o expresie cu trei negații care e adevărată
  aproape mereu.
- **`threatAssist` cu `/ getSize()`** primește gardă; azi un heal în afara
  luptei împarte constant la zero.

Unreachable: azi o singură țintă neatinsă → `EnterEvadeMode` imediat, cu un
`TODO: make timer` în cod. Devine un timer de netratabilitate în engagement
(prag configurabil), ca un hop de navmesh sau un snare să nu mai producă evade.

---

## Ranged

Auto Shot construiește un `Spell` nou la fiecare glonț peste unul dummy ținut
permanent în slot (§9), și trece prin `MeleeSpellHitResult` care iese fără dodge
pentru `RANGED_ATTACK` (§5).

```cpp
/// Owns its own cadence. One shooter per unit, not one Spell per bullet.
class AutoShot
{
    public:
        void Start(ObjectGuid target, uint32 spellId);
        void Stop();
        void Tick(uint32 diffMs, Engagement& e, ReactionQueue& q);
};
```

Aceeași `HitTable`, construită dintr-un `Matchup` cu `canParry = false`,
`canBlock = false`, `canDodge = true` — care e regula TBC și pe care codul
actual o inversează în ambele direcții (dodge → miss, parry → hit plin).

Se rezolvă și §18: slotul `CURRENT_AUTOREPEAT_SPELL` nu mai e ocupat, deci
`IsNonMeleeSpellCasted` nu mai raportează „castez" cât timp tragi, deci melee-ul
nu mai e blocat de un autorepeat rămas agățat.

---

## Numerele

Toate într-un singur antet, fiecare cu proveniență, fiecare fixată de un test.
Cele care azi sunt Vanilla sau inventate:

| Mărime | Cod azi | 2.4.3 | Notă |
|---|---|---|---|
| Șansă de glance | `10 + (def − skill)`, cap **25%** | `10 + 2×(def − skill)`, cap **40%** | Vanilla rămas; vs boss 25 în loc de 40 |
| Curba de damage la glance | `1.3 / 1.2`, minus `0.7 / 0.3` pentru casteri | aceeași curbă pentru toate clasele | scăderea de caster e invenție MaNGOS |
| `maxLowEnd` | 0.91 warrior/rogue, 0.6 restul | fără split de clasă | idem |
| Armura | aplicată pe orice școală, înainte de roll | numai fizic, după multiplicator | §17 |
| Rezistența magică | `0.15 / level` pe **victimă** | nivelul atacatorului | §17 |
| Supresie de crit vs +3 | −0.6% | între 3% și 4.8% după sursă | **de decis cu sursă înainte de a fixa** |
| Arc de melee la swing | 120° (`2π/3`) | în general tratat ca 180° | **de confirmat pe client 2.4.3** |
| Parry haste | timer *hasted* vs prag *unhasted* | aceleași unități | forma 20/40/60 e corectă |
| Rage de atacator pe dodge/parry | 0 | factorul de viteză a armei | §10 |
| Bloc NPC | 5% pentru orice creatură | derivat din echipament/flag | §6 |
| Parry NPC | doar `CREATURE_TYPE_HUMANOID` | derivat din armă/flag | §6 |

Ultimele două rânduri marcate **de decis / de confirmat** nu se codifică pe
ghicite. Intră ca parametru cu valoarea actuală păstrată, plus un test care o
fixează, ca schimbarea ulterioară să fie o linie și un test roșu, nu o vânătoare.

---

## Etapizare

Fiecare etapă e un PR care ține CI verde pe GCC, Clang și MSVC, și fiecare are
sens singură. `include what you use` peste tot — cele două biblioteci standard
nu scapă aceleași anteturi.

| # | Ce intră | Comportament schimbat | Verificare |
|---|---|---|---|
| **0** | `src/tests/CombatTableTest.cpp` peste tabela **actuală**, extrasă minimal | niciunul | fixează comportamentul de azi, ca etapele următoare să aibă de ce să se lovească |
| **1** | `combat/pure/`: constante, `Profile`, `Matchup`, `HitTable`, `Strike`, `StrikeResolver` | niciunul — nimeni nu-l cheamă | teste pe granițe, pe fiecare rezultat, pe ordinea de mitigare |
| **2** | `StrikeCommit` + `ReactionQueue`, cablate **numai** pe swing-ul alb | ordinea proc/damage; extra-attacks în coadă; scutul de damage | §2, §3, §8, §11 |
| **3** | `Engagement` + `CombatRegistry`; `m_attacking` și `AttackerSet` ies | o singură stare de combat | §7, §23; tabelul UAF |
| **4** | `ThreatLedger` înlocuiește `ThreatManager` | taunt, unreachable, selecție | §12, §13, §19 |
| **5** | `AutoShot`; calea yellow trece pe `HitTable::TwoRoll` | ranged corect | §5, §9, §18 |
| **6** | corecțiile de numere din tabelul de mai sus | balans | fiecare cu testul lui |
| **7** | ștergerea căii vechi: `RollMeleeOutcomeAgainst`, `CalculateMeleeDamage`, `DealMeleeDamage`, `MELEE_HIT_BLOCK_CRIT` | niciunul, dacă 2–5 au fost complete | `Unit.cpp` scade cu ~800 de linii |

Etapa 0 nu e ceremonie. Fără ea nu există nimic împotriva căruia să compari, iar
audit-ul își încheie tabelul de priorități exact cu asta: *zero teste de combat —
orice fix e orb*.

Etapa 6 vine **după** ce motorul e mutat, nu înainte: altfel schimbi și structura,
și numerele, în același timp, și nu mai știi care dintre ele a stricat balansul.

`CMSG_ATTACKSWING` pe `PROCESS_INPLACE` (§20) se mută pe `PROCESS_THREADSAFE`
în etapa 3, când `Attack()` nu mai atinge pointeri bruți — mai devreme ar fi o
schimbare de fir peste o stare care încă nu e sigură.

---

## Teste

Nucleul pur se leagă în `mangos_tests` fără biblioteca de joc. Ce se poate scrie
din prima zi, fără server, fără DB, fără client:

- **Granițe de tabelă.** Pentru fiecare rezultat, roll-ul de pe graniță și cel
  de dinaintea ei. Suma cumulativă nu depășește 10000; niciun rezultat nu are
  interval negativ.
- **Ordinea de mitigare.** Un crit blocat parțial pe o țintă cu armură și absorb
  produce exact numerele așteptate în fiecare câmp din `Strike`.
- **Fizic vs non-fizic.** Un melee de foc nu e armurat.
- **Reacții.** Windfury pe un swing produce două intrări `ExtraSwing`, nu două
  cast-uri; Sword Spec plus Windfury produc trei, nu una.
- **Moarte în mijloc.** Un scut de damage care omoară atacatorul: coada e golită
  de intrările care îl vizează, nimic nu rulează pe un guid mort.
- **Adâncime.** Un lanț de extra care se auto-alimentează se oprește la prag.
- **Taunt.** A taunt, B taunt, A expiră, B expiră — pragul de threat al lui A
  revine la valoarea reală. Scenariul din §12, ca test.
- **Profil murdar.** După fiecare mutație de echipament/aură/stat, profilul din
  cache e identic cu unul reconstruit forțat.

Ce rămâne test de integrare, pe server: rage-ul, skill-up-ul, demolarea hărții.

---

## Ce nu face planul

- Nu atinge calea de spell. `SPELL_ILLNESS.md` e separat; `HitTable::TwoRoll`
  e punctul unde cele două se vor întâlni, mai târziu.
- Nu schimbă protocolul. Aceleași opcode-uri, aceleași câmpuri.
- Nu schimbă balansul în etapele 1–5. Numerele se mută în etapa 6, singure,
  ca să fie măsurabile.
- Nu introduce fire noi. Combatul rămâne pe tick-ul hărții — `mangosd` e singura
  autoritate, iar coada de reacții e drenată tot pe firul hărții.
- Nu compune poziții peste graniță de vas. `InMeleeReach` pe `ShareFrame` e deja
  corect și rămâne așa; `Matchup` primește geometria deja rezolvată în cadru.

---

## Etapa 1, așa cum a intrat

Ramura `feature/combat-core`. `src/game/combat/pure/` — bibliotecă statică
`combat_pure`, adăugată în `src/CMakeLists.txt` **în afara** gărzii
`BUILD_MANGOSD`, exact ca `geometry`, pentru că testele o leagă fără `game`.

Patru abateri de la ce scrie mai sus, toate deliberate:

- **`Strike` nu poartă guid-uri.** `ObjectGuid` e un tip de joc; a-l include ar
  fi spart chiar invariantul pentru care există modulul. Guid-urile stau lângă
  `Strike`, în stratul de commit, care oricum le rerezolvă.
- **Penalizarea de glance pentru casteri nu a fost reprodusă.** `Profile` nu
  poartă clasă, deci invenția n-are unde locui. Singurul loc din etapa 1 care
  nu e identic bit cu bit cu calea veche.
- **Roll-ul e `[0, 10000)`, nu `urand(0, 10000)`.** Semi-deschis, ca „benzile
  însumează exact tabela" să fie o afirmație testabilă. Biasul de 1/10001 pe
  care îl elimină nu se poate măsura în joc; motivul e testul, nu balansul.
- **Penalizarea de dual wield nu se aplică pe specials.** Calea veche încerca
  să ghicească „e yellow?" scanând sloturile de spell după școală fizică, deci
  un auto shot în zbor anula penalizarea pe un swing alb. Acum apelantul spune.

Corectura de ordine a mitigării e mai îngustă decât părea: armura e un
multiplicator, deci a o schimba cu alt multiplicator nu mișcă damage-ul decât
prin rotunjire. Ce se repară real e **școala** (armura numai pe fizic) și
**contabilitatea** — cu mitigarea aplicată înainte de a ști rezultatul, nu
exista punct în lanț în care „ce n-a încasat victima" să poată fi exprimat, și
de acolo venea rage-ul zero al atacatorului pe dodge și parry.

Douăzeci și trei de teste în `src/tests/CombatCoreTest.cpp`. Trei dintre ele se
numesc `...IsStillVanilla` / `...IsStillUnsourced` / `...IsStillFivePercentFlat`
și fixează numere despre care se știe că sunt greșite, ca etapa 6 să înroșească
un test pe nume, nu să surprindă pe cineva.

Invariantul de link e un test CMake, `combat_boundary`
(`src/tests/CheckCombatBoundary.cmake`): citește include-urile din
`combat/pure/` și pică dacă vreunul iese din director sau din biblioteca
standard. Linkerul nu ține cusătura singur — un `#include "Unit.h"` pentru un
singur enum compilează perfect în `game` și abia următorul om descoperă că
nucleul cere tot serverul.

---

## Prima mișcare

Etapa 0 și etapa 1 pot merge în paralel și nu ating niciun apelant: un fișier de
teste și un director nou care nu e chemat de nimeni. Zero risc de regresie, și
la capătul lor există pentru prima dată un loc unde se poate demonstra că o
formulă de combat e greșită.
