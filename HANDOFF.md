# HANDOFF — Pivot to an Autonomous Pathfinding/Strategy Bot

> **Read me first.** This document is a complete handoff for continuing
> HelloNeighorBot on the main dev PC in a fresh Claude Code session. It records
> the new direction, the full target architecture, every change idea, a phased
> plan, and the concrete UE4.18 technical details needed to implement it.
>
> The repo currently contains a **TAS-style positional mover** (replays captured
> waypoints by teleporting/gliding the player). **That is not what we want
> anymore.** The new goal is an **autonomous agent** that perceives the game
> world, **pathfinds on its own**, and **plans its own strategy** to complete
> the game. This doc explains how to get from here to there.

---

## 0. How the next session should use this doc

1. `git pull` on the main PC (this file is committed to the repo).
2. Read this whole file, then skim `DESIGN.md` (the *current* TAS design — parts of
   the low-level infra still apply; the high-level mover does not).
3. Confirm the **Open Decisions** in §12 with the user (defaults are provided so
   you can start immediately if they're unavailable).
4. Start at **Phase 0** in §9. Do not try to build everything at once — each phase
   has a concrete "done when" test you can run against the live game.
5. The keystone is the **UE4 reflection SDK** (§6.1). Almost nothing else works
   until that exists. Do it first.

A suggested first prompt for the next session is in §14.

---

## 1. TL;DR of the pivot

| | Old (current code) | New (target) |
|---|---|---|
| Movement | Teleport/glide along **pre-captured** waypoints | **Pathfinds** to chosen targets, walks via real movement |
| Knowledge | None — blind list of XYZ points | **Perceives** actors (doors/keys/neighbor) from game memory |
| Decisions | None — fixed script | **Plans** dynamically (GOAP/HTN); replans as it learns |
| Neighbor | Ignored | **Tracked & evaded** (reactive layer) |
| Author effort | Human walks the route and saves it | Bot figures the route out itself |

We keep the working **infrastructure** (injection, memory utils, config, logging,
ImGui overlay, injector) and the low-level **path-follower** (the repurposed
RouteEngine). We add **perception, world model, navigation, planning, actuation,
and neighbor-evasion** layers, all driven by a real **UE4 reflection SDK**.

---

## 2. Current state of the repo (baseline to build on)

Branch `main`, initial commit `d4e86ab` (plus this handoff). Builds as an x64
DLL + injector via CMake (FetchContent: MinHook, nlohmann/json, Dear ImGui).
**Not yet compiled on a real toolchain** — first build happens on the main PC.

```
src/dll/
  dllmain.cpp            DllMain -> bot thread; defines g_bot.            [KEEP]
  Bot.{h,cpp}            Orchestrator + main loop + hotkeys.             [EXPAND -> agent loop]
  Config.{h,cpp}         JSON config, VK parsing, file search.           [EXPAND -> SDK/nav/planner cfg]
  Logger.{h,cpp}         Console + file logging, ring buffer.            [KEEP]
  Hotkeys.{h,cpp}        Edge-detect GetAsyncKeyState.                   [KEEP]
  mem/Pattern.{h,cpp}    Module info, AOB scan, RIP resolve, SEH RW,     [KEEP + ADD ProcessEvent helper]
                         pointer chains.
  ue4/UE4.h              FVector/FRotator math.                          [EXPAND -> FName/TArray/UObject...]
  ue4/SdkAccess.{h,cpp}  GWorld + transform via pointer chains.          [REPLACE -> full reflection SDK]
  route/RouteEngine.*    Waypoint state machine (teleport/walk).         [REPURPOSE -> nav/PathFollower]
  ui/Overlay.{h,cpp}     D3D11 Present hook + ImGui.                      [KEEP]
  ui/Menu.{h,cpp}        Menu contents.                                  [EXPAND -> world/plan telemetry]
src/injector/injector.cpp  LoadLibrary injector.                         [KEEP]
config/                 example config + example route.                  [EXPAND schema]
docs/                   building / finding-offsets / usage / route-format.
DESIGN.md               Current (TAS) design contract.                   [SUPERSEDED in part]
```

**Pointer-chain transform access (current SdkAccess) is a dead end for autonomy.**
It can only read/write one FVector via a hand-found chain. The autonomous bot
needs to enumerate *all* actors and *call game functions*, which requires real
reflection. SdkAccess becomes a thin compatibility shim over the new SDK (or is
deleted once nothing uses it).

---

## 3. Confirmed technical facts (verified from the binary)

- **Engine: Unreal Engine 4.18** (string `++UE4+Release-4.18` in the shipping exe).
- **Navigation class: `UNavigationSystem`** (the pre-4.20 name; UE4.20 renamed it
  `UNavigationSystemV1`). Use the 4.18 API.
- Implications of 4.18 for reflection (the SDK must match these):
  - **GNames** is the *old* `TNameEntryArray` =
    `TStaticIndirectArrayThreadSafeRead<FNameEntry, ...>` (an array of
    `FNameEntry*` chunks). **Not** the `FNamePool` (that arrived in 4.23).
  - **GObjects** is `FUObjectArray` with a `TUObjectArray` of `FUObjectItem`.
  - These formats are exactly what **Dumper-7** (and most UE4 SDK generators)
    emit for 4.18 — use it.
- Process/exe: `HelloNeighbor-Win64-Shipping.exe`, x64.
- Game dir: `E:\Hello Neighbor (Old Patch - 1.1.6)` (content locked in a ~5 GB pak;
  no editor, no mod API → memory access is the only path).
- Toolchain on main PC must have: VS2022 + "Desktop development with C++" +
  Windows 10/11 SDK, CMake 3.20+, git (FetchContent needs internet on first config).
- **Single-player game** → no anti-cheat concerns; this is a personal speedrun tool.

> ⚠️ Still **verify** all struct offsets / function signatures against a fresh
> Dumper-7 dump of *this* exe. Numbers in the appendix are typical-4.18 values,
> not guaranteed for this build.

---

## 4. The new vision & scope

**Goal:** inject, and the bot autonomously plays Hello Neighbor toward the win
condition as fast as it reliably can — discovering the layout, finding the items
it needs, opening the doors those items unlock, evading the Neighbor, and
sequencing all of that itself.

**What "pathfind on its own" means concretely:** given a target world position or
actor, the bot computes a collision-respecting path (preferably via the game's own
navmesh through `UNavigationSystem`, fallback: our own A* over a probed grid) and
follows it with real character movement — not a hand-authored waypoint list.

**What "comes up with its own strategies" means concretely:** an online
**planner** (GOAP or HTN) operating over a symbolic world model built from
perception. It chooses *which* objectives/items to pursue and in *what order*
based on discovered preconditions (e.g. `HasKey(red) ⇒ CanOpen(redDoor) ⇒
CanReach(basement)`), and **replans** when it learns something new, an action
fails, or the Neighbor interferes. Exploration is itself a planned sub-goal when
required knowledge is missing. (Optional stretch: cross-run learning / route
optimization — see §13.)

**Non-goals (for now):** computer vision (we read memory instead), beating the
neighbor's AI via ML, multiplayer, or anything anti-cheat-related.

---

## 5. Target architecture

A layered **sense → model → plan → act** loop with a **subsumption-style** safety
override (the Neighbor reaction can pre-empt the plan).

```
                       ┌────────────────────────────────────────────┐
                       │                 Agent loop (Bot)            │
                       │  every tick (throttled per subsystem):      │
                       └───────────────┬────────────────────────────┘
                                       │
   ┌───────────────┐   observations    ▼     ┌──────────────────────┐
   │  PERCEPTION    │ ───────────────► WORLD  │  WORLD MODEL          │
   │ enumerate &    │                  MODEL  │  - spatial memory     │
   │ classify actors│ ◄─────────────── update │  - explored map       │
   └──────┬─────────┘                         │  - inventory          │
          │ (uses SDK)                        │  - knowledge base     │
          ▼                                   └─────────┬─────────────┘
   ┌───────────────┐                                    │ state
   │  UE4 SDK       │  GObjects/GNames/ProcessEvent      ▼
   │  (reflection)  │                          ┌──────────────────────┐
   │  + line traces │ ◄────────────────────────│  PLANNER (GOAP/HTN)   │
   └──────┬─────────┘        queries           │  goal: win condition  │
          │                                     │  -> ordered actions  │
          │                                     └─────────┬────────────┘
          │                                               │ current action
          │                          ┌────────────────────▼───────────────┐
          │                          │  NEIGHBOR REACTIVE LAYER (override) │
          │                          │  if threatened: flee/hide/break LOS │
          │                          └────────────────────┬───────────────┘
          ▼                                                │ motion/interaction intent
   ┌───────────────┐   path points   ┌──────────────┐     ▼
   │  NAVIGATION    │ ──────────────► │ ACTUATION    │  AddMovementInput / look /
   │  navmesh / A*  │                 │ PathFollower │  use-key / (debug: teleport)
   └───────────────┘                 └──────────────┘
```

Subsystem cadence (suggested): perception ~5–10 Hz, planner on-event +
low-frequency tick, navigation on new target, actuation/steering every tick,
neighbor check every tick (cheap).

---

## 6. Subsystem designs

### 6.1 UE4 reflection SDK — the keystone (do this first)

Everything depends on being able to (a) enumerate objects, (b) resolve names,
(c) call game functions. Build a proper `ue4/` layer.

**Get the offsets/SDK:** run **Dumper-7** (github: Encryqed/Dumper-7) against
`HelloNeighbor-Win64-Shipping.exe`. It outputs `GObjects`, `GNames`,
`ProcessEvent` index/offset, plus struct/class layouts and function param structs.
For a 4.18 game it will also tell you the GNames format. You do **not** need to
compile the whole generated SDK — extract the offsets and the few classes/functions
we call, and hand-write minimal wrappers (keeps the DLL small and SEH-safe).

**Minimal wrappers to implement (`ue4/UE4.h` + `ue4/Sdk.{h,cpp}`):**
- `FName` (4.18): `{ int32 ComparisonIndex; int32 Number; }`; resolve via GNames
  `TNameEntryArray` → `FNameEntry` → ANSI/WIDE string. Cache index→string.
- `TArray<T>` `{ T* Data; int32 Count; int32 Max; }` with bounds-checked `Get(i)`.
- `FString` = `TArray<wchar_t>`.
- `UObject` accessors at dumped offsets: `Vtable`, `ObjectFlags`, `InternalIndex`,
  `ClassPrivate (UClass*)`, `NamePrivate (FName)`, `OuterPrivate (UObject*)`.
  Helpers: `GetName()`, `GetFullName()`, `IsA(UClass*)`, `GetClass()`.
- `UClass`/`UStruct`: `SuperStruct` walk for `IsA`; (later) `Children`/properties
  if we want field-by-name reads.
- `UField`/`UFunction`: enough to locate a function object for ProcessEvent.
- **GObjects iterator:** `FUObjectArray` → `TUObjectArray` of
  `FUObjectItem { UObject* Object; int32 Flags; int32 ClusterRootIndex; int32 SerialNumber; }`;
  iterate `[0, Num)`, skip null/unreachable.
- **Lookups:** `FindObject(name)`, `FindClass("Class /Script/Engine.Pawn")`,
  `FindFunction(UClass*, "FuncName")` (search class + supers), and a fast
  `ClassByName` cache.
- **ProcessEvent:** `void ProcessEvent(UObject* obj, UFunction* fn, void* params)`.
  Resolve the vtable index (Dumper-7 gives it) or AOB-scan; call through the
  object's vtable. **Wrap every call defensively** — a wrong params layout will
  crash the game. Validate obj/fn non-null, sizes sane; consider a global "panic"
  toggle that disables all ProcessEvent calls if something faults.

**High-level call helpers** (thin, typed; build the param struct, call PE):
- `FVector Actor_GetLocation(UObject* actor)` via `K2_GetActorLocation`.
- `bool Actor_SetLocation(UObject* actor, FVector, bool sweep, bool teleport)` via
  `K2_SetActorLocation` (debug/teleport mode only).
- `void Pawn_AddMovementInput(UObject* pawn, FVector dir, float scale, bool force)`.
- `void Controller_SetControlRotation(UObject* controller, FRotator)`.
- `UObject* GetPlayerPawn(world,0)` / `GetPlayerController(world,0)` via
  `UGameplayStatics`.
- Navigation + traces — see §6.4.

> Param-struct layouts for the key functions are in the **Appendix (§15)** — verify
> sizes/field order against the dump before first call.

**Acceptance:** menu shows the player pawn's class name + live location read via
`K2_GetActorLocation` (not the old pointer chain), and a count of total UObjects.

### 6.2 Perception (`perception/Perception.{h,cpp}`)

- Throttled scan of GObjects (or, cheaper, of the current level's actor list:
  `UWorld->PersistentLevel->AActors` (a `TArray<AActor*>`), plus streamed levels).
- Classify each relevant actor by `UClass` name / `IsA`:
  - player pawn, **neighbor** pawn (the AI), pickups/keys, doors (and lock state if
    readable), drawers/cabinets/containers, levers/valves/switches, ladders/windows
    (traversal aids), trigger volumes, the **objective/win** actor(s).
- Emit `Observation { kind, actorPtr, location, extra... }` list each scan.
- **You must discover the actual class names** for HN from the dump + by listing
  nearby actors at runtime (add a menu "dump nearby actors" button early). Keep the
  class→kind mapping in **config** (`class_map`) so it's tunable without recompiling.
- Distance-cull + only re-classify changed/new actors for performance.

**Acceptance:** menu lists nearby actors grouped by kind, updating as you move; the
neighbor is identified and its position tracked.

### 6.3 World Model (`world/WorldModel.{h,cpp}`)

- **Spatial memory:** known locations of items/doors/objectives (persist even when
  the actor is culled/out of view), with `lastSeen`.
- **Explored map:** occupancy/visited grid (cell size ~100–200 cm) for
  frontier-based exploration; mark cells visited as the bot moves and as it
  observes open space (via traces).
- **Inventory:** read the player's inventory/holding component if locatable
  (preferred), else infer from pickups that disappeared after an interact.
- **Symbolic facts** for the planner: `At(x)`, `Have(item)`, `Open(door)`,
  `Locked(door, by=key)`, `Known(loc)`, `Reachable(loc)`, `Threatened`.
- **Knowledge base (optional persistence):** serialize discovered facts (item
  locations, which key opens which door, good routes) to a JSON next to the DLL so
  later runs start informed. This is the seed of "learning its own strategies."

**Acceptance:** the model accumulates item/door knowledge across the run and
exposes a queryable state to the planner; survives actors leaving view.

### 6.4 Navigation (`nav/Navigator.{h,cpp}`)

**Primary: use the game's navmesh via `UNavigationSystem` (4.18).**
- Get nav system: `UWorld->NavigationSystem` (or
  `UNavigationSystem::GetNavigationSystem(worldCtx)`).
- `FindPathToLocationSynchronously(WorldContextObject, FVector Start, FVector End,
  AActor* PathfindingContext, TSubclassOf<NavQueryFilter> Filter)` →
  `UNavigationPath*`. Read its `PathPoints` (`TArray<FVector>`), `IsValid`,
  `IsPartial`. (Confirm member offsets from the dump.)
- Helpers we'll want: `ProjectPointToNavigation` (snap a target onto the mesh),
  `GetRandomReachablePointInRadius` (exploration), reachability test.
- This gives real, collision-aware routing "for free" wherever the level is
  navmeshed.

**Fallback / gaps: custom A*.** Hello Neighbor's speedrun routes use vaulting
through windows, climbing boxes, and roof/ledge traversal the navmesh won't cover.
For those, and as a no-nav fallback:
- Probe geometry with line/sphere traces (`UKismetSystemLibrary::LineTraceSingle` /
  `SphereTraceSingle` via PE): downward traces for floor height, horizontal for
  walls/ledges, to build a local height/occupancy grid.
- A* over the grid; allow special "traversal edges" for vault/climb where a trace
  finds a reachable ledge within jump range.
- Keep nav vs A* selectable per-target; prefer nav, fall back when nav path is
  invalid/partial.

**Acceptance:** given a target actor across the house, Navigator returns a path
that routes around walls; the bot follows it to within tolerance.

### 6.5 Actuation (`act/Actuator.{h,cpp}` + repurposed `RouteEngine` → `nav/PathFollower`)

- **PathFollower:** consume `TArray<FVector>` path points; each tick steer toward
  the next point; advance when within tolerance; finished at the last point. This
  is the RouteEngine logic generalized to consume a *computed* path instead of a
  saved route. Keep the timer/splits for speedrun measurement.
- **Movement (real, preferred):** face the move direction
  (`Controller_SetControlRotation` toward next point) and
  `Pawn_AddMovementInput(dir, 1.0)` each tick. This respects collision/physics →
  genuine "playing." Alternatively send raw `W`/`A`/`S`/`D` + mouse-look; keep this
  as an option (`actuation.mode: movement_input | raw_keys | teleport`).
- **Look/aim:** smooth `SetControlRotation` toward targets (needed to interact and
  to break/keep line of sight).
- **Interaction:** to use a door/pickup, approach + face it, then trigger "use" —
  simplest is to send the game's interact key; advanced is calling the interaction
  UFunction directly once identified. Confirm by inventory/door-state change.
- **Teleport mode** stays as a debug accelerator and for un-navmeshed shortcuts if
  the user permits it (see Open Decision §12).

**Acceptance:** bot walks (not teleports) to a target via the path and opens a
door / picks up an item, with the world model reflecting the change.

### 6.6 Planner (`plan/Planner.{h,cpp}`)

Recommend **GOAP** first (simplest to get autonomous behavior), HTN later if we
want hierarchical routines.
- **World state:** bitset/map of symbolic facts from the world model.
- **Actions** (precond → effect), each with a cost (≈ estimated time):
  - `GoTo(loc)` — precond `Reachable(loc)`; effect `At(loc)`. Cost = path length / speed.
  - `PickUp(item)` — precond `At(item.loc)`; effect `Have(item)`, `¬Exists(item)`.
  - `OpenDoor(door)` — precond `At(door) ∧ (¬Locked(door) ∨ Have(door.key))`;
    effect `Open(door)`, `Reachable(beyond)`.
  - `UseObject(obj)`, `SolvePuzzle(...)` — domain-specific, added as discovered.
  - `Explore(frontier)` — precond `¬Known(goalDeps)`; effect reveals knowledge.
- **Goal:** the win condition (e.g., reach/trigger the Act objective). Plan with
  A* over action space to satisfy the goal from current state.
- **Replanning:** on new perception facts, action failure, or neighbor override
  ending. If the goal's prerequisites are unknown (don't know where a key is),
  insert `Explore` goals (frontier-based) until knowledge appears, then replan.
