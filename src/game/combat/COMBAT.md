# Sistemul de combat — plan de tranziție

Răspuns la `COMBAT_ILLNESS.md` **și** `SPELL_ILLNESS.md`. Un singur plan,
pentru că cele două audituri nu descriu două sisteme: descriu aceleași patru
decizii, luate de două ori. C++17. Data: 2026-08-15.

Nu o rescriere mare. O tranziție graduală în care serverul funcționează după
fiecare pas, iar fiecare pas se poate opri fără să lase arborele într-o stare
intermediară.

---

## Un singur sistem

| Decizie de structură | Cum arată în melee | Cum arată în spelluri |
|---|---|---|
| Faptele nu se materializează niciodată — totul se re-derivă la fiecare eveniment | fără tabelă de hit; skill, aure, armură recalculate la fiecare swing (§1 melee) | fără `SpellInfo`; rândul DBC reinterpretat la fiecare întrebare (§1, §2 spell) |
| Comportamentul stă în `switch`, nu în date | tipul de creatură decis în mijlocul roll-ului: toți blochează 5%, doar umanoizii parry (§6 melee) | dummy / script / proc = `switch` pe spell ID în șase fișiere, eșec tăcut (§4 spell) |
| Efectele rulează în mijlocul operației care le-a produs | proc înainte de damage; scut de damage într-o buclă peste lista pe care o mută (§3, §8 melee) | triggered cast **în interiorul** lui `cast()`; putere și cooldown la lansare (§3, §13 spell) |
| Identitatea e pointer brut | `m_attacking`, `AttackerSet`, `damageInfo.target` (§7 melee) | `Item*` dangling, `SpellModifier` șters fără null (§7, §11 spell) |

Plus două care nu se împart, dar contează:

- **Cadrul de referință.** Melee-ul e corect: `InMeleeReach` cere `ShareFrame`.
  Spellurile nu: dest / src din client sunt trei float-uri fără cadru, amestecate
  cu `Where()`, care pe un vas e deck-local. Asta e exact compunerea
  hull ⊗ offset (§12 spell).
- **Casterul e obligatoriu `Unit*`.** Un GameObject nu poate caste, deci capcanele,
  flag-urile de BG și damage-ul de mediu sunt emulate prin „utilizatorul
  castează" (§5 spell). Aceeași boală ca `m_attacking`: identitate prea îngustă,
  fixată în tip.

**Consecința practică:** piesele pe care le-am construit pentru melee nu sunt
piese de melee. `Profile` e la unități ce e `SpellInfo` la spelluri — fapte
materializate o dată. `ReactionQueue` e locul unde *orice* consecință amânată
trăiește, fie ea un proc de armă sau un `spell_linked`. `HitTable` are trei
construcții, nu una. Planul de mai jos le tratează ca nucleu comun.

---

## Principii

1. **Un eveniment e o valoare.** Un swing, un hit de spell, un tick de DoT — se
   rezolvă complet înainte ca lumea să se miște.
2. **Un singur loc mutează lumea**, cu faze numite.
3. **Nucleul pur nu vede `Unit` și nu vede DBC-ul.** Regulă de link, ținută de
   `pure_boundary`: `combat/pure/` se leagă în binarul de teste fără
   biblioteca de joc.
4. **Identitatea e un guid**, rezolvat la folosire. Inclusiv casterul: un
   `Caster` e guid + fel, ca un GO să poată fi unul.
5. **Reintrarea e o coadă**, cu adâncime și buget.
6. **Poziția are cadru.** O destinație de spell poartă harta în care a fost
   citită, sau nu se compune cu nimic.
7. **Constantele au o casă și o sursă.**
8. **Fără cast-uri C. Fără god-file.**

---

## Cum se face tranziția, nu doar ce

Fiecare bucată trece prin patru trepte. Regula e că **nicio treaptă nu poate fi
sărită**, și că serverul rulează normal după fiecare.

```
  1. ADITIV      codul nou intră lângă cel vechi; nimeni nu-l cheamă
                 risc: zero. verificare: teste unitare pe nucleul pur

  2. UMBRĂ       codul nou rulează în paralel cu cel vechi, la fiecare eveniment
                 real; rezultatul VECHI e cel aplicat; diferențele se
                 raportează
                 risc: cost CPU. verificare: log-ul de divergență

  3. COMUTAT     codul nou aplică; cel vechi rămâne compilat și încă rulează
                 în umbră, cu rolurile inversate
                 risc: real, dar reversibil printr-o linie

  4. ȘTERS       calea veche iese din arbore
                 risc: zero. se face doar după ce umbra a tăcut
```

