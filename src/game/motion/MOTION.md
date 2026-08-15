# Audit `src/game/motion` + `MotionGenerators` + `movement`

Audit de defecte al planului de mișcare (Helm), al generatoarelor și al spline-ului
care rulează pe server. C++17. Data: 2026-08-15.

`src/game/motion` e planul (Course / Path / Pace / Motion). `MotionGenerators` e
politica (cine conduce, unde vrea să stea). `src/game/movement` e execuția
(`MoveSpline`) și, de la Helm încoace, firul (`Course` + `CourseWire` din
`MoveSplineInit::Launch`).

Sunt **trei** implementări ale aceleiași curbe. Când nu calculează aceeași durată
sau aceeași formă, clientul și serverul se despart. Deck-ul e tratat corect (fără
`hull ⊗ offset`). Problema nu e nava.

---

## Verdict

`MoveSpline` e ce **rulează** serverul. `Course` e ce **pleacă pe fir**.
`Path`/`Pace`/`Motion` e un al treilea ceas, folosit la taxi. Roster-ul pe ranguri
a schimbat contractul lui `MoveIdle` și al lui `MovePoint` în timpul unui chase.

## Stare — 2026-08-15: **motorul vechi de spline nu mai există**

`src/game/movement/` mai are exact două fișiere: `MoveSplineInit.h/.cpp`, lansatorul.
Șterse: `MoveSpline`, `spline.h/.cpp/.impl.h`, `packet_builder`, `util.cpp`,
`MoveSplineFlag`, `MoveSplineInitArgs`, `typedefs.h`.

**Nu mai sunt trei ceasuri, nici două curbe, nici două `Instant`.** `Helm::Course` e
planul, firul **și** poziția. `Unit::movespline` a dispărut; întrebările lui se pun
acum cursului:

| înainte | acum |
|---|---|
| `movespline->Finalized()` | `Unit::IsTravelling()` |
| `movespline->Initialized()` | `Unit::HasCourse()` |
| `movespline->currentPathIdx()` | `Unit::CoursePointIndex()` → `Course::PointIndex(now)` |
| `movespline->ComputePosition()` | `Course::At(now)` / `Course::Heading(now)` |
| `movespline->_Interrupt()` | `Unit::AbandonCourse()` |
| `movespline->GetId/Duration/Elapsed/IsSmooth` | `Course::Id/Duration/Elapsed/GetCurve` |
| `movespline->updateState(diff)` | nimic — cursul nu se avansează, se întreabă |
| `PacketBuilder::WriteCreate` | `Helm::Wire::WriteCreate` |
| `Movement::computeFallTime/Elevation` | `Helm::Fall::Time/Drop` |

### Ce a trebuit construit ca să plece motorul

- **Căderea** (`Course::Falling` + `Fall.h`). Constantele sunt ale **clientului**, mutate
  din `movement/util.cpp` unde planul nu avea voie să ajungă. Firul ridică `FLAG_FALLING`
  și clientul integrează singur înălțimea. Asta era singura excepție care ținea tot
  constructorul vechi de pachete în viață.
- **Create-block-ul** din curs. Cel vechi dumpa vectorul intern *paddat* (controlul
  reflectat + coada duplicată), pe care clientul îl mai padda o dată — deci cine vedea
  doar `SMSG_UPDATE_OBJECT` desena altă curbă decât cine vedea `SMSG_MONSTER_MOVE`.
- **Tăierea drumurilor lungi** (defectul 2). Colapsul la 2 puncte era singurul răspuns
  imposibil de corectat: punctele interioare *sunt* colțurile ocolite. Acum se ia cel mai
  lung prefix care supraviețuiește împachetării.

### Defecte închise din lista de mai jos