- **Why this is "its own strategy":** the action sequence is *derived*, not
  scripted; the same planner produces different routes as the world reveals itself.

**Acceptance:** with a key placed somewhere and a locked door between the bot and a
test objective, the bot autonomously: explores → finds key → fetches key → opens
door → reaches objective, and recovers if interrupted.

### 6.7 Neighbor threat tracking + reactive evasion (`ai/NeighborTracker.{h,cpp}`)

- Track neighbor pawn: position, velocity, distance, and **line-of-sight to player**
  (trace player↔neighbor). If the game exposes an AI awareness/suspicion value,
  read it (find via dump); else infer threat from proximity + LOS + chasing.
- **Subsumption override:** threat level gates behavior:
  - calm → run the plan.
  - suspicious → continue but prefer routes that avoid the neighbor's position/FOV.
  - chased → **flee**: path to nearest known hide spot (closet/under bed) or move to
    break LOS / put obstacles between; optionally just sprint to the next objective
    if faster. Resume the plan when threat clears.
- Keep this layer cheap and always-on; it pre-empts the actuator's current intent.

**Acceptance:** when the neighbor spots/chases the bot, the bot evades and later
resumes its plan instead of walking into capture.

### 6.8 Debugging / observability

This is essential — the bot's "mind" is invisible otherwise.
- **In-world debug draws** via `UKismetSystemLibrary::DrawDebugLine/Sphere/Box`
  (PE): draw the current path, target actor, neighbor + LOS line, frontier cells.