Treapta 2 e miezul. Un motor de combat cu **zero teste** nu se poate comuta pe
încredere: singura dovadă care contează e „am rulat ambele pe un realm viu, o
săptămână, și n-au fost diferențe". Umbra costă CPU pe durata ei și se scoate
imediat după.

Mecanismul, o singură clasă pentru tot:

```cpp
/// Runs the new answer beside the old one and reports where they part.
///
/// Not a debug aid: it is the only evidence that exists for a subsystem with
/// no tests and no reference implementation but itself. Rate-limited per site
/// -- a divergence that fires on every swing must not become the log.
template <typename T>
class Shadow
{
    public:
        static T Pick(char const* site, T const& legacy, T const& fresh);
};
```

Guvernat de o singură cheie de configurare, `CombatShadow`: `0` off, `1`
umbră (aplică vechiul), `2` comutat (aplică noul, raportează vechiul). Nu e
un switch de comportament permanent — e schela, și se demolează la treapta 4.
Fiecare etapă spune explicit unde se pune umbra și cum arată tăcerea.

---

## Straturile, comune

```
                      ┌────────────────────────────────┐
   pur, testabil      │ CombatConstants                │  numerele, sursate
   fără Unit,         │ Profile      (unitate)         │  fapte de unitate
   fără World,        │ SpellFacts   (spell)           │  fapte de spell
   fără DBC           │ Matchup      (pereche)         │
                      │ HitTable     (alb / special /  │  o singură rezolvare
                      │               magic)           │
                      │ Resolver     -> Strike | Hit   │
                      └───────────────┬────────────────┘
                                      │  valori imutabile
                      ┌───────────────▼────────────────┐
   atinge lumea       │ ProfileBuilder / SpellFactsLoad│  cusăturile
   într-o ordine      │ Commit        (faze numite)    │  SINGURUL loc care mută
   numită             │ ReactionQueue (a hărții)       │  consecințe amânate
                      │ Engagement / CombatRegistry    │
                      │ ThreatLedger                   │
                      └────────────────────────────────┘
```

`SpellFacts` e la spelluri exact ce e `Profile` la unități, inclusiv ca formă:
o structură pură, plus un încărcător care citește DBC-ul și overlay-urile SQL
o dată, la boot. Diferența e ciclul de viață — un `Profile` se reface când
unitatea se schimbă, un `SpellFacts` nu se schimbă niciodată după boot, deci
poate fi `const` și partajat.

---

## Ce e deja în arbore

Treapta 1, făcută, pe `feature/combat-core`:

| Piesă | Stare |
|---|---|
| `combat/pure/` — constante, `Profile`, `Matchup`, `HitTable`, `Strike`, `StrikeResolver` | aditiv, 30 de teste |
| `pure_boundary` — invariantul de link, ca test ctest | ține cusătura |
| `ProfileBuilder`, `CombatRng` | cusătura de citire |
| `ReactionQueue` — șase forme într-un `variant`, adâncime + buget | 8 teste |
| `StrikeCommit` — șapte faze | |
| `PerformSwing` + `AttackerStateUpdate` rescris | **comutat direct, fără umbră** |

Ultima linie e datoria pe care o am de plătit. Swing-ul alb a trecut de la
treapta 1 la treapta 3 într-un pas, fără umbră, pentru că atunci încă
credeam că e un subsistem izolat. **Prima etapă de mai jos repară asta**:
pune umbra sub el retroactiv, ca să existe dovada care lipsește.

---

## Etape

Fiecare e un PR care ține CI verde pe GCC, Clang și MSVC. Coloana „treaptă"
spune unde ajunge bucata la finalul etapei.

### A — Umbra, retroactiv pe melee

Treaptă: **2**, pentru ceea ce e deja comutat.

Cheia `CombatShadow` alege motorul, nu doar raportul:

- `1` — `PerformLegacySwing`: `CalculateMeleeDamage` / `DealMeleeDamage`,
  procuri înainte de damage, consecințe inline, nimic în coadă. Rollback real.
- `2` — `PerformSwing`, cu aceleași benzi raportate.

