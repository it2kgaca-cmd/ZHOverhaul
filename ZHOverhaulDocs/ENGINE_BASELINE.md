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