- **Menu telemetry:** current goal + planned action stack, world-model fact list,
  nearby-actor table, neighbor state, FPS/scan timings, "dump nearby actors" and
  "go to selected actor" buttons, actuation-mode switch, panic/stop.
- Heavy logging behind levels.

---

## 7. Mapping to existing code (keep / refactor / replace / add)

**Keep as-is:** `dllmain`, `Logger`, `Hotkeys`, `mem/Pattern` (add a `ProcessEvent`
helper + maybe `SphereScan`), `ui/Overlay`, `injector`, CMake/build.

**Refactor / expand:**
- `Bot` → the **agent loop**: own all new subsystems; run sense→model→plan→act with
  per-subsystem cadence + the neighbor override; keep hotkeys (add: toggle autonomy,
  panic-stop, dump-actors, cycle actuation mode).
- `RouteEngine` → **`nav/PathFollower`**: same follow/timer/splits logic, but
  consumes computed `TArray<FVector>` paths; keep waypoint capture as a debug aid.
- `Config` → load SDK offsets, `class_map`, nav/planner/actuation settings.
- `Menu` → world/plan telemetry + debug toggles (§6.8).
- `ue4/UE4.h` → grow into the math + reflection primitives.

**Replace:**
- `ue4/SdkAccess` (pointer-chain transform) → **`ue4/Sdk`** (full reflection). Keep a
  tiny `GetPlayerLocation()` shim during transition, then delete.

