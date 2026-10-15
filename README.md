# Swarmbot

A [Geode](https://geode-sdk.org/) mod for Geometry Dash that experiments with **evolutionary level solving** and records per-attempt metrics.

<img src="logo.png" width="150" alt="the mod's logo" />

## What it does

Open a level in the editor and press the **SWARM** button below the playtest controls. Swarmbot launches the level's playtest and begins an evolutionary search, testing one input sequence per attempt and changing candidates after deaths. A small HUD shows the current attempt and best progress. Results are recorded in a CSV in the mod's save directory.

## Status

- ✅ Deterministic input/replay foundation (see below)
- ✅ Pluggable algorithm framework + metrics logging
- ✅ Three built-in algorithms: `backtracking`, `random`, `genetic`
- 🚧 Daily-level auto-runner (scaffolded; navigation TODO — see `src/daily/`)

## How it works

### Determinism (the important part)

The original prototype counted frames in `PlayLayer::postUpdate`, which runs once
per *rendered* frame. Geometry Dash steps its physics at a fixed 240 steps/second
and runs a variable number of those steps per rendered frame, so a per-render
frame index drifts between attempts and replays desync.

dashback instead hooks **`GJBaseGameLayer::processCommands`**, the function the
game calls once per fixed physics step. Counting those calls yields a frame index
that is identical across attempts regardless of framerate. Input is applied via
the game's own **`handleButton`** entry point, so it enters exactly as a real
press would. This is the fix for the flakiness that plagued the prototype.

### Architecture

```
src/
  core/
    Types.hpp            StepContext (incl. GameMode) / DeathInfo / AttemptResult / InputState
    Algorithm.hpp        the interface every solver implements
    AlgorithmRegistry.*  name -> factory, so algorithms are selected by string
    SolverController.*    orchestrator; owns the algorithm + metrics, driven by hooks
    MetricsRecorder.*     appends one CSV row per attempt
    SolutionStore.*      saves/loads completed input sequences for E2E replay
    Perception.*         on-demand obstacle sensing (damaging/static objects ahead)
  hooks/
    GJBaseGameLayerHooks.cpp  processCommands  -> deterministic step + input
    PlayLayerHooks.cpp        init/destroyPlayer/resetLevel/levelComplete/onExit + HUD
    MenuLayerHooks.cpp        entry point for the daily runner
    KeybindHooks.cpp          [ / ] live speed control
  algorithms/
    SequenceSearch.hpp        shared hold-sequence helpers (mutate/crossover)
    BacktrackingAlgorithm.*   DFS backtracking over variable-length hold intervals
    GeneticAlgorithm.*        evolutionary solver (tournament + crossover + mutation)
    HillClimbingAlgorithm.*   (1+1) local search; annealing = same class, temp > 0
    RandomSearchAlgorithm.*   pure-random baseline
    ReplayAlgorithm.hpp       plays back a stored solution end-to-end
    Builtins.cpp              registers the built-ins
  daily/
    DailyRunner.*             daily auto-solve scaffold
```

Algorithms (Algorithm setting): `backtracking`, `genetic`, `hillclimb`, `annealing`, `random`.
All share one mode-agnostic representation — a per-frame hold bit — which is
correct for every game mode (cube/ship/ball/UFO/wave/robot/spider/swing); the
mode is only used for awareness. Perception is a foundation for future
reactive/RL algorithms and is not consumed by the search algorithms above.

The controller lifecycle, driven entirely by game hooks:

```
PlayLayer::init          -> onLevelStart   pick algorithm, start attempt 1
GJBaseGameLayer::         -> onStep         deterministic frame + input, every physics step
  processCommands
PlayLayer::destroyPlayer -> onDeath         record attempt, let algorithm learn, reset
PlayLayer::resetLevel    -> onAttemptStart  begin next attempt
PlayLayer::levelComplete -> onComplete      record success, stop
PlayLayer::onExit        -> onLevelEnd      tear down
```

### Adding a new algorithm

1. Subclass `dashback::Algorithm` (see `algorithms/BacktrackingAlgorithm.*` for the
   simplest example). Implement `name()` and `decide(ctx)`; optionally
   `onAttemptStart` / `onDeath` / `onComplete` to learn between attempts.
2. Register it in `algorithms/Builtins.cpp`:
   ```cpp
   registry.reg("my-algo", [] { return std::make_unique<MyAlgorithm>(); });
   ```
3. Select it via the **Algorithm** setting (value = the id you registered).

Nothing else changes — the controller, metrics, and HUD work with any algorithm.

## Metrics

Each attempt appends a row to `<mod save dir>/metrics.csv`:

```
session_id,algorithm,level_id,level_name,attempt,success,best_progress,death_frame,frames,wall_ms
```

Load it into a spreadsheet or pandas and group by `algorithm` to compare
success rate, attempts-to-solve, and wall-clock time.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| Enable Solver | on | Master switch |
| Algorithm | `backtracking` | `backtracking` \| `random` \| `genetic` |
| Max Attempts | 0 | Give up after N attempts (0 = unlimited) |
| Auto-solve Daily | off | Experimental daily auto-runner |

## Build instructions

Requires the [Geode SDK](https://docs.geode-sdk.org/getting-started/) and CLI.

```sh
# with GEODE_SDK pointing at your SDK clone
geode build
```

# Resources
* [Geode SDK Documentation](https://docs.geode-sdk.org/)
* [Geode SDK Source Code](https://github.com/geode-sdk/geode/)
* [Geode CLI](https://github.com/geode-sdk/cli)
* [Bindings](https://github.com/geode-sdk/bindings/)
