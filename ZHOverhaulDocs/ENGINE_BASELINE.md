# Zero Hour Overhaul Engine Baseline

Baseline source: TheSuperHackers/GeneralsGameCode `weekly-2026-09-18`.

## 0.0.1 - Pathfinding instrumentation only

This patch deliberately changes no pathfinding decisions or gameplay behavior. It adds Tracy-only instrumentation to `Core/GameEngine/Source/GameLogic/AI/AIPathfind.cpp`.

### Existing plots retained
- `PathfindCells`
- `PathfindPaths`

### New plots
- `PathfindQueueDepthBefore`
- `PathfindQueueDepthAfter`
- `PathfindQueueHighWater`
- `PathfindEnqueueAttempts`
- `PathfindAcceptedRequests`
- `PathfindDuplicateRequests`
- `PathfindMaxCellsPerRequest`
- `PathfindZeroCellRequests`
- `PathfindMissingObjects`
- `PathfindMissingAIUpdates`
- `PathfindBudgetExhausted`
- `PathfindCellsPerRequestAvg`
- `PathfindZoneRecalculation`

### New Tracy zones
- `Pathfinder::processPathfindQueue`
- `Pathfinder::queuedDoPathfind`
- `Pathfinder::findPath`
- `Pathfinder::internalFindPath`
- `PathfindZoneManager::calculateZones`
- `PathfindZoneManager::calculateZones-body`
- `AI::update`

### Why
The upstream instrumentation reports total processed paths and cells, but it cannot distinguish queue pressure, duplicate requests, pathological individual requests, or repeated budget exhaustion. These counters establish a reproducible baseline before changing pathfinding behavior.

### Build target for profiling
Use the repository's `win32-profile` CMake preset and Tracy 0.13.1 as documented in the upstream README.


## 0.0.2 - Pathological path request tracing

This remains profiling-only and deliberately changes no pathfinding decisions.

### Slow request threshold
A queued path request is classified as `SlowPath` when it consumes at least 5,000 path cells, matching the nominal per-frame pathfinding budget.

Each `SlowPath` emits a Tracy message containing:
- logic frame
- object ID and unit template
- controlling player index/type
- AI state ID/name
- blocked/stuck and retry flags
- total cells used by the queued request
- `findPath`, `internalFindPath`, `findClosestPath`, and `patchPath` call/cell/success counts
- cells not accounted for by those primary stages
- current position, requested destination, and state-machine goal position

### New plots
- `PathfindFindPathCalls`
- `PathfindFindPathCells`
- `PathfindFindPathSuccesses`
- `PathfindInternalCalls`
- `PathfindInternalCells`
- `PathfindInternalSuccesses`
- `PathfindClosestPathCalls`
- `PathfindClosestPathCells`
- `PathfindClosestPathSuccesses`
- `PathfindPatchPathCalls`
- `PathfindPatchPathCells`
- `PathfindPatchPathSuccesses`
- `PathfindSlowRequests`
- `PathfindSlowFallbackRequests`

### Trace export for 0.0.2
In addition to the summary/events CSVs, export Tracy messages so slow requests can be grouped by unit, object ID, state, and destination:

```bat
tracy-csvexport.exe -m "Generals Tracy.tracy" > generals_messages.csv
```

The goal of 0.0.2 is to identify the exact call path and game objects behind the 30k-40k-cell outliers observed in the first Twilight Flame capture before any behavioral optimization is attempted.


## 0.0.3 - Large-map pathfinding resource fix

First gameplay-affecting pathfinding change.

The retail engine allocates only 30,000 `PathfindCellInfo` records. Profiling on Twilight Flame showed repeated expensive exact-path failures followed by successful `findClosestPath` fallbacks. Upstream investigation of the same map identifies pathfinding-resource exhaustion as a root cause of valid ravine paths failing.

### Change
- Increase `CELL_INFOS_TO_ALLOCATE` from 30,000 to 500,000.
- This intentionally drops retail pathfinding compatibility for this fork.
- No A* cost/heuristic or movement behavior is otherwise changed in this step.

### New Tracy plots
- `PathfindCellInfoAllocationFailures`
- `PathfindCellInfoInUse`
- `PathfindCellInfoPeakInUse`

### Test goal
On the next Twilight Flame stress run:
1. allocation failures should remain at zero;
2. the repeated Rebel exact-path failure/fallback loop should disappear or fall sharply;
3. total slow-path requests and pathfinding wall time should drop;
4. units should be more likely to complete ravine routes instead of piling against cliffs.

If long valid paths remain expensive after they stop falsely failing, the next optimization target is the open-list implementation rather than suppressing retries.


## Single-player performance policy

This fork is intended for one local player and does not target multiplayer or retail replay compatibility.

### Disabled during live games
- Replay recording / `LastReplay.rep` creation.
- Per-frame command-list serialization and replay-file flushing.
- Periodic live-game synchronization CRC recalculation.

Replay playback code remains available for diagnostics, and CRC generation remains active during replay playback only.

This removes work that exists primarily for deterministic multiplayer and replay reproduction, neither of which is a requirement for this fork.


## 0.0.4 - A* open-list profiling

This step changes no pathfinding decisions. It measures the cost of the current linked-list open set before replacing it.

### Added Tracy zones
- `PathfindOpenList::forwardInsert`
- `PathfindOpenList::reverseInsert`
- `PathfindOpenList::retailInsert`
- `PathfindOpenList::remove`