**1** (`Pace` cronometra coarde) · **2** (`!Fits`) · **3** (`MoveIdle` chiar oprește:
curăță și oprește moverul) · **4** (o singură `Helm::Curve`; un singur `Instant`, din
`CourseTime.h`) · **5** parțial · **7** (home: captura se face mereu; `Hold` cât e
`NOT_MOVE`, nu `Done` — gata cu evade-ul pe loc după Frost Nova) · **8** (confuzia cere
drum) · **9** (`Initialize` doar pe câștigător; Home/Effect rămân înainte, fiindcă doar
*captează*) · **10** (`MoveStatus::haveLeg` — `(0,0,0)` nu mai e un goal) · **11**
(`SetFacing(nullptr)`) · **12** (plafonul fugii pe ambele `Move`) · **14** (`flying` nu
mai vine din `CAN_FLY`/`LEVITATING`) · **15** (create-block) · **17, 19, 20, 26**
(dispărute cu spline-ul) · **18** (`isfinite` + prag de viteză în `Plan`) · **21**
(`Elapsed` prin `Since`) · **25** (`Wrap2Pi` pe Inf).

Din nav: **`maxLength`** e din nou aplicat (`ClipToLength` + `RouteStop::LengthBudget`).

**Două rupturi de build găsite abia acum**, pentru că ținta `game` nu mai fusese
compilată de la `b7a35530` (doar `mangos_tests`):

- `DBCStructure.h` lua `Helm::Path` dintr-un `#include "Path.h"` gol — `src/game/motion`
  e înaintea lui `src/game/WorldHandlers` pe calea de include. Acum e calificat.
- `MotionFrame.h` mai avea `using Movement::PointsArray/Vector3` după ce tipurile au
  trecut în `namespace Motion` (`MovementIntent.h`). Șterse.

### Rămas

**16** (cyclic — a plecat cu spline-ul; dacă se vrea, se implementează pe `Course`),
**13** (evenimentele DBC pe nodurile de taxi), **22** (`Roster::Active()` pe listă goală),
**23** (`SkewSince` după evicție), **5** (`MIN_RELAY_DISTANCE` fix), **6**
(`TransportFrame::FromWorld` e identitatea și nu poate să nu fie — comentariul minte),
și numele: `MoveSplineInit` ar trebui să se cheme altfel, dar nu în commit-ul care
scoate un motor.

---

## Critice / mari

### 1. Taxi: `Motion` cronometrează coarde, firul cronometrează arce

`Motion::Begin` apelează `Pace::Time`, care adună doar `Path::SegmentLength` (coardă
3D). `Course::Plan` și `MoveSpline::SegLengthCatmullRom` măsoară arcul Catmull-Rom
în 3 pași (`STEPS_PER_SEGMENT = 3` = `kSmoothSteps`).

`FlightPathMovementGenerator` construiește `m_flight` cu `Curve::CatmullRom`, apoi
`PointIndex` / `Update` retrag noduri și demontează după **ceasul pe coarde**.
`Launch()` întoarce `course.Duration()` (arc). Arcul ≥ coarda → generatorul
termină **devreme**: evenimente de nod, unmount, `Finalize` (inclusiv
`SetFallInformation`) în timp ce clientul și spline-ul mai zboară.

Pe un taxi TBC cu multe coturi, diferența se adună. Nu e 1 ms.

**Fix:** `Pace` folosește `SegmentArc` când curba e Catmull-Rom, sau taxi-ul citește
`Course` / spline-ul, nu `Motion`.

### 2. `!Fits`: clientul primește alt drum decât cel pe care merge serverul

În `MoveSplineInit::Launch`, spline-ul e deja `Initialize` pe **tot** drumul. Dacă
un punct interior iese din plicul packed (11+11+10 biți, sfert de yard → ±256 /
±128 yd față de mijlocul origin–dest), se reconstruiește un `Course` cu **două
puncte**, se trimite ăla, `SetCourse` ăla, `return` durata **coardei**.

```cpp
// src/game/movement/MoveSplineInit.cpp
if (!Helm::Wire::Fits(course))
{
    // Plan(front, back) — spline-ul rămâne pe polilinia întreagă
}
unit.SetCourse(course);
return int32(course.Duration());
```

Urmări:

