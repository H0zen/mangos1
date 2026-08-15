# Audit `src/shared/nav`

Audit de defecte al stratului de navigație (C++17). Directorul cerut ca `src/nav` este
`src/shared/nav`; baker-ul stă în `src/tools/extractor/nav`. Data: 2026-08-15.

Sunt **două stive de rutare în viață** (celule + gateway vs. mesh + Polyanya). Calea pe
mesh e declarată „THE MESH IS THE ROUTE”, dar e incompletă și, pe câteva puncte, anulează
motivele pentru care există clearance-ul per-celulă. Calea pe celule e cea care mai ține
rutele între tile-uri.

---

## Stare — ce s-a închis din audit (2026-08-15)

**Nu mai sunt două stive.** Motorul pe celule a fost scos din `Router`; ce a mai rămas
din el (`GateRef`, `Crossing`, `CrossingsOf`, `StitchLocked`, gateway-urile de bordură,
matricea de cost, `FindGateways`/`GatewayCosts`) e **`MARKED FOR DELETION` acolo unde
stă** și trăiește doar pentru că formatul `.nav` mai poartă secțiunea. Pleacă la
următorul bump de versiune de tile.

| # | ce era | acum |
|---|---|---|
| 1 | cititorul `.mesh` refuza rim portals | acceptă `LeavesTheTile()`; **test de round-trip** în `NavMeshTest` (rim + links + map/tile greșit) |
| 2 | `rect.clearance` = minim, otrăvea câmpul deschis | e **maximul** — margine superioară declarată; filtrul real e clearance-ul portalului |
| 4 | `LoadTile` nu promova `pinned` | promovează |
| 5 | Polyanya ignora `MoveProfile` | **arie: exact** (`MoveProfile::AdmitsGround`, o singură definiție, inclusiv regula „walker-ul nu iese la suprafața apei”). **Cost: nu în Polyanya** — vezi mai jos |
| 6 | `maxLength` ignorat pe mesh | `ClipToLength` taie **pe segment**, nu la colțul dinainte; `RouteStop::LengthBudget` |
| 18 | link peste seam de tile, înghițit | raportat de baker la parsarea `offmesh.txt`, singurul loc care vede ambele capete și grila |
| — | mesh-ul n-avea link-uri deloc | `MeshLink` în `TileMesh`, mesh version **11** |

### Legăturile, ca structură separată de `Portal`

Un portal e un **interval** de graniță comună, și tot ce face Polyanya cu el se sprijină
pe asta. Un link n-are graniță comună: are un punct de plecare, un punct de sosire, și
nimic mers între ele — exact ce-l face link și nu deschidere. Un „kind byte” pe `Portal`
ar fi băgat un lucru fără interval în singura structură al cărei înțeles e intervalul.

Deci: listă proprie, expandată de etapa **coarse**, invizibilă etapei fine. Piciorul se
**taie** la gura link-ului, saltul se emite ca un singur segment, Polyanya e întrebată din
nou din gura cealaltă. Nimeni nu merge pe un link.

Autorul lor rămâne perechea `Gateway`(SIDE_LINK) + `Link` din tile; `BuildTileMesh`
rezolvă fiecare gură la dreptunghiul de sub ea **prin înălțime** (un link pe doc s-ar
rezolva altfel pe apa de dedesubt).

### De ce costul pe arie NU intră în Polyanya

`areaCost` transformă problema în *weighted-region shortest path*, unde drumul optim se
**refractă** la granița dintre două costuri (Snell), în loc să meargă drept. Linia dreaptă
în interiorul unei arii convexe e singura proprietate pe care se sprijină toate
proiecțiile de interval de acolo. Costul în bucla aia n-ar face-o mai lentă sau
aproximativă — i-ar face răspunsul **greșit** cât timp ea continuă să pretindă că e optim.

Costul se aplică deci în **coarse**, unde căutarea chiar e peste arii discrete (`relax` ×
`profile.PenaltyOf`). Ce rămâne descoperit, scris aici ca să nu se piardă: **în interiorul
unui singur tile**, un drum mai scurt prin apă mică admisă poate fi preferat unuia uscat
puțin mai lung. Euristica din coarse rămâne admisibilă cât niciun multiplicator nu e sub 1
— ce zice `areaCost` despre sine.