**Add (new files):** `perception/Perception`, `world/WorldModel`, `nav/Navigator`,
`act/Actuator`, `plan/Planner`, `ai/NeighborTracker`, and the SDK
(`ue4/Sdk.{h,cpp}`, plus generated/extracted offset headers).

---

## 8. New / changed config schema (additions to `config.json`)

```jsonc
{
  // existing: process_name, module_name, settings.hotkeys, ...
  "sdk": {
    "gobjects": { "mode": "pattern|rva", "pattern": "...", "rva": "0x0", "rip_relative": true, "pattern_offset": 3, "instruction_length": 7 },
    "gnames":   { "mode": "pattern|rva", "...": "..." },
    "process_event_index": 64,            // vtable index of UObject::ProcessEvent (from Dumper-7); or pattern below
    "process_event_pattern": "40 55 56 57 ...",
    "name_format": "names_array"          // 4.18 -> TNameEntryArray (NOT name_pool)
  },
  "offsets": {                            // verify all via Dumper-7 for this exe
    "uobject": { "class": "0x10", "name": "0x18", "outer": "0x20", "internal_index": "0x0C", "flags": "0x08" },
    "ustruct": { "super": "0x40" },
    "world":   { "persistent_level": "0x30", "nav_system": "0x...", "game_instance": "0x..." },
    "level":   { "actors": "0x0" },
    "navpath": { "path_points": "0x..." }
  },
  "class_map": {                          // HN-specific; discover at runtime, tune here
    "player_pawn": "BP_Player_C",
    "neighbor":    "BP_Neighbor_C",
    "key":         ["BP_Key_C", "Pickup_Key_C"],
    "door":        ["BP_Door_C"],
    "objective":   ["BP_WinTrigger_C"]
  },
  "nav":       { "prefer_engine_navmesh": true, "grid_cell_cm": 150, "arrival_tolerance_cm": 120 },
  "actuation": { "mode": "movement_input", "move_scale": 1.0, "turn_rate_deg_s": 360, "interact_key": "VK_E" },
  "planner":   { "replan_on_new_fact": true, "explore_when_blocked": true },
  "behavior":  { "evade_neighbor": true, "hide_distance_cm": 1500 }
}
```