Cele două motoare nu se amestecă niciodată: jumătate din fiecare ar fi un al
treilea comportament pe care nu l-a testat nimeni.

Ce se compară sunt **benzile** — numerele care intră în roll — nu rezultatul.
Motivul e că `CalculateDamageAbsorbAndResist` consumă scuturi de absorb: o
umbră care rechema calea veche ar mânca un Power Word: Shield de două ori pe
swing. Benzile sunt aritmetică pură peste aceiași accesori.

Rămâne de plătit: umbra pe **rezultatul aplicat** (damage, `clean`, masca de
proc) cere un motor care poate rula fără să miște lumea. Nu există încă.

Tăcerea așteptată nu e totală: cinci divergențe sunt intenționate și se
declară dinainte, ca umbra să le ignore pe nume — armura pe școală nefizică,
rotunjirea intervalului de damage, penalizarea de dual wield pe specials,
absența penalizării de caster la glance, roll semi-deschis. Orice altceva e
un defect al meu.

### B — `ReactionQueue` trece la hartă

Treaptă: **3** direct; e o mutare de proprietate, nu o schimbare de reguli.

Azi coada e o variabilă locală în `AttackerStateUpdate`. Trebuie să fie a
hărții, pentru că un spell triggered de un proc de melee și un proc de spell
trebuie să împartă același buget și aceeași adâncime. Fără asta, etapa F nu
se poate face.

`CombatRegistry` per `Map` apare aici, cu coada înăuntru, drenată la sfârșitul
tick-ului hărții. Demolarea are un punct numit în `Map::UnloadAll`, înaintea
gridurilor.

### C — `SpellFacts`, aditiv

Treaptă: **1**.

Structura pură plus încărcătorul de la boot. Conține tot ce enumeră §1 și §2
din auditul de spelluri: durate / raze / raze de acțiune / cast time
**rezolvate**, nu indici; biții (pozitiv per efect, channeled, passive, AoE,
single-target, break-stealth, death-persist); `SpellSpecific`, grupul de
diminishing, măștile; efectele ca structuri; familia de stacking precalculată;
`spell_bonus_data` și `spell_proc_event` lipite de spell.

Verificarea e un audit la boot care, pentru **fiecare** spell din DBC, compară
faptul materializat cu răspunsul interogării vechi. `LoadAndVerify` e poarta:
la orice nepotrivire aruncă tot store-ul, `Get` răspunde „necunoscut" pentru
orice ID, și fiecare apelant cade înapoi pe interogarea DBC. Serverul pornește
în ambele cazuri; ce se pierde e o căutare economisită, nu corectitudinea.

Store-ul e indexat pe ID, cu `GetIdBound()` ca plafon — `GetNumRows()` devine
un *număr de intrări* de îndată ce ceva a chemat `SetEntry`, iar un vector
dimensionat cu el pierde tăcut orice ID de deasupra. Un rând al cărui `ID` nu
e egal cu indexul la care a fost găsit e numărat și lăsat necunoscut.

Aici intră și matricea de no-stack: `IsNoStackSpellDueToSpell` e o funcție pură
de `(a, b)` care azi costă două `LookupEntry` și șase sute de linii, **pe
fiecare pereche, pe fiecare apply**. Un boss cu 30 de aure plătește ~90 de
căutări ca să afle ce nu se stivuiește. Precalculat, e o citire.

### D — Interogările fierbinți trec pe `SpellFacts`

Treaptă: **2**, apoi **3** pe grupuri mici.

Nu toate deodată. Grupuri, în ordinea în care umbra le poate demonstra:

1. durată / cast time / rază / rază de acțiune — pur numeric, ușor de comparat
2. `IsPositiveSpell` / `IsPositiveEffect` — și aici se rupe minciuna cu
   `CANT_BE_REFLECTED` folosit ca „e debuff" (§8 spell); bitul dedicat
   `SPELL_ATTR_AURA_IS_DEBUFF` intră în fapt, nu în euristică
3. `GetSpellSpecific`
4. matricea de no-stack

Fiecare grup: umbră până tace, apoi comutat. Overload-urile pe `uint32` care
aruncă pointerul și-l recer dispar cu grupul lor.

### E — O singură rezolvare de hit

Treaptă: **1** (a treia construcție), apoi **2**, apoi **3**.