---

## Critice

### 1. Cache-ul `.mesh` e refuzat pentru aproape orice tile cu bordură

`ReadTileGeometry` tratează `Portal::OUTSIDE` (`0xFFFFFFFF`) ca index invalid:

```cpp
// src/shared/nav/NavMeshIO.cpp — citirea portalurilor
if (portal.rect >= out.mesh.rects.size() ||
    portal.neighbour >= out.mesh.rects.size())
{
    ok = false;
    break;
}
```

Orice rim portal are `neighbour == OUTSIDE`. Testul din `NavMeshTest` știe că trebuie
exclus (`if (!p.LeavesTheTile())`); cititorul nu. Rezultat: fișierul scris de baker e
aruncat, `MeshOf` face `BuildTileMesh` pe tick-ul hărții — exact stall-ul pe care
cache-ul trebuia să-l evite.

Nu există test de round-trip IO pentru `.mesh`.

### 2. Clearance-ul minim pe dreptunghi otraveste tot câmpul deschis

`DecomposeTile` crește maximal și se oprește doar la regiune / arie / `CELL_STEEP`.
**Nu** se oprește la clearance. Apoi ia minimul (`NavPolygons.cpp`).

O celulă de bordură are clearance `CELL_SIZE/2 ≈ 0.52 yd` (`Clearance()` pune
`CELL_BORDER` pe orice nod cu un vecin lipsă). Un câmp deschis e **un** dreptunghi care
include tot rim-ul → `rect.clearance ≈ 4` cuante (`0.5 yd`).

`SurfaceAt` citește **întâi** mesh-ul și **nu** cade pe celule dacă mesh-ul a răspuns
(`NavStore.cpp`). `Find` apoi face `Admits` → `Fits(rect.clearance)`. Orice mover cu
`radius > 0.5` (o bună parte din creaturi) primește `TooNarrow` pe Elwynn, nu pe o ușă.
Motivul declarat al bake-ului — un bake, murloc și devilsaur — e anulat de stratul de
mesh.

Polyanya / `CoarseOnMesh` folosesc același byte. Nu e doar seating.

### 3. `FindOnMesh` pe mai multe tile-uri e mort

Gruparea e pe tile; punctul de plecare al tile-ului următor e punctul de **ieșire** al
celui precedent, care rămâne pe tile-ul vechi (`Router.cpp`, bucla din `FindOnMesh`).

`RectAtHeight` refuză un punct al cărui `TileOfCell` nu e tile-ul curent. `MatchRims`
pune crossing-ul pe **centrul celulei de bordură a tile-ului near**, deci strict în
interiorul lui A, niciodată în B.

În plus, `CoarseOnMesh` sare portalurile `LeavesTheTile()` și iese doar prin
`MeshCrossingsOf`. Ultimul `MeshStep` din grupul A e un midpoint interior, nu
crossing-ul. Al doilea `FindMeshPath` e aproape mereu „start off this tile” → `false`
→ tot efortul de mesh e aruncat → fallback pe celule.

Rutele locale (același tile) pot folosi mesh-ul. Rutele între tile-uri — nu.

### 4. `LoadTile` nu promovează `pinned`

```cpp
// src/shared/nav/NavStore.cpp — LoadTileInternal, ambele verificări
if (m_tiles.find(key) != m_tiles.end())
{
    return true;
}
```

Secvența reală din `GridMap`:

1. `PumpWanted` încarcă tile-ul **unpinned**.
2. Un jucător calcă grila; `LoadTile` vede că e deja acolo și iese fără `pinned = true`.
3. `EvictUnpinned` (cap 48) poate evicta un tile pe care grila încă îl ține
   (`GridRef > 0`).
4. `CleanUpGrids` judecă după `GridRef`, nu după `pinned` — cele două ceasuri diverg.

Rutare pe propria grilă a jucătorului: „no ground”.

---

## Logică — aceeași întrebare, două răspunsuri