All names/offsets above are placeholders — fill from the dump and runtime discovery.

---

## 9. Phased implementation plan

Each phase is independently testable **in the live game**. Don't advance until the
"done when" passes. Commit per phase.

- **Phase 0 — Reflection SDK online.** Dump with Dumper-7; implement GObjects/
  GNames/ProcessEvent + UObject/FName/TArray wrappers + lookups.
  **Done when:** menu shows player pawn class name + location via
  `K2_GetActorLocation`, and total UObject count. *(De-risks everything.)*

- **Phase 1 — Real movement.** `Pawn_AddMovementInput` + `SetControlRotation`;
  PathFollower steers to a single target point (straight line, no pathfinding).
  **Done when:** bot walks to a menu-selected actor, respecting collision; stops
  within tolerance.

- **Phase 2 — Pathfinding.** Navigator via `UNavigationSystem`
  `FindPathToLocationSynchronously`; follow `PathPoints`. Add debug path draw.
  **Done when:** bot routes around walls to a target across the house. *(If engine
  nav is unreachable, implement the trace-grid A* fallback here.)*

- **Phase 3 — Perception + World Model.** Classify actors via `class_map`; build
  spatial memory + explored grid + frontier exploration.
  **Done when:** bot autonomously explores and the menu shows a growing map of
  doors/keys/neighbor with remembered positions.