`HitTable::Magic(matchup)` alături de `OneRoll` și `TwoRoll`.
`MagicSpellHitResult` și `MeleeSpellHitResult` devin construcții ale aceleiași
tabele. Aici se repară și §5 melee: ranged **poate** fi dodged, nu poate fi
parried sau blocked, iar azi codul inversează ambele.

`Profile` capătă jumătatea de spell — hit de spell, crit de spell, rezistențe
pe școală, spell power — ca `SpellDamageBonusDone` să se poată plia la fel ca
`MeleeDamageBonusDone`.

### F — Spellurile triggered devin reacții

Treaptă: **2**, apoi **3**. Etapa cea mai riscantă din plan.

Azi un triggered cast se face **în interiorul** lui `cast()`: `new Spell` +
`new SpellEvent` + tot lanțul, recursiv. Devine o intrare `ProcCast` în coada
hărții. Ordinea de execuție se schimbă — de aceea etapa are nevoie de umbră
lungă, și de aceea vine după B.

Aici intră și Windfury, care azi e două `CastCustomSpell` inline și ocolește
garda de extra-on-extra: devine o intrare `ExtraSwing`.

### G — `Engagement` și cache-ul de profil

Treaptă: **2**, apoi **3**.

Abia acum se plătește §1 din ambele audituri. `Engagement` ține `Matchup` +
`HitTable` + versiunile celor două profile; tabela se reface la schimbare de
versiune, nu la swing. `Profile` capătă biți de murdărie, ștampilați în
punctele de mutație.

Umbra de aici e specială și e cea mai importantă din tot planul: **reconstruiește
profilul cu forța și compară cu cel din cache, la fiecare citire**. Un bit de
invalidare uitat nu e o încetinire, e un număr greșit; ăsta e singurul mod de
a-l găsi fără să-l descopere jucătorii.

`m_attacking` și `AttackerSet` ies aici. Starea de combat devine una singură.

### H — `ThreatLedger`

Treaptă: **2**, apoi **3**. Taunt ca stivă, `Top()` ca `std::optional`,
vector plat cu index pe guid, predicatul GM/taxi ca funcție cu nume și test.

### I — Ranged

Treaptă: **1**, apoi **3**. `AutoShot` cu cadența lui, un shooter în loc de un
`new Spell` per glonț. Eliberează și slotul `CURRENT_AUTOREPEAT_SPELL`, care
azi face `IsNonMeleeSpellCasted` să raporteze „castez" cât timp tragi.

### J — Cadru și caster

Treaptă: **2**, apoi **3**. Destinațiile de spell capătă cadru; `Caster`
devine guid + fel, ca un GameObject să poată caste. Cele două găuri
arhitecturale din auditul de spelluri, și singurele care cer o schimbare de
tip, nu de flux.

### K — Numerele

Treaptă: **3** direct, fiecare cu testul ei.

Glance 25 → 40 % și 1 %/punct → 2 %/punct. Supresia de crit, **după** ce
cineva o citează. Armura pe școală. Rezistența magică pe nivelul atacatorului.
Rage de atacator pe dodge / parry. Arcul de melee, **după** ce e confirmat pe
client 2.4.3. Blocul și parry-ul de NPC din echipament.

Vine după G și nu înainte: altfel se schimbă structura și balansul în același
timp, și nu se mai știe care l-a stricat.

### L — Alocările

Treaptă: **3**. `Spell` din pool, `vector` cu capacitate rezervată pentru
ținte în loc de `std::list`, holder / aură din pool. Măsurabil abia după G,
fiindcă până atunci profilul domină.

### M — Ștergerea

Treaptă: **4**. `RollMeleeOutcomeAgainst`, `CalculateMeleeDamage`,
`DealMeleeDamage`, `MELEE_HIT_BLOCK_CRIT`, interogările pe ID, `Shadow` însuși
și cheia `CombatShadow`. Schela se demolează.

---

## Ordinea, și de ce

```
A ── umbra pe ce e deja comutat          (plătește datoria)
│
B ── coada trece la hartă                (deblochează F)
│
├─ C ── SpellFacts, aditiv
│   └─ D ── interogările fierbinți
│        └─ E ── o singură tabelă de hit
│             └─ F ── triggered ca reacții
│
└─ G ── Engagement + cache               (plătește §1)
     ├─ H ── threat
     ├─ I ── ranged
     └─ J ── cadru și caster
          └─ K ── numerele
               └─ L ── alocările
                    └─ M ── ștergerea
```