### 5. Polyanya ignoră `MoveProfile` (arie, cost, `canWalk`)

`FindMeshPath` testează doar `clearance >= QuantiseClearance(radius)`. Portalurile unesc
dreptunghiuri din **aceeași regiune** fără să ceară aceeași arie (țărmul e graniță de
dreptunghi, dar rămâne portal). Un walker pe același tile poate fi trimis prin apă /
lavă.

`Admits()` din router (inclusiv „walkerul nu iese la suprafața apei”) există doar pe
calea de celule.

`areaCost`, `CELL_STEEP`, `CELL_BORDER` — nule pe mesh. Pe celule, apa costă 1.5, lava 5.

Același tile, două algoritmi, două geometrii permise.

### 6. `maxLength` e ignorat pe mesh

`SearchBudget::ForLength` e folosit de flee / confused (`MotionFrame`). `Emit` pe celule
taie la `maxLength`. După `FindOnMesh`, `Find` verifică doar `points.size()` față de
`budget.points`. Nicio măsurătoare în yarzi. O fugă de 10 yd pe același tile poate ieși
`Routed` pe 200 yd. Exact clasa de bug pe care `ForLength` zice că a închis-o.

### 7. `AtGoal` nu verifică etajul

```cpp
// src/shared/nav/Router.cpp
if (goal.cell >= 0)
{
    return inTile == goal.cell;
}
```

Ajungi pe celula-țintă pe **orice** suprafață admisă. Pe un pod, search-ul care calcă
pământul de sub el se oprește. Comentariul de la `RectAtHeight` descrie exact asta (BRD /
Karazhan); fallback-ul pe celule nu a primit același fix.

### 8. `forceDestination` din `RouteRequest` e aproape mort în router

Off-mesh: `Find` iese cu `OffMesh` **înainte** de orice emit. Pathing salvează cazul cu
`BuildShortcut`. În router, flag-ul atinge doar `Emit` pe celule, și doar dacă ruta n-a
fost tăiată. Calea mesh mută mereu ultimul punct pe `request.end`, indiferent de flag.

API-ul minte; decizia e împrăștiată pe două straturi.

### 9. Seating mesh vs. celule

| | Mesh (`MeshSurfaceAt` / `RectAtHeight`) | Celule (`SurfaceUnder`) |
|---|---|---|
| Deasupra corpului | fereastră / 2 | fereastră întreagă |
| Clearance | min pe tot dreptunghiul | celula de sub picioare |
| Înălțime etaj > 0 | `(minZ+maxZ)/2` | eșantionul real |
| `GROUND_CLEARANCE` / `SWIM_SEAT_DEPTH` | nu se aplică | da, în `Emit` |

Aceeași poziție, două etaje / două Z-uri, în funcție de cine răspunde.

### 10. Match la rim: un capăt bun e de ajuns

```cpp
// src/shared/nav/NavMesh.cpp — MatchRims
if (stepLo > window && stepHi > window)
{
    continue;
}
```

Dacă un capăt e treaptă și celălalt e faleză, tot overlap-ul devine crossing. Polyanya
interpolază pe tot intervalul → potențial pas în gol.

---

## UAF / curse / UB

### 11. `NavStores::Find` / `For` vs. `Drop`

`Find` întoarce `NavStore*` după ce eliberează mutex-ul. `For` întoarce referință.
`Drop` șterge `unique_ptr`. `Pathing::HasNavigation` ține pointerul și apoi apelează
`ResidentCount()`. Dacă `Drop` e doar la shutdown, riscul e mic; contractul e tot
dangling.

### 12. `NavStores::MapCount` fără lock

```cpp
size_t MapCount() const { return m_maps.size(); }
```

Citire concurentă pe `unordered_map` = UB. Folosit din comenzi GM.

### 13. `Clear()` uită `m_wanted`

Tile-urile rezidente dispar; lista de want rămâne. `PumpWanted` reîncarcă resturi după
un reset.

### 14. Conversie float→int pe NaN

