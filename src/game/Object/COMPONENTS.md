# Componentizarea ierarhiei `Object`

Plan arhitectural pentru `src/game/Object/`. Fratele lui
[`src/game/combat/COMBAT.md`](../combat/COMBAT.md), care descrie o singură
verticală; ăsta descrie fundația pe care stă ea și toate celelalte.

Regula care guvernează totul, într-o propoziție: **o entitate ARE
comportamente, nu ESTE ele.**

---

## Măsurătoarea

Nu opinii. Numărat pe arbore, 2026-08-15.

| Clasă | Antet | `.cpp` | Fișiere | Metode |
|---|---|---|---|---|
| `Player` | 4459 | 31514 | 42 | **546** |
| `Unit` | 4107 | 14587 | 14 | **296** |
| `Creature` | 984 | 8363 | 8 | 106 |
| `GameObject` | 879 | 3422 | 5 | 59 |
| `Object` / `WorldObject` | 850 | — | — | ~31 accesori de câmp |
| `Item` | 435 | 2002 | 4 | — |

`Player` moștenește lanțul `Player : Unit : WorldObject : Object`. Un `Player`
răspunde astăzi la peste 840 de metode proprii și moștenite.

---

## Diagnosticul: împărțirea în fișiere nu e componentizare

Arborele **a mai încercat o dată**. `Player.cpp` a fost tăiat în 42 de fișiere
pe subiect — `PlayerQuest.cpp`, `PlayerLoot.cpp`, `PlayerTaxi.cpp` — și `Unit`
în 14.

Nu a cumpărat nimic structural. Verificare:

```
PlayerQuest.cpp   65 definiții, toate 65 sunt Player::
PlayerStats.cpp   35 definiții, toate 35 sunt Player::
PlayerLoot.cpp     5 definiții, toate  5 sunt Player::
```

Clasa a rămas un singur bloc. S-a mutat **textul**, nu **datele**. Antetul a
rămas 4459 de linii pentru că fiecare dintre cele 546 de metode e încă
declarată acolo, fiecare `.cpp` include tot, și fiecare recompilare e totală.

De aici iese testul care contează:

> **Testul de componentă:** dacă extragerea nu mută cel puțin o *variabilă
> membru* de pe proprietar, nu e componentă. E o mutare de text.

`PlayerTaxi m_taxi` trece testul. `PlayerTaxi.cpp` care ar defini metode
`Player::` nu l-ar trece.

---

## Ce există deja și e corect

Practica există în arbore. Nu inventez un stil, îl numesc și îl generalizez.

| Componentă | Proprietar | Ce deține |
|---|---|---|
| `Geometry::Placement` | `WorldObject` | poziția + cadrul; `Where()` / `Place()` |
| `MotionMaster` | `Unit` | stiva de generatoare de mișcare |
| `ThreatManager` | `Unit` | registrul de amenințare |
| `HostileRefManager` | `Unit` | referințele ostile |
| `Diminishing` | `Unit` | grupurile de diminishing returns |
| `EventProcessor` | `Unit` | evenimentele întârziate |
| `Camera` | `Player` | punctul de vedere + notificatorul |
| `PlayerTaxi` | `Player` | masca de noduri + traseul |
| `PetMgr` | `Player` | sloturile de stabul, numărul petului temporar |
| `HonorMgr` | `Player` | rollover-ul zilnic + calculul pe kill |
| `SpellCooldownMgr` | `Player` | harta de cooldown + load/save |
| `ReputationMgr` | `Player` | starea de reputație |
| `CombatRegistry` | `Map` | coada de reacții |
| `combat/pure/` | — | regulile de luptă, fără `Unit` |

`SpellCooldownMgr` e exemplarul. Forma lui e contractul casei:

```cpp
class SpellCooldownMgr
{
    public:
        explicit SpellCooldownMgr(Player* owner) : m_owner(owner) {}
        // ... operațiile pe datele de mai jos ...
        void LoadFromDB(QueryResult* result);
        void SaveToDB();

    private:
        Player*        m_owner;      ///< non-owning
        SpellCooldowns m_cooldowns;  ///< datele, care nu mai sunt pe Player
};
```

---

## Fundațiile

Cinci reguli. Astea sunt „fundațiile solide"; catalogul de mai jos e doar
aplicarea lor.

### F1 — O componentă se definește prin datele pe care le deține

Nu prin subiectul după care e numită. Vezi testul de mai sus. Consecință
practică: extragerea începe întotdeauna de la variabilele membru, nu de la
lista de metode.