- Clientul merge pe coardă (prin geometrie).
- `RefreshPoseFromCourse` urmează coarda, apoi sare pe spline.
- Timerele care folosesc valoarea din `Launch()` expiră cât spline-ul încă rulează.
- Dacă origin == dest după colaps, `Plan` e gol, `WriteLaunch` nu scrie nimic,
  durata 0.

Lovește exact rutele lungi de navmesh (țărm, ocol de munte). Cursurile Smooth sar
`Fits`. Canarul de drift de durată rulează **înainte** de colaps, deci nu prinde
asta.

**Fix:** taie drumul în bucăți care `Fits`, sau re-inițializează spline-ul pe
aceleași puncte pe care le trimiți. Nu întoarce o durată pe care spline-ul nu o
execută.

### 3. `MoveIdle` nu mai oprește mișcarea

După trecerea la `Roster` pe rang, `MoveIdle` doar **adaugă** `&si_idleMovement` la
`Rank::Routine`. Activ rămâne cel cu rangul cel mai mare.

```cpp
// src/game/MotionGenerators/MotionMaster.cpp
void MotionMaster::MoveIdle()
{
    if (empty() || !isStatic(top()))
    {
        m_roster.Add(&si_idleMovement, Helm::Rank::Routine);
    }
}
```

Un chase (`Combat`) sau flee (`Panic`) **continuă**. Apelează: `PetAI`, `GuardAI`,
`CreatureEventAI`, `TransportMap`, `SpellAuraControl`, scripturi. Calea de moarte
e OK (`Clear(false, true)` apoi `MoveIdle`). Un pet pe Stay în timpul follow-ului
nu se oprește.

Când idle **chiar** preia (același rang ca waypoint / wander, cel mai nou câștigă),
generatorul de jos **nu** e `Interrupt`-uit și spline-ul **nu** e oprit. Idle
`Update` e no-op: patrollerul merge restul weld-ului fără `OnArrived` / scripturi /
`MovementInform`. Unii calleri (`CreatureAI`) fac `StopMoving` ei înșiși;
`ScriptAction`, pets, transporturi, EventAI adesea nu.

Pe stiva veche, idle devenea vârful. Asta e o schimbare de contract, nu o nuanță.

### 4. Trei Catmull-Rom, două capete

| Implementare | Capătul de start | Capătul de final | Folosit de |
|---|---|---|---|
| `MoveSpline::InitCatmullRom` | **reflectat** `2p0−p1` | duplicat | poziția serverului |
| `Course::Controls` | reflectat | duplicat | firul / clientul |
| `Helm::Curve::Control` | **clamp** (duplicat) | duplicat | `Motion::At` / `SegmentArc` |

Capetele cad pe noduri în ambele variante (`t=0` → `w0=0`), deci testele de
„începe pe start” trec. **Forma și tangenta primului segment nu**. `Course` și
spline-ul se acoperă; `Motion` nu.

În plus, **două** `enum class Helm::Curve` în același namespace
(`Segmented`/`Smooth` vs `Linear`/`CatmullRom`). Orice TU care vede ambele e
ill-formed. `WaypointMovementGenerator.cpp` include ambele lanțuri (`Motion.h` și
`Unit.h` → `Course.h`).

Convenția corectă (client + `Course` + `InitCatmullRom`): Catmull-Rom uniform,
τ = 1/2, `t ∈ [0, 1]` pe segment; fereastra `(Pᵢ₋₁, Pᵢ, Pᵢ₊₁, Pᵢ₊₂)`;
non-ciclic: `P₋₁ = 2P₀ − P₁`, `Pₙ = Pₙ₋₁`.

---

## Logică (generatoare)

### 5. `MotionDriver` înghite o destinație nouă sub 0.5 yd

Comentariul zice „toleranța intent-ului”. Codul e `MIN_RELAY_DISTANCE = 0.5` fix.
Chase-ul recalculează spot-ul la 100 ms; dacă noul `m_dest` s-a mutat cu 0.4 yd,
driver-ul nu re-pune piciorul. Generatorul a cerut `Move`, driver-ul ignoră.