`CellIndex`, `QuantiseZ`, `QuantiseClearance`: `floor(NaN)` / `NaN + 0.5` apoi
`static_cast<int/uint16_t>` e UB. Poziție coruptă → crash, nu „off map”.

### 15. `Polyanya.cpp` folosește `std::numeric_limits` fără `<limits>`

Trece pe libstdc++ (header leak), poate pica pe libc++. Exact cazul din `CLAUDE.md`
(include what you use; GCC și Clang nu scurg aceleași headere).

### 16. `FineSearch` nu sare intrările stale din heap

`Coarse` o face; `FineSearch` decrementează `budget` și re-extinde. Pe oraș dens poți
lua `NodeBudget` pe o rută care exista.

### 17. `Coarse` (gateway) nu are plafon de expansiuni

Comentariul zice „nu poate fugi”. 64×64 tile-uri × zeci de gateway-uri, pe thread-ul
hărții, fără budget. `CoarseOnMesh` are 20 000; ăsta nu.

---

## Cazuri extreme / bake

### 18. Link off-mesh care taie un seam de tile e înghițit în tăcere

`PlantLinks` cere ambele guri pe **același** tile. Comentariul zice „and says so”;
codul face `continue`. Un link din `offmesh.txt` pe o graniță de tile dispare fără log.

### 19. `MIN_REGION_NODES = 6`

Platforme mici (stâlpi, bărci, pervazuri) sunt șterse. Moverul de acolo e „off mesh”.
Prag magic, fără măsură (spre deosebire de persistența Reeb, care nici nu e folosită la
query).

### 20. Câmpul de distanță nu marchează rim-ul ca obstacol

Header-ul `DistanceField` zice că rim-ul e obstacol ca să nu existe coridor fals pe
marginea tile-ului. `BuildDistanceField` pune 0 doar pe celule unwalkable.
Implementarea și contractul nu se acoperă. (Axa nu e folosită la runtime — vezi
exagerarea.)

### 21. `ClimbWindow` / `tan`

`maxSlopeDeg ≥ 90` → `tan` infinit sau negativ. Valorile din bake sunt 55; un parametru
stricat leagă tot sau nimic.

### 22. `WriteNavTile` nu șterge fișierul parțial

`WriteTileGeometry` face `remove` la eșec; `.nav` lasă treaba pe caller. Baker-ul o
face; orice alt caller uită → fișier pe care serverul îl citește ca valid până la un
check ulterior.

---

## Exagerare

Cod care pretinde o arhitectură pe care query-ul n-o folosește.

| Piesă | Ce zice | Ce face runtime-ul |
|---|---|---|
| Medial axis + ECM | clearance O(boundary), orice rază | `Router` nu o apelează |
| Reeb graph | pasuri / bazine în loc de gateway-uri | mort la query |
| `TileGeometry` | „un plan, trei structuri, evită stall-ul” | ~~cititorul aruncă fișierul~~ (bug 1 închis); axa/Reeb tot nu sunt publicate în `MeshOf` — se păstrează doar `geometry.mesh` |
| Gateway + cost matrix + `Coarse`/`Refine`/`Emit` | fallback scurt cât mesh-ul nu e gata | **șters**; ce a rămas e `MARKED FOR DELETION`, ținut viu doar de formatul `.nav` |
| Polyanya | „optimal, un singur pass, fără smoothing” | arii **da**, `maxLength` **da**; costul stă în coarse (și de ce — vezi „Stare”); multi-tile **încă nu** |

Trei transformări scumpe pe bake (`BuildTileGeometry` citește tile-ul de două ori:
`BuildTileMesh` face `ReadTilePlans`, apoi `ReadTilePlan` din nou) pentru două
structuri pe care routerul nu le întreabă.

---

## Simplificare prea mare

- Un clearance pe dreptunghi maximal = **un** crack dictează un câmp întreg (bug 2).
- Un portal = tot intervalul, chiar dacă jumătate e faleză (bug 10).
- `QuantiseClearance` trunchiază, mesh compară cuante, celule compară yarzi restaurați
  — aceeași rază 0.26 vs. clearance 2 trece pe mesh și pică pe celule.