C, D, E pot merge în paralel cu G, H, I: prima ramură e despre spelluri, a
doua despre stare. Se întâlnesc la F și la K.

**Orice etapă se poate opri.** Dacă tranziția moare la D, arborele rămâne cu
un `SpellFacts` corect, verificat la boot, citit pentru durate și raze, și cu
restul motorului nemișcat. Nimic nu e pe jumătate.

---

## Testele

Nucleul pur se leagă fără biblioteca de joc. Ce se poate scrie fără server:

- **Tabele.** Benzile acoperă `[0, 10000)` exact, granițele ±1, fiecare rezultat.
- **Ordinea de mitigare.** Un crit blocat parțial, cu armură și absorb.
- **Reacții.** Adâncime, buget, moarte la mijloc, ordinea FIFO.
- **`SpellFacts`.** Faptul materializat contra interogării vechi, pentru fiecare
  spell — la boot, nu în `mangos_tests`, fiindcă are nevoie de DBC.
- **No-stack.** Matricea contra celor șase sute de linii, pe fiecare pereche
  care apare în DBC.
- **Profil murdar.** După fiecare mutație de echipament / aură / stat, cache-ul
  identic cu o reconstrucție forțată.

Ce rămâne pe umbră, pe realm viu, fiindcă nu există alt martor: ordinea
efectelor, procurile, threat-ul, și tot ce depinde de starea lumii.

---

## Ce nu face planul

- Nu schimbă protocolul.
- Nu schimbă balansul înainte de K.
- Nu introduce fire noi. `mangosd` rămâne singura autoritate; coada se drenează
  pe firul hărții.
- Nu compune poziții peste graniță de vas. Etapa J dă cadru destinațiilor
  tocmai ca să nu se mai poată.
- Nu atinge SD3 sau Eluna. Hook-urile de script rămân unde sunt; etapa C le
  lipește de `SpellFacts` în loc să le caute prin `switch`, dar contractul lor
  nu se mișcă.

---

## Etapele deja intrate, în detaliu

### Etapa 0 și 1 — nucleul pur și cusătura

`src/game/combat/pure/` e bibliotecă statică proprie, adăugată în
`src/CMakeLists.txt` **în afara** gărzii `BUILD_MANGOSD`, ca `geometry`,
pentru că testele o leagă fără `game`.

Patru abateri deliberate de la calea veche:

- **`Strike` nu poartă guid-uri.** `ObjectGuid` e tip de joc; a-l include ar fi
  spart chiar invariantul pentru care există modulul.
- **Penalizarea de glance pentru casteri nu a fost reprodusă.** `Profile` nu
  poartă clasă, deci invenția n-are unde locui.
- **Roll semi-deschis `[0, 10000)`**, ca „benzile însumează exact tabela" să
  fie testabil.
- **Dual wield nu se aplică pe specials.** Calea veche ghicea „e yellow?"
  scanând sloturile de spell după școală fizică, deci un auto shot în zbor
  anula penalizarea pe un swing alb.

A cincea, intrată cu etapa 2: **damage-ul de armă se rotunjește**, nu se
trunchiază. `urand((uint32)0.9f, (uint32)1.1f)` e `urand(0, 1)`.

### Etapa 2 — commit, coadă, cablare

Ce s-a schimbat în joc: procul rulează după damage; nimic nu recursează;
grant-urile de extra se adună și se recoltează după procul care le-a pus, cu
mâna care le-a câștigat; scuturile de damage sunt fotografiate; `AttackedBy` o
singură dată; imunitatea e un rezultat; armura numai pe fizic.

Ce **nu** s-a schimbat, și trebuie spus:

- Faza 4 nu e separată — threat, rage, AI și kill sunt tot în `DealDamage`.
- **Cache-ul de profil nu există.** `PerformSwing` construiește două profile
  complete la fiecare swing, plus `MeleeDamageBonusDone/Taken`. §1 nu e
  reparat; codul e mutat undeva unde se **poate** repara. Etapa G îl repară.
- Windfury e tot două `CastCustomSpell` în handler-ul de proc.
- Calea veche e încă în arbore, și e bine: etapa A are nevoie de ea ca
  referință de umbră.