### 6. `TransportFrame::FromWorld` e identitatea

`PointMovementGenerator` convertește „coordonate de script / lume” prin
`FromWorld`. `TransportFrame` **nu** override-uiește; moștenește identitatea. Pe
punte, un `MovePoint` din script cu coordonate de continent e citit ca offset de
punte (mii de yarzi). Nu poți compune `hull⊗offset` (regula navei); deci
conversia **nu poate** exista. Comentariul pretinde că există.

### 7. Home evade: rooted/stunned „ajunge acasă” pe loc

`MoveTargetedHome` nu e blocat de root (`UNIT_STAT_LOST_CONTROL` e doar flee/charm).
`HomeMovementGenerator::Initialize` iese imediat pe `UNIT_STAT_NOT_MOVE` și lasă
`m_haveHome == false`. Cât e root, `UpdateMotion` nici nu rulează
(`UNIT_STAT_CAN_NOT_MOVE`). Când cade root-ul, primul `Intent`:

```cpp
if (!m_haveHome || status.arrived || status.blocked)
{
    m_arrived = true;
    return Motion::MoveIntent::Done();
}
```

`Finalize` rulează `JustReachedHome` pe locul de luptă: reset de combat, addon,
fără drum acasă. Evade-in-place după frost nova / stun.

Comentariul „n-a putut fi trimis acasă tot contează ca acasă” e drept pentru o
**rută eșuată**, nu pentru „n-am capturat home-ul pentru că era rooted la
`Initialize`”. Captura de home trebuie făcută mereu (de-asta init e înainte de
push). Dacă `NOT_MOVE`, `Hold` până poate merge; `Done` doar după un attempt
real.

Dacă ruta apoi eșuează, `MOVE_NONE` cade pe linia dreaptă prin zid. Evade-ul
trebuie să se termine (`blocked` → `Done`), deci e intenționat-ish, dar
creatura „ajunge acasă” prin geometrie.

### 8. Confused fără `MOVE_REQUIRE_PATH`

Poate tăia pe scurtătură prin obstacole. Flee cere path.

### 9. `Mutate` + ranguri: `MovePoint` în timpul chase-ului nu preia volanul

Documentat în comentariu. Un script `MovePoint` (Errand) pe o creatură în chase
(Combat) întrerupea înainte chase-ul; acum chase-ul rămâne activ, Point stă
dedesubt. Chase-ul e `Interrupt`-uit doar dacă noul rang **câștigă**. Aici nu
câștigă, deci **nici Interrupt**. Chase continuă. Scriptul crede că a trimis
creatura undeva.

Mai rău: `Initialize` rulează **înainte** de `Add`, necondiționat.
`PointMovementGenerator::Initialize` face `StopMoving()` și pune
`UNIT_STAT_ROAMING*`. Efect: omoară spline-ul de chase și șterge
`UNIT_STAT_CHASE_MOVE`, apoi Point stă sub chase. Următorul tick de chase
re-pune piciorul. Hitch vizibil, și un Point care n-a condus niciodată. Același
clasă de bug pentru orice `Initialize` cu efecte secundare (`Random` pune
`UNIT_STAT_ROAMING` cât chase-ul e live).

**Fix:** `Initialize` doar pe câștigător, sau desparte „capture / fără side
effects” de „preia controlul” (Home deja depinde de init-before-push pentru
`GetResetPosition`). Nu `StopMoving` decât dacă `top() == this` după `Add`.

### 10. Chase/follow: „niciun goal pus” e originea hărții

`MoveStatus::legGoal` rămâne `(0,0,0)` cât `m_haveLeg` e false. Targeted, după
primul tick (`m_recheckTime` e 0), apelează `RequiresNewPosition(owner,
status.legGoal)`: „ținta e încă în slop față de **(0,0,0)**?”

- Continent: aproape mereu „nu” → dest calculat. Merge din întâmplare.
- Hartă de transport: originea **este** puntea. O țintă la ~`0.75 * combat
  reach` de originea modelului (sau un `LayLeg` eșuat, care lasă `m_haveLeg ==
  false`) face `needDest` false → `Hold` fără dest. Chase/follow îngheață.