- `FacingSide` / `BorderCell` au `default:` care tratează orice side necunoscut ca
  `HIGH_Y` / `LOW_Y`. Un `SIDE_NONE` nu e eroare, e un bord greșit.
- `Walkable()` e `area != Blocked`. Nibble 6–15 (fișier corupt) e walkable. `CostOf`
  mapează index invalid pe costul lui `Blocked`.

---

## Cai multiple de ieșire

`Router::Find` are cel puțin opt ieșiri, cu două motoare:

1. start/end fără suprafață → `OffMesh`
2. `Admits` fail → `OffMesh` / `TooNarrow` (clearance-ul otrăvit lovește **aici**,
   înainte de search)
3. tile lipsă → `OffMesh`
4. mesh success → `Routed` / `Partial` (fără `maxLength`, fără arii)
5. mesh fail, aceeași regiune, `FineSearch` fail → `Wall` / `NodeBudget`
6. `Coarse` fail → `WantAlong` + `Wall`
7. `Refine` fail → gol sau picioare parțiale
8. `Emit` → `Routed` / `Partial` (`PointBudget` / `maxLength`)

`ignorePathfinding` și `mayLeaveMesh` nu trăiesc în router; trăiesc în `Pathing`. Un
test care leagă doar `nav` nu poate verifica contractul pe care header-ul îl descrie.

`RouteStop::NoMesh` / `Forced` nu sunt setate de `Find`. Le pune `Pathing`. Enum-ul e
al routerului; valorile nu.

---

## Ce e în regulă

- Tile-ul e `shared_ptr`; unload-ul nu face UAF pe un search în curs.
- `StitchLocked` / `StitchMeshLocked` nu scriu indici de regiune în fișier; re-bake-ul
  unui vecin nu lasă pointeri stale.
- `ReadNavTile` verifică dimensiuni, `n²` la matrice, link-uri, costuri non-finite —
  e unul dintre cititoarele atente din tree.
- `Connect` + `Slope` + `ClimbWindow` partajat bake / stitch / search e gândit corect
  (problema e că mesh-ul nu folosește aceleași reguli de **permisiune**).
- Supercover-ul din `CanWalkLine` (nu greedy diagonal) e fixul bun pentru „clientul
  merge pe coardă”.

---

## Ordinea de atac

Închise (vezi „Stare” sus): 1, 2, 4, 5-arie, 6, 18, plus link-urile pe plasă și marcarea
motorului vechi. Rămâne:

1. **`FindOnMesh` multi-tile (bug 3) — următorul, și cel mai mare.** `leave` pentru
   grupul de pe tile-ul A e încă punctul de **intrare** în ultima arie din A, nu punctul
   de traversare; iar `at` pentru tile-ul B e un punct care stă în A, pe care
   `RectAtHeight` al lui B îl refuză. Cere ca `MeshCrossing` să poarte **două** puncte
   (unul de fiecare parte a graniței) sau un epsilon împins în tile-ul far. Cât timp nu e
   făcut, rutele între tile-uri nu merg deloc — motorul pe celule care le mai ținea nu
   mai există.
2. `AtGoal` / etaj: strat sau `|z - aim.z|` în fereastra de climb (bug 7).
3. Seating: mesh vs. celule dau două Z-uri (bug 9); `GROUND_CLEARANCE` /
   `SWIM_SEAT_DEPTH` nu se mai aplică nicăieri de când `Emit` a plecat.
4. `MatchRims`: un capăt bun nu e de ajuns (bug 10).
5. Gărzi UB: `MapCount` fără lock (12), `Clear()` uită `m_wanted` (13), float→int pe NaN
   (14), `ClimbWindow` cu `maxSlopeDeg ≥ 90` (21).
6. `forceDestination` (bug 8): decizia e împrăștiată pe două straturi, iar acum
   `EmitMeshPath` mută mereu ultimul punct pe `request.end` — cu excepția rutei tăiate de
   `ClipToLength`, care **nu** trebuie să-l primească înapoi (și nu-l primește).
7. Medial axis + Reeb: ori le întreabă cineva, ori ies din bake (vezi „Exagerare”).