### Added plots
- `PathfindOpenListInsertCalls`
- `PathfindOpenListForwardCalls`
- `PathfindOpenListReverseCalls`
- `PathfindOpenListRetailCalls`
- `PathfindOpenListTraversalSteps`
- `PathfindOpenListMaxTraversal`
- `PathfindOpenListMaxSize`
- `PathfindOpenListRemoveCalls`
- `PathfindOpenListFastInsertCalls`

### SlowPath message extension
`open=insertCalls/traversalSteps/maxSingleTraversal/maxOpenListSize`

The test target is a large player-issued cross-map move order on Twilight Flame. If traversal steps and insert-zone wall time dominate the giant searches, 0.0.5 will replace the sorted linked list with a priority-queue/heap open set.


## 0.0.5 - Binary-heap A* open set

The 0.0.4 trace showed open-list maintenance consuming roughly half of `internalFindPath` time during the Twilight Flame stress test, with more than 100 million sorted-list insertions in one capture.

### Changes
- Replaced the active A* open set's sorted linked-list ordering with a binary min-heap.
- Equal-cost nodes retain stable insertion order.
- Arbitrary open-set removal remains O(log n) through a heap index stored in `PathfindCellInfo`.
- The old linked pointers are retained only for cleanup/debug enumeration.
- Retail-compatible forward/reverse insertion helpers remain compiled but are no longer used by the active pathfinder.
- Removed per-operation Tracy zones from the active insert/remove path to avoid enormous trace/event exports.

### New aggregate Tracy plots
- `PathfindOpenHeapPushCalls`
- `PathfindOpenHeapRemoveCalls`
- `PathfindOpenHeapSiftSteps`
- `PathfindOpenHeapMaxSiftSteps`
- `PathfindOpenHeapMaxSize`

Slow-path messages now label the open-set tuple as `heap=pushes/siftSteps/maxSift/maxSize`.

### Test
Repeat the same large player-issued move order across Twilight Flame and compare:
1. visible hitching;
2. `Pathfinder::internalFindPath` wall time;
3. `Pathfinder::processPathfindQueue` max/mean time;
4. the new heap aggregate counters.

Do not export a raw events CSV unless specifically needed.

## Crusader muzzle-flash regression

When a model state creates a new render object, the prior code hid muzzle flashes before the new render object existed, then skipped the post-creation hide call. The new object could therefore start with muzzle-flash subobjects visible.

The recreated render object now hides validated muzzle-flash subobjects immediately after `validateStuff()`. Also includes the upstream null guard in `handleClientRecoil()`.


## 0.0.6 - Shared macro-route corridors

This step reduces repeated long-distance work from large move orders without cloning one unit's exact path onto another.

### Behavior
- After a successful long ground path, cache the coarse pathfinding blocks crossed by that route.
- Cache keys include:
  - start and goal pathfinding blocks;
  - locomotor surface mask;
  - unit path radius / centering class;
  - crusher status;
  - human-vs-AI routing mode;
  - start and destination layers.
- A matching unit may reuse the cached block corridor and still runs its own local A* inside that corridor.
- Only long ground-to-ground moves with no ignored obstacle are eligible.
- Cache entries are short lived (12 logic frames) and capped at 64 entries.
- If a reused corridor fails, the pathfinder immediately retries once with the normal hierarchical prepass. Shared routing is therefore an optimization, not a new failure mode.

### Tracy aggregates
- `PathfindSharedRouteHits`
- `PathfindSharedRouteMisses`
- `PathfindSharedRouteStores`
- `PathfindSharedRouteRejected`
- `PathfindSharedRouteBlocksReused`

This is intentionally a conservative first shared-routing step. It shares the strategic corridor while retaining per-unit collision, radius, endpoint, and local path checks.

### Included visual fix
The Crusader muzzle-flash follow-up is included in the same test build. Muzzle-flash visibility now toggles every render subobject attached directly to the configured muzzle-flash bone instead of only the first matching subobject.


## 0.0.6.1 - Formation-friendly shared routing

0.0.6 proved that sharing short-lived macro corridors reduces ordinary A* work, but the first cache key was intentionally conservative. 0.0.6.1 broadens reuse before the 0.1 movement work begins.

### Changes
- Macro-route cache lifetime increased from 12 to 30 logic frames so large queued formations have time to consume a route.
- Radius and center-in-cell are no longer macro-route key fields. They still matter to each unit's local A*, but they do not define which strategic side of the map a formation should travel through.
- Units may join a route produced from the same coarse block or an immediately neighboring coarse block (Manhattan distance <= 1).
- Reused routes get a 3x3 coarse-block join/leave area around the current unit start and requested goal, allowing formation slots to merge onto the shared strategic corridor without cloning an exact path.
- Exact paths and findClosestPath fallback routes use separate cache modes.
- findClosestPath can now reuse recently successful macro corridors. If a reused closest-path corridor fails, it retries once with the normal hierarchical prepass; a successful retry refreshes the cache with the corrected corridor.
- Existing safety restrictions remain: long ground-to-ground routing only, matching locomotor surface mask and human/AI routing mode, no ignored-obstacle route reuse.

### Additional Tracy aggregates
- `PathfindSharedRouteNeighborStartHits`
- `PathfindSharedRouteClosestHits`
- `PathfindSharedRouteClosestRejected`

The goal is to finish the macro-routing baseline before 0.1 introduces destination spreading, yielding, local avoidance, choke flow, and attack-move movement changes.