- Intent nu citește `status.blocked`, deci o rută eșuată nu forțează retry.

**Fix:** `bool haveLeg` pe `MoveStatus`. `needDest = !m_haveDest ||
!status.haveLeg || RequiresNewPosition(...)`. `status.blocked` = dest nou acum.

### 11. `SetFacing(const Unit*)` fără null

`MoveSplineInit::SetFacing(const Unit* target)` dereference imediat.
`MotionDriver` verifică lookup-ul; alți calleri nu.

### 12. Flee: plafonul de 30 yd cade la orice re-punere mid-bolt

Primul `Move` duce `.WithinLength(FLEE_PATH_LENGTH_LIMIT)`. Cât `status.traveling
&& m_haveFleePoint`, restatement-ul e doar `MOVE_REQUIRE_PATH`, fără limită. O
schimbare de viteză mid-bolt pune `m_speedChanged` și `LayLeg` din nou **fără**
capul de 30 yd. Chiar înainte de gaura de `maxLength` de pe mesh, generatorul
își lasă propriul budget.

**Fix:** `WithinLength(FLEE_PATH_LENGTH_LIMIT)` pe ambele `Move`, sau păstrează
flags/limit pe punctul de flee.

### 13. Taxi: evenimentele DBC pe nod nu se mai trag

`FlightPathMovementGenerator::Update` avansează `m_currentNode` cu toggle-ul
vechi departure/arrival (`+1` / `+0`) dar **fără** `DoEventIfAny`. `PointIndex`
e un nod pe punct de spline, nu două. Toggle-ul tot ajunge la orice index
întreg (nu e buclă infinită), dar `DepartureEventID` / `ArrivalEventID` de pe
nodurile din mijloc nu se mai apelează. Rămân doar `PassJunction` și
`HandleMoveSplineDoneOpcode`.

### 14. `CAN_FLY` / `LEVITATING` forțează Catmull-Rom + bit de zbor

În constructorul `MoveSplineInit`, `args.flags.flying` e setat din
`CAN_FLY | FLYING | LEVITATING`. `isSmooth()` e **doar** bitul flying: selectează
`ModeCatmullrom`, durată pe arc în 3 pași, `Gait::Fly` / `FLAG_SMOOTH_FLYING`.

Un walker levitant pe 3+ puncte e animat și cronometrat ca zbor, la viteză de
mers / alergare (`SelectSpeedType` rămâne walk/run dacă `MOVEFLAG_FLYING` nu e
efectiv setat). Hop-urile pe 2 puncte sunt aproape liniare.

### 15. Create-block vs. launch: fantomele Catmull-Rom ies pe fir

`InitCatmullRom` (folosit și pentru **linear**) padd-uiește:

| | non-ciclic | ciclic |
|---|---|---|
| `points[]` | `[2P₀−P₁, P₀…Pₙ₋₁, Pₙ₋₁]` | `[Pₙ₋₁, P₀…Pₙ₋₁, P₀, P₁]` |
| `index_lo…index_hi` | `1 … n` | `1 … n+1` |

`Helm::Wire` / `WriteCatmullRomPath` trimit controale **ne-paddate**.
`PacketBuilder::WriteCreate` dump-uiește **tot** vectorul intern.

Cine vede doar `SMSG_UPDATE_OBJECT` (intră în rază / login) desenează altă
curbă decât cine a văzut `SMSG_MONSTER_MOVE`. Clientul re-paddează; primul
segment pornește în spatele unității, plus o coadă de lungime zero.

### 16. Cyclic e implementat pe spline și scos de pe fir

`SetCyclic()` nu e apelat azi. Dacă e:

- Server: `init_cyclic_spline` + `time_passed % Duration()` la nesfârșit.
- `Course::Plan` e o polilinie **finită** (fără închidere, fără wrap). `FlagsOf`
  nu emite `FLAG_CYCLIC` / `FLAG_ENTER_CYCLE`.