### F2 — Deținută prin valoare, cu back-pointer neposesor

`Owner* m_owner` în constructor, componentă membru prin valoare pe proprietar.
Fără `new`, fără `shared_ptr`, fără durată de viață separată de a proprietarului.
Asta e deja practica; o scriu ca să nu se piardă.

### F3 — Fiecare extracție produce, unde se poate, o PERECHE

- **Pur** — fără antete de joc, se leagă fără biblioteca `game`, testabil pe
  masă. Regulile, aritmetica, validările.
- **Cu stare** — deține datele și back-pointer-ul, vorbește cu lumea.

`combat/pure/` + `ProfileBuilder` e perechea deja construită. `Inventory` va fi
la fel: regulile de slot și de bag sunt o funcție pură de (item, slot, sac),
iar `m_items[]` e starea.

Unde nu se poate, se face doar jumătatea cu stare. Perechea e o preferință, nu
o obligație.

### F4 — Cusătura se ține cu un test, nu cu o convenție

`src/tests/CheckCombatBoundary.cmake` citește `#include`-urile din
`combat/pure/` și pică suita dacă vreunul iese din director. Un singur
`#include "Unit.h"` pentru un enum convenabil compilează perfect în `game` și
distruge tăcut testabilitatea.

**Generalizare:** `CheckPureBoundary.cmake`, parametrizat pe director. Fiecare
jumătate pură primește o intrare. Fără asta, F3 se erodează în trei luni.

### F5 — Nimic nu se compune peste o graniță de hartă

Regula vasului. `Placement` o ține deja: două obiecte se compară în cadrul lor
comun. Orice componentă nouă care atinge poziția o citește prin `Where()` și nu
compune niciodată `hull_world ⊗ local_offset`.

---

## Catalogul

Numerele sunt metode numărate azi. „Pur" înseamnă că jumătatea de reguli poate
trăi fără `Unit`.

### `Object` / `WorldObject` — fundația

| Componentă | Ce mută | Metode | Pur |
|---|---|---|---|
| `FieldSet` | `m_uint32Values`, `m_uint32Values_mirror`, `m_changedValues`, `m_valuesCount` + ~31 accesori | ~31 | parțial |
| `UpdateStream` | construcția blocurilor de update, `m_objectUpdated` | `ObjectUpdate.cpp` 774 l. | nu |
| `VisibilitySet` | `m_clientGUIDs` (Player), `UnitVisibility` | 12 | nu |

`FieldSet` e cea mai importantă și cea mai riscantă: o atinge **fiecare**
entitate. De făcut cât e încă cea mai mică, dar nu prima — vezi ordinea.

### `Unit`

| Componentă | Ce mută | Metode | Pur |
|---|---|---|---|
| `Combat::Combatant` | `Profile` + ștampilă, engagement, `m_attackers`, `m_attacking` | ~57 | **da** (există) |
| `AuraContainer` | `m_spellAuraHolders`, `m_deletedAuras`, `m_deletedHolders`, `m_modAuras` | 44 | parțial |
| `Combat::ProcRegistry` | tabela de 262 `&Unit::Handle*Proc` | 15 | **da** |
| `StatBlock` | `m_auraModifiersGroup`, `m_createStats` | 9 (+35 pe Player) | **da** |
| `PowerPool` | `m_regenTimer` + aritmetica unui tick | 7 | **da** |
| `SpeedSet` | `m_speed_rate` + compunerea vitezei | 6 | **da** |
| `SummonedObjects` | `m_dynObjGUIDs`, `m_gameObj`, `m_wildGameObjs`, `m_guardianPets` | 11 | nu |

### `Player` — cel mai rău, și cu cel mai mare câștig

| Componentă | Ce mută | Metode | Pur |
|---|---|---|---|
| `Inventory` | `m_items[]`, coada de update, `m_enchantDuration`, `m_itemDuration` | **95** | **da** (regulile de slot/sac) |
| `Persistence` | serializarea; nu deține date proprii | **80** | nu |
| `QuestLog` | `m_timedquests`, sloturile de quest | 65 | parțial |
| `SpellBook` | `m_spells`, talentele, învățarea | 20 | parțial |
| `SocialState` | grup, canale, LFG, mail, duel | ~25 | nu |
| `ZoneState` | zonă, area trigger, rest, instanță | ~16 | nu |
| `Vitals` | oglinda de respirație/oboseală, moartea, învierea | 19 | parțial |
| `ActionBar` | `m_actionButtons` | 5 | da |