- **Phase 4 — Interaction.** Approach+face+use; pick up an item; open a door;
  reflect changes in inventory/world model.
  **Done when:** bot fetches a key and opens its door unattended.

- **Phase 5 — Planner (GOAP).** Actions/goals/replanning + explore-when-blocked.
  **Done when:** key-behind-locked-door scenario solved end-to-end with recovery
  after a forced interruption.

- **Phase 6 — Neighbor evasion.** Tracker + subsumption override (flee/hide/break
  LOS), resume plan after.
  **Done when:** bot avoids capture during a chase and continues its objective.

- **Phase 7 — Full autonomy + polish.** Chain to an actual Act objective; tune
  speed; optional knowledge persistence + route optimization; measure the run with
  the existing timer/splits.
  **Done when:** bot completes the target act unattended; record a time.

---

## 10. Testing & validation methodology

- Build Release x64 on the main PC; inject into a live game session.
- Use the menu to **step**: select a target, watch path draw, watch the plan stack.
- Validate each PE call in isolation first (read before write; one function at a
  time) — wrong param layouts crash the game, so add a global **panic toggle** that
  disables all PE calls, and SEH-guard the PE wrapper.
- Save before tests; the neighbor makes runs nondeterministic.
- Keep a scratch save right before the objective for fast Phase-5/6/7 iteration.