- `Course::Controls` nu wrap-uiește: ultimul segment e coadă duplicată, nu
  `(Pₙ₋₂, Pₙ₋₁, P₀, P₁)`.

Clientul joacă drumul deschis o dată și se oprește. După primul tur, dezacord
total. `Enter_Cycle` e hardcodat 0.

### 17. Cădere pe o polilinie care urcă

`FallInitializer` pune pe fiecare nod timpul **absolut** de cădere de la Z-ul de
start, nu un Δt. `initLengths` cere stamp-uri monotone. Dacă un punct ulterior e
**mai sus**, stamp-urile scad → assert în debug, segmente negative în release.

`MoveFall` e 2 puncte (doar coborâre), deci e în siguranță. `SetFall` +
`MovebyPath` nu. `isSafeFall` e mereu `false`.

---

## UAF / UB / curse

### 18. `Course::Plan` — NaN/Inf → `uint32` (UB)

Nu respinge puncte non-finite (`Path::Build` da) și nu pune un prag pe `speed`.
`1000/speed` → Inf, `uint32(float + Inf/NaN)` e UB în C++17 (`[conv.fpint]`).
Apoi `RefreshPoseFromCourse` scrie rezultatul în poză.

### 19. `computeIndex` — împărțire la lungime 0

```cpp
// src/game/movement/spline.impl.h
u = (length_ - length(index)) / (float)length(index, index + 1);
```

`ComputePosition` evită cazul (`seg_time > 0`). Calea `evaluate_percent(t)` nu.
Puncte coincidente → Inf/NaN în poziție.

### 20. `currentPathIdx` pe spline ciclic cu `last == first`

`point % (last - first)` — modulo 0, UB. Latent cât `SetCyclic` nu e folosit.

### 21. `Motion::Elapsed` — „înainte de start” = „gata”

`now - m_at` unsigned: 1 ms înainte de start arată ca ~49.7 zile. `At()`
întoarce destinația. `Course::Elapsed` folosește `Since` semnat și întoarce 0.
`CourseTime.h` există exact ca să nu scrii comparația așa. Taxi-ul trece
`getMSTime()` în `PointIndex`.

### 22. `Roster::Active()` pe listă goală

`m_entries[0]` fără gardă. `UpdateMotion` assert-uiește `!Empty`. `top()` /
`GetCurrent()` nu. Un caller pe roster gol e out-of-bounds.

### 23. `ClientClock::SkewSince` după evicție

Istoricul e tăiat la 256 de rapoarte; `m_skewTotal` nu scade. Dacă `when` e mai
vechi decât primul stamp păstrat, `before` rămâne 0 → întreaga datorie a
sesiunii, nu datoria de la `when`. Comentariul zice că subestimează;
implementarea **supraestimează**. Un burst la 15 fps umple 256 sloturi în ~17 s;
un taxi ține minute. `CourseSync` trage reparații premature (limitate de
`kFloor`).

`ClientClock` are mutex; rețea vs. world e în regulă.

### 24. `Quantise` / `PackedFits` convertesc înainte să verifice domeniul

`lround` pe Inf/valoare uriașă e nespecificat, apoi cast la `int32`. `Fits` e
plasa de siguranță; plasa însăși poate fi UB.

### 25. `Wrap2Pi` pe ±Inf nu se oprește

`while (a < 0) a += 2π` pe `-Inf` blochează thread-ul lumii.

### 26. Overflow `int32` pe durata spline-ului

`CommonInitializer::time` e `int32`. `Validate` cere doar `velocity > 0`.
`Σlen / v` mare → overflow float / cast negativ. `Duration()` ~ 24.8 zile, sau
`time_passed % Duration()` pe durată negativă (UB). Fallback-ul
`length() < 1 → 1 sau 1000` nu repară un tabel intermediar deja stricat.
`Course` folosește `uint32`; un wrap acolo vs. `INT32_MAX` e încă un dezacord.

---

## Spline — ce e corect, ce nu

