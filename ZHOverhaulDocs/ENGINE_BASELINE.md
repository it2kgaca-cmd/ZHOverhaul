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