---

## 11. Risks, unknowns, fallbacks

- **PE crashes** from mismatched param structs → verify against dump, guard with
  SEH + panic toggle, test one function at a time.
- **Navmesh coverage gaps** (windows/climbing/roof routes the speedrun needs) →
  trace-grid A* + special traversal edges; or permit teleport shortcuts for those
  spans if the user allows.
- **Class-name discovery** for HN is empirical → add the "dump nearby actors"
  button early; keep `class_map` in config.
- **4.18 GNames format** must be the old array, not FNamePool → already confirmed
  4.18; Dumper-7 handles it, but double-check name resolution prints real strings.
- **Neighbor AI nondeterminism** → robust replanning + reactive layer; expect
  retries.
- **Perf** (scanning GObjects each tick) → prefer the level actor list, throttle,
  distance-cull, cache class pointers.
- **Inventory inaccessible** → infer from pickup disappearance + interaction events.

---

## 12. Open decisions (confirm with user; defaults let you start now)

1. **Which act/objective first?** *(HN 1.1.6 = full release; Act 1 is the usual
   first speedrun target.)* **Default:** target Act 1 completion.
2. **Movement purity:** strictly in-game movement, or allow teleport for
   un-navmeshed shortcuts? **Default:** real movement everywhere; teleport only as
   an explicit debug/optional shortcut toggle (off by default).
3. **Engine navmesh vs custom A*:** **Default:** prefer engine navmesh, A* fallback
   for gaps.
4. **Cross-run learning / knowledge persistence + route optimization:** in scope?
   **Default:** build the world-model persistence hook (cheap), defer RL/optimizer
   to a stretch.
5. **Planner style:** GOAP first vs HTN. **Default:** GOAP first.

---

## 13. Every change idea (running log — nothing dropped)

- Replace pointer-chain SdkAccess with a real reflection SDK (Dumper-7-sourced). **[core]**
- Add `ProcessEvent` helper to call game UFunctions; typed wrappers for the few we need. **[core]**
- Repurpose RouteEngine → PathFollower that consumes computed paths; keep timer/splits. **[core]**
- Navigation via `UNavigationSystem.FindPathToLocationSynchronously`; read `PathPoints`. **[core]**
- Trace-grid A* fallback for navmesh gaps + special vault/climb traversal edges. **[core]**
- Real movement via `AddMovementInput` + `SetControlRotation` (modes: movement_input / raw_keys / teleport). **[core]**
- Perception: enumerate level actors, classify via config `class_map`, emit observations. **[core]**
- WorldModel: spatial memory, explored grid, inventory, symbolic facts. **[core]**
- GOAP planner with GoTo/PickUp/OpenDoor/UseObject/Explore; replanning + explore-when-blocked. **[core]**
- NeighborTracker + subsumption evasion (flee/hide/break-LOS). **[core]**
- Debug draws (path/target/neighbor/LOS/frontier) via DrawDebug functions. **[obs]**
- Menu: plan stack, world facts, nearby-actor table, dump-actors button, go-to-actor, actuation-mode switch, panic-stop. **[obs]**
- Global panic toggle that disables all PE calls; SEH around PE. **[safety]**
- Config additions: sdk/offsets/class_map/nav/actuation/planner/behavior. **[infra]**
- Knowledge-base persistence to JSON (item→loc, key→door, good routes). **[stretch/learning]**
- Cross-run route optimization / lightweight RL over recorded runs. **[stretch]**
- HTN layer for reusable routines (e.g., "enter via window") on top of GOAP. **[stretch]**
- Hide-spot detection (closets/under-bed actors) for evasion. **[nice]**
- "Practice mode": loop a sub-objective for tuning. **[nice]**
- Use UE4 `GetRandomReachablePointInRadius` for cheap exploration targets. **[nice]**
- Read neighbor AI awareness/suspicion value if locatable for better evasion timing. **[nice]**
- Multi-objective sequencing/optimization once individual objectives are solved. **[stretch]**
- Keep DESIGN.md updated or write DESIGN_AUTONOMOUS.md as the new contract. **[doc]**