Catmull-Rom aici (aliniați `Course` și `MoveSpline`, **nu** `Helm::Curve`):

- Uniform, τ = 0.5, `t ∈ [0, 1]` pe segment.
- Segmentul `i` interpolază **Pᵢ → Pᵢ₊₁** din `(Pᵢ₋₁, Pᵢ, Pᵢ₊₁, Pᵢ₊₂)`.
- Non-ciclic: **P₋₁ = 2P₀ − P₁**, **Pₙ = Pₙ₋₁**.
- Durata e **arc chordal în 3 pași / viteză**, nu parametrul CR și nu arcul adevărat.
- În interiorul segmentului, `u` e liniar în **timp**.

**Corect (măsurat pe 2.4.3):**

- Accumulator semănat la 1 ms, trunchiat pe segment — `Course` și, aproximativ,
  `CommonInitializer`.
- Packed: 11/11/10, rotunjire la cel mai apropiat, plic `[-1024,1023]` /
  `[-512,511]` cuante.
- Fără byte de seat pe `MONSTER_MOVE_TRANSPORT` (câmp 3.x; corect pentru 2.4.3).
- `FLAG_RUNNING` din gait, nu OR-uit pe toate pachetele de travel Helm.
- Deck: `Where()` e deja local; `DeckVesselGuidOf` din mapă; nicio compunere de hull.
- `Stop(forceSend)` după `_Interrupt` — clientul e anunțat.
- `MoveFall` pe 2 puncte: XY fix, Z = `z₀ − ½gt²` plafonat la dest, durata =
  `computeFallTime(Δz)`, `FLAG_FALLING` pe fir.

**Greșit sau dublat:**

- `_checkPathBounds` e comentat în `Validate`; limita din el era 1024 **yarzi**
  (de 4× prea largă). `Fits` e plasa reală, apoi e ocolită prin colaps la 2
  puncte (defect 2).
- `CommonInitializer` e `int32(float(time) + seg*velInv)`, `Pace` e `floor` pe
  `double`. Pot diferi cu 1 ms (canarul din Launch e calibrat). Pe curbe, `Pace`
  nici măcar nu măsoară arcul (defect 1).
- Clientul reconstruieste tabelul de lungimi cu ~20 eșantioane; serverul cu 3.
  Durata **totală** e pe fir, deci capetele coincid. Părțile de timp pe segment
  diferă unde curbura variază. `Course::Slack` modelează asta; `MoveSpline` nu.
- Cădere: `FallInitializer` (gravitație) vs. `Course` (polilinie). Launch evită
  să scrie căderea ca `Course` — corect. Până când Course are cădere, rămân doi
  constructori de pachete.
- `FLAG_PARABOLIC` / `FLAG_NO_SPLINE` nu se emit (incertitudine 2.4.3). OK.
- Falls / „stand here” încă trec prin `WriteMonsterMove`, care OR-uiește
  Runmode. Helm a reparat asta doar pentru travel. NPC-urile care cad animează
  alergare. `appendPackXYZ` trunchiază; Helm rotunjește.
- `computeFallElevation` (3 argumente): după timpul terminal, integrează tot
  cu 60.15, nu cu `termVel` (7 la safe-fall). Neapelat azi (overload-ul cu 1
  argument, v₀ = 0).
- `Frozen` / `No_Spline` nu schimbă `_updateState`. Un flag „frozen” tot ajunge
  la `Result_Arrived`.
- Evaluarea Catmull-Rom din `Curve.cpp` (clamp) nu e cea din client.

---

## Facing / derivate

- Facing-ul final se aplică doar dacă `done && isFacing()`. `Final_Target`:
  serverul **nu** calculează heading (clientul se întoarce spre țintă; serverul
  păstrează ultima direcție de mers).
- Segment de lungime zero: `evaluate_derivative` e 0 → `atan2(0, 0)` → 0.
  Heading-ul sare pe +X. `Motion::Heading` păstrează `m_lastHeading`;
  `Course::Heading` nu.
- Derivata liniară ignoră `t` (coardă constantă). Corect pentru linear.