`Inventory` singură e 95 de metode și scoate `Player` din afacerea cu sacii.
`Persistence` e 80 de metode care nu sunt un jucător, sunt un serializator.

### `Creature`

| Componentă | Ce mută | Metode | Pur |
|---|---|---|---|
| `CreatureScaling` | scalarea pe nivel din template | `CreatureLevel.cpp` | **da** |
| `CreatureSpells` | cooldown-urile de creatură | `CreatureSpellCooldown.cpp` | parțial |
| `VendorTrainer` | listele de vendor/trainer | `CreatureVendorTrainer.cpp` | nu |

### `GameObject`

| Componentă | Ce mută | Metode | Pur |
|---|---|---|---|
| `GameObjectUseRegistry` | `switch` pe 17 `GAMEOBJECT_TYPE_*`, 818 linii | — | parțial |
| `Destructible` | starea de distrugere | `GameObjectDestructible.cpp` | nu |

Aceeași formă ca `ProcRegistry`: un `switch` mare deghizat în metodă.

### `Item` / `Bag`

`Bag : Item` e moștenire pentru un singur fapt — că are sloturi. Candidat clar
la `Container` ca **componentă opțională** pe `Item`, nu la o subclasă.

---

## Ordinea

Fiecare pas ține CI verde pe GCC, Clang și MSVC. Săgeata înseamnă „deblochează".

```
0. Regulile (documentul ăsta) + CheckPureBoundary.cmake generalizat
        │
        ├──► 1. ProcRegistry        (mecanic, 262 intrări, risc mic)
        │         │
        │         └──► 2. GameObjectUseRegistry  (aceeași formă, a doua oară)
        │
        ├──► 3. Combatant + StatBlock + PowerPool + SpeedSet   (Unit)
        │         │
        │         └──► 4. AuraContainer          (cel mai împletit de pe Unit)
        │
        ├──► 5. Inventory           (Player, cel mai mare câștig)
        │         │
        │         └──► 6. Persistence, QuestLog, SpellBook, restul Player
        │
        └──► 7. FieldSet + UpdateStream   (Object; atinge tot, se face ultimul)
```

**De ce `ProcRegistry` primul:** e cel mai mecanic lucru din listă și e o
tabelă, nu o rețea. Scoate 15 metode de pe `Unit` fără să atingă o singură
variabilă membru, deci nu poate strica starea. E pasul care dovedește forma
înainte să o aplicăm undeva unde greșeala doare.

**De ce `FieldSet` ultimul, deși e fundația:** e singurul care atinge fiecare
entitate din arbore. Se face după ce forma e dovedită de șase ori, nu înainte.

**De ce `Combatant` înaintea lui `AuraContainer`:** jumătatea pură există deja
(`combat/pure/`), iar `AuraContainer` e cel mai împletit lucru de pe `Unit` —
aurele ating stat-urile, procurile, imunitățile și vizibilitatea.

---

## Ce se plătește la fiecare pas

Nu „cod mai curat". Concret:

- **Testabil.** `combat_pure` se leagă în `mangos_tests` fără biblioteca de
  joc. Fiecare jumătate pură nouă face la fel. Astăzi martorul unei reguli de
  inventar e un server pornit și un client conectat.
- **Cache-uibil.** O metodă n-are unde să-și țină rezultatul; un obiect are.
  `Profile` + ștampila de murdărie e exact motivul pentru care §17 din
  [`COMBAT_ENGINE_REVIEW`](../../../COMBAT_ENGINE_REVIEW.md) nu se poate plăti
  fără `Combatant`.
- **Durata de viață devine vizibilă.** `attacker.DealDamage(victim, ...)` e o
  metodă pe atacator care mută victima; de-asta a fost scriibil UAF-ul din §3.
  O componentă are un proprietar numit și o graniță.
- **Compilare.** 4459 de linii de antet incluse aproape peste tot.

---

## Ce NU face planul ăsta

- Nu introduce un ECS. Componentele sunt membri prin valoare cu un
  back-pointer, exact ca `SpellCooldownMgr`. Fără registru de entități, fără
  sisteme care iterează arhetipuri, fără alocare dinamică.
- Nu schimbă comportament. Fiecare pas e o mutare; regulile se corectează în
  planurile lor (`COMBAT.md`), nu aici.
- Nu aplatizează lanțul de moștenire. `Player : Unit : WorldObject : Object`
  rămâne. Componentizarea îl face mai subțire; ruperea lui e altă discuție și
  vine, dacă vine, după.
