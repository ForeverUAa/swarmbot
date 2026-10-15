# Swarmbot

Swarmbot is an experimental [Geode](https://geode-sdk.org/) mod for Geometry Dash. It tries to find a winning input sequence by running attempts, measuring how far each attempt gets, and evolving better candidates with a genetic algorithm.

> **Status:** experimental. A successful build only confirms that the mod compiles. The solver is still being tested in-game and is not guaranteed to solve levels.

<img src="logo.png" width="150" alt="Swarmbot logo" />

## Download

Get the latest Windows build from the [Latest build release](https://github.com/ForeverUAa/swarmbot/releases/tag/latest). Download the `.geode` file and install it with Geode.

The automated release currently builds **Windows x64**. The project metadata lists Geometry Dash 2.2081 and Geode SDK 5.8.2 as its target versions.

## Use it

1. Open a level in the **level editor**.
2. Click **SWARM**, below the editor's playtest controls.
3. Let the playtest run. The HUD shows the algorithm, game mode, attempt number, current progress, and best progress.
4. When a candidate dies, the solver records its result and starts another attempt. After a population has been tested, the genetic algorithm evolves the next generation.

The solver runs candidates **sequentially** through Geometry Dash's actual player. It does not create a visible crowd of independent players.

To stop the search, leave the level. The game speed is reset when the level closes.

## How it works

- **Input:** the solver chooses whether to hold the jump input on each physics step. This is a basic input model, not a complete controller for every mechanic.
- **Attempt scoring:** the current implementation primarily scores a candidate by how many physics steps it survives. The HUD also tracks current and best horizontal progress.
- **Evolution:** the genetic algorithm keeps top candidates, selects parents with tournament selection, crosses their input sequences over, and mutates inputs near the best known death point. After several stagnant generations, it injects fresh candidates.
- **Physics hook:** the mod hooks `GJBaseGameLayer::processCommands` using the Geometry Dash 2.2081 bindings to drive the solver alongside game command processing.
- **Reset and completion hooks:** player deaths trigger candidate scoring and resets. Completing the level stops the search and records a successful attempt.
- **Metrics:** each attempt is appended to `metrics.csv` in the mod's save directory.

This approach is a work in progress. Survival time is only a rough fitness signal, and a longer attempt does not always mean a better input sequence. Levels requiring precise mode-specific control, timing beyond a single hold input, or mechanics not represented by the solver may not be solvable yet.

## Settings

The mod exposes these settings in Geode:

| Setting | What it controls |
|---|---|
| **Enable Solver** | Master switch for the solver. |
| **Algorithm** | The general algorithm setting used by the framework. The editor's **SWARM** button currently selects the genetic algorithm directly. |
| **Max Attempts** | Attempt limit for the general controller. The editor swarm currently runs without an attempt cap. |
| **Solve Speed** | Game speed multiplier while solving. The default is 6x, with a range of 1x to 20x. |
| **Search Granularity** | Step size for the backtracking algorithm. |
| **Max Hold Length** | Maximum hold interval considered by backtracking. |
| **Population Size** | Number of candidates in each genetic generation. |
| **Mutation Rate** | Probability of flipping an input during mutation. |
| **Mutation Window** | Range around the best known death point where mutations focus. |
| **Annealing Temperature** | Initial temperature for the annealing algorithm. |
| **Show Obstacle Sensing** | Adds debug sensing information to the HUD. It does not currently guide the genetic search. |
| **Auto-solve Daily** | Experimental scaffold only. Automatic Daily navigation is not implemented. |

The `[` and `]` keys adjust solving speed while a level is active.

## Metrics

The solver appends attempt results to `metrics.csv` in the mod's save directory. The CSV contains fields such as session ID, algorithm, level ID and name, attempt number, success, best progress, death frame, frame count, and elapsed time.

You can open the CSV in a spreadsheet or use Python/pandas to compare attempts. Keep in mind that the current fitness function is based mainly on survival frames, not a full understanding of level geometry.

## Build from source

You need the [Geode SDK](https://docs.geode-sdk.org/getting-started/), the Geode CLI, and a configured C++ build environment.

With `GEODE_SDK` pointing to your SDK installation:

```sh
geode build
```

The GitHub Actions workflow builds the Windows mod on pushes to `main`, uploads the `.geode` file as an artifact, and updates the [Latest build release](https://github.com/ForeverUAa/swarmbot/releases/tag/latest). Pushing a version tag such as `v1.0.1` creates a versioned release.

## Project layout

```text
src/
  core/        Solver controller, algorithm registry, input types, metrics,
               saved solutions, and obstacle sensing
  hooks/       Geometry Dash gameplay hooks, editor button, HUD, and keybinds
  algorithms/  Genetic, backtracking, hill-climbing, annealing, random, and replay
  daily/       Experimental Daily-level runner scaffold
```

The framework contains several algorithms, but the editor's **SWARM** action currently starts the genetic algorithm specifically.

## Links

- [Geode website](https://geode-sdk.org/)
- [Geode documentation](https://docs.geode-sdk.org/)
- [Geode SDK source](https://github.com/geode-sdk/geode)
- [Geode CLI](https://github.com/geode-sdk/cli)
- [Geometry Dash bindings](https://github.com/geode-sdk/bindings)
- [Actions and build logs](https://github.com/ForeverUAa/swarmbot/actions)
- [Latest build](https://github.com/ForeverUAa/swarmbot/releases/tag/latest)