---

## Exagerare / stive paralele

| Strat | Rol declarat | Rol real |
|---|---|---|
| `MoveSpline` | execuția | **da** — poziția la tick |
| `Course` + `CourseWire` | planul = firul | **da** la `Launch` / `SetCourse` |
| `Path` / `Pace` / `Motion` | același plan, testabil | taxi `PointIndex`; ceas **greșit** pe curbe |
| `Roster` + ranguri | înlocuiește stiva | da, dar `MoveIdle` și `MovePoint` vs chase s-au schimbat |
| `MotionDriver` + Intent | un singur loc de launch | da; `MIN_RELAY` e prea prost |
| `ClientClock` + `CourseSync` | repară clientul rămas în urmă | doar zbor; SkewSince evictuit e zgomot |

Trei ceasuri, două `Curve`, două `Instant` (`uint32` în `CourseTime.h`,
`uint32_t` în `Motion.h`). Același diagnostic ca nav: două motoare pentru
aceeași întrebare.

`Launch()` întoarce durata **Course**; generatoarele care așteaptă pe valoarea
aia nu așteaptă spline-ul.

---

## Ce e în regulă

- Targeted: `FollowerReference`, `isValid()` înainte de dereference; bearing-ul
  de chase pe **poziții de frame**, nu `Where().BearingTo` pe punte.
- Confused: ancora e `MoverPosition` (deck-local), nu cache-ul world.
- Flee: distanța e invariantă; bearing-ul e din frame. Guid, nu pointer.
- `HomeMovementGenerator::Initialize` întreabă `top()` **înainte** de `Add` —
  evadarea citește ancora generatorului vechi.
- `EffectMovementGenerator` nu pretinde „arrived” pe un spline pe care nu l-a
  lansat.
- Moarte: refresh din Course, apoi `StopMoving`, apoi `Clear`+`MoveIdle`.
- Nicio compunere navă↔lume în `motion` / `MotionFrame` / `MoveSplineInit`.
  Spline-ul își copiază punctele (`memcpy`); `facing.target` e un guid; nicio
  referință la `Unit` după `Launch`.
- `Since` / `Advance`: uint32 modular → int32 e convenția corectă de wrap.
- Packed în plic: `uint32(negativ) & mask` e two’s-complement corect.
- Linear și CR folosesc ambele `InitCatmullRom`, ca indexarea
  `getPointCount()-3` de pe pachet să coincidă cu vectorul paddat — pe
  monster-move Helm, nu pe create-block (defect 15).

---

## Ordinea de atac

1. `Pace`/`Motion`: arc Catmull-Rom ca `Course`/`MoveSpline` — taxi-ul se
   demontează la timpul clientului.
2. `!Fits`: taie sau re-inițializează spline-ul; aceeași geometrie pe fir și pe
   server.
3. `MoveIdle`: `Clear` până la default sau `Add` la un rang care chiar preia
   volanul; `Interrupt` + `StopMoving` când idle devine activ. `Initialize`
   fără side effects până după `Add`. Decide explicit dacă `MovePoint` bate
   chase-ul.
4. Home: capturează mereu destinația în `Initialize`; `Hold` cât `NOT_MOVE`.
   Chase: nu trata `(0,0,0)` ca goal pus; citește `status.blocked`.
5. Un singur `Helm::Curve`, un singur accumulator, un singur capăt Catmull-Rom
   (reflect + duplicate).
6. Create-block: trimite aceleași controale ca monster-move, nu vectorul intern
   paddat. Nu seta `flying` din `CAN_FLY`/`LEVITATING` pe un hop de sol.
7. Gărzi UB: `isfinite` în `Plan`/`Quantise`, `Elapsed` prin `Since`,
   `SkewSince` după evicție, `computeIndex` pe lungime 0, `Wrap2Pi` pe Inf.

Vezi și `src/shared/nav/NAV.md` — `maxLength` ignorat pe mesh lovește flee-ul
(`FLEE_PATH_LENGTH_LIMIT = 30`) pe același tile.