---

## 14. Suggested first prompt for the next (main-PC) session

> "Continue the HelloNeighorBot project at `<repo path>`. Read `HANDOFF.md` first,
> then `DESIGN.md`. We're pivoting from the TAS waypoint mover to an autonomous
> pathfinding/strategy bot — the plan and all decisions are in HANDOFF.md. The game
> is UE4.18 at `E:\Hello Neighbor (Old Patch - 1.1.6)`. Start with **Phase 0**: run
> Dumper-7 against `HelloNeighbor-Win64-Shipping.exe`, extract GObjects/GNames/
> ProcessEvent + struct offsets, and implement the reflection SDK so the overlay can
> show the player pawn's class name and live location via `K2_GetActorLocation`.
> Confirm the Open Decisions in §12 with me before/while you go. Build Release x64,
> we'll inject and test against a live game."

---

## 15. Appendix — UE4.18 cheat-sheet (VERIFY against the dump)

**UObject (x64, typical 4.18):** `vtable@0x00`, `ObjectFlags@0x08 (int32)`,
`InternalIndex@0x0C (int32)`, `ClassPrivate@0x10 (UClass*)`,
`NamePrivate@0x18 (FName)`, `OuterPrivate@0x20 (UObject*)`.
**UStruct:** `SuperStruct@~0x40` (walk for IsA).
**FName:** `{ int32 ComparisonIndex; int32 Number; }`.
**TArray<T>:** `{ T* Data; int32 Count; int32 Max; }`.
**FUObjectItem:** `{ UObject* Object; int32 Flags; int32 ClusterRootIndex; int32 SerialNumber; }`.

**Key functions to call via ProcessEvent (find each UFunction by name; build params
to match the dump — field order/padding matters):**

```cpp
// AActor::K2_GetActorLocation() -> FVector
struct { FVector ReturnValue; } p;

// AActor::K2_SetActorLocation(FVector NewLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport) -> bool
struct { FVector NewLocation; bool bSweep; FHitResult SweepHitResult; bool bTeleport; bool ReturnValue; } p; // align!

// APawn::AddMovementInput(FVector WorldDirection, float ScaleValue, bool bForce)
struct { FVector WorldDirection; float ScaleValue; bool bForce; } p;

// AController::SetControlRotation(FRotator NewRotation)
struct { FRotator NewRotation; } p;

// UGameplayStatics::GetPlayerPawn(UObject* WorldContextObject, int32 PlayerIndex) -> APawn*
struct { UObject* WorldContextObject; int32 PlayerIndex; UObject* ReturnValue; } p;

// UNavigationSystem::FindPathToLocationSynchronously(UObject* WorldCtx, FVector Start, FVector End,
//        AActor* PathfindingContext, TSubclassOf<UNavigationQueryFilter> FilterClass) -> UNavigationPath*
struct { UObject* WorldContextObject; FVector PathStart; FVector PathEnd;
         UObject* PathfindingContext; UClass* FilterClass; UObject* ReturnValue; } p;
// then read UNavigationPath->PathPoints (TArray<FVector>)

// UKismetSystemLibrary::LineTraceSingle(WorldCtx, Start, End, ETraceTypeQuery, bTraceComplex,
//        TArray<AActor*> ActorsToIgnore, EDrawDebugTrace, FHitResult& OutHit, bIgnoreSelf,
//        FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) -> bool
// UKismetSystemLibrary::DrawDebugLine(WorldCtx, FVector LineStart, FVector LineEnd,
//        FLinearColor LineColor, float Duration, float Thickness)
```

**Globals:** `GObjects` (FUObjectArray), `GNames` (TNameEntryArray, 4.18 array form),
`GWorld` (already resolvable). `UWorld`: `PersistentLevel`, `NavigationSystem`,
`OwningGameInstance` (→ `LocalPlayers[0]` → `PlayerController` → `Pawn`).
`ProcessEvent`: UObject vtable index (Dumper-7 reports it) or AOB scan.

> Re-confirm every offset/signature with the dump of *this* exe before calling.
> Read-only first, one function at a time, panic toggle armed.
