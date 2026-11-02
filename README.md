# hydro-recomp

A native recreation of **Hydro Thunder Hurricane** (the Windows / UWP release), rebuilt function by function
from the shipped binary - and checked against it.

> **No game data is included, and none ever will be.** You need your own legally owned copy of the game.
> The executable, `Base.apf` and anything derived from their contents stay on your machine (see
> [Game data](#game-data)).

## How it is built

The rule the whole project follows: **recreate from the decompilation, 1:1.** Nothing is written from what a
feature "should" do, from key names, from the shape of the data or from convention. Where a path has not been
recreated yet it says so (it returns unknown / does nothing) rather than standing in for retail with a guess.
A visible difference from the original game is treated as missing coverage, not as a bug to patch around.

What makes "1:1" a measurement rather than a claim is the **differential oracle**: retail's own functions are
run under emulation (Unicorn), or natively for x87 code, on chosen inputs, and their outputs are captured as
*golden vectors*. `hydro.exe --selftest` then runs this repository's recreation on the same inputs and compares
every written word. Each new check is also **perturbed** on purpose - one constant changed, one rule broken -
with the number of failures predicted in writing first, so a check that cannot fail is caught.

## Progress

Measured, not estimated: see **[docs/INVENTORY.md](docs/INVENTORY.md)** (summary) and
[docs/inventory.csv](docs/inventory.csv) (one row per retail function, with its class, subsystem and status).

| retail functions | 8,638 |
|---|---:|
| **verified** - retail executed in an oracle case and recreated here | **3,695** |
| callee - executed inside a checked case, not recreated on its own (CRT / STL / engine helpers) | 759 |
| cited - recreated or referenced here, but no oracle case runs it yet | 628 |
| absent | 3,556 |

Verified so far, by area: Bullet physics (collision, broadphase, the constraint solver, integration - 154
functions), the boat's tick path and fluid / buoyancy forces, the water wave kernels, the VUJB asset document
reader, VuTransformComponent, the VuEntity constructor's object graph, the whole VuBoatEntity construction (0x41B850: 43 objects, every byte and pointer, and its event registrations), and the
entity type registry (all 294 types - the engine's, the game's and one per template - in retail's order), and
VuProject::load, VuEntity::load's child loading and template instances (the entity tree retail builds from real
project and level data: names, parents, child order, which template made what), the components' loading (an entity's
transform from the tree), and the dynamic light's and the corona's property loads (the bool / float / angle /
colour / vector / string property classes).

**What runs today:** the Hydroelectric track loads and renders - terrain, lights, fog, props, skinned props,
coronas, particle effects, triggers, the event graph, and the ocean with point waves.

**Not there yet** (a visible-feature summary, not the full scope - the inventory is the full scope): the boat
itself (its tick path, construction, registration and teardown are done and checked; it is not yet placed in the world), handling,
AI, wakes, water surfaces, sky, audio, menus, HUD, race rules / checkpoints / laps / timing / modes / boost,
damage debris, the boat camera, input, save / unlocks / settings, multiplayer and leaderboards, the real
shaders (the retail DXBC), the UWP / Xbox 360 look toggle, and retail's full entity framework (the world loader
still reads the level data its own way; moving it onto retail's entity model is the next architectural step).

## Repository layout

| path | what |
|---|---|
| `engine/` | the engine library: platform, renderer, scene, assets (VUJB reader, pack), physics (Bullet 2.79 as retail ships it), utility; `engine/workspace/utility/*` holds the engine-side oracle checks |
| `hydro/` | the game: world loader, water, boat, particles, event graph, game loop; `hydro/workspace/utility/*` holds the game-side checks; `main.cxx` |
| `engine/impl/bullet`, `engine/impl/inflate` | vendored third-party code (Bullet, zlib licence; puff) |
| `reference/` | earlier code kept for reference only - NOT compiled (it was written from inference and culled) |
| `docs/` | the function inventory |
| `hydro.slnx` | the solution |

Style: a unity build per project (`entry.cxx` / `main.cxx`), header-only `.hxx` modules with one-word file names,
no file over ~3k lines.

## Building

Requirements: Windows 10/11 x64, Visual Studio 2022 or newer with the C++ desktop workload (MSBuild, MASM
for `engine/x87.asm`).

```
MSBuild hydro.slnx /p:Configuration=Release /p:Platform=x64
```

The output is `build/hydro/Release/hydro.exe`: the game, with no test data in it.

The self-test is a separate build, because the oracle's golden vectors compile to tens of megabytes of
constants that the game has no use for:

```
MSBuild hydro.slnx /p:Configuration=Release /p:Platform=x64 /p:HydroSelftest=true
```

builds `build/hydro-selftest/Release/hydro.exe`, which carries the golden vectors and runs `--selftest`.
Without the golden data (below) it still builds; `--selftest` then reports that it has nothing to check
against.

## Game data

`hydro.exe` looks for your install, walking up from its own folder (up to 8 levels), for either
`HydroThunderUWP\Base.apf` or `Base.apf`. The simplest layout:

```
<any folder>\
    HydroThunderUWP\     <- your game install: HydroThunder.exe, Base.apf, ...
    hydro-recomp\        <- this repository
    hydro-tools\         <- the research / oracle tools (not in this repository)
```

## Running

```
hydro.exe                                   # the Hydroelectric track, free camera
hydro.exe --race Hydroelectric --mode ... --course ...
hydro.exe --bench --still 200,400,800 --frames 820 --fixed-step 0.016666667 --race Hydroelectric --out <dir>
                                            # deterministic stills (the regression gate)
hydro.exe --selftest                        # every oracle check; exit 0 = all pass (the -selftest build)
hydro.exe --verbose                         # extra diagnostics
```

Other switches: `--no-reflect`, `--no-rig`, `--skip-reflective`, `--background`, `--vantage`, `--drive`,
`--drive-to`, `--stream-rigid-props`, `--shots`.

## The tools (kept outside this repository)

The reverse-engineering and verification tooling lives in a sibling folder, `hydro-tools/`, because it works
directly on the retail binary and the oracle's data embeds asset bytes:

| folder | what |
|---|---|
| `oracle/` | the differential oracle (`oracle.py`, Unicorn), its harness, the native x87 runner, `golden.py` (writes the golden headers the self-test compiles) and their data |
| `codegen/` | instruction-for-instruction translators from the retail listing to C (`asm2c.py`, `sse2c.py`) and `regen.py`, which regenerates every generated block in `hydro/workspace/core/boat/` (`regen.py --check` is a gate) |
| `census/` | data-driven censuses over the binary and the pack, including `inventory.py` (writes `docs/`) |
| `ida/` | IDA headless scripts and small binary-analysis helpers (callers, vtable owners, string references) |
| `audit/` | `constaudit.py` - every unit-conversion constant against retail's bits |
| `build/` | `cycle.ps1` (oracle -> golden -> build -> self-test) and `gates.ps1` (every gate) |

The projects find the generated golden headers through the MSBuild property `HydroGoldenDir` (default:
`..\hydro-tools\oracle\generated\`).

### The gates, after every change

Build exit 0 with a fresh exe; the three bench stills' MD5s unchanged (or the change explained and the new
MD5s recorded); `--selftest` all pass; `constaudit` 0 differing; `regen.py --check` clean.

## Legal

This repository contains no files from the game: no executable, no asset pack, no extracted or derived asset
data. The recreation is written by hand (and by the translators in `hydro-tools/codegen`) from analysis of the
author's own legally owned copy, and you need your own copy to run it. Hydro Thunder Hurricane and its assets
belong to their respective owners.

## Acknowledgements

- **[VuDumper](https://github.com/NextrixVFX/VuDumper)** - the RTTI / SDK dumper for the Vu engine this
  game is built on. Its dump of the retail binary - 1,248 classes with their inheritance, vtable and
  type-descriptor addresses, named engine globals, and each property's loader address - is the map the
  reverse engineering starts from: it says which class a function belongs to and where to look before a
  single function is decompiled, and it is one of the two sources of the generated property schema
  (`engine/workspace/core/entity/schema.hxx`).
- **[Unicorn](https://github.com/unicorn-engine/unicorn)** - the CPU emulator the differential oracle runs on.
  Every "verified" in this project means retail's own machine code was executed under Unicorn on chosen inputs
  and this repository's recreation reproduced its output exactly; without it the claim "1:1" would rest on
  reading code rather than on measuring it.
- **[Bullet Physics](https://github.com/bulletphysics/bullet3)** (2.79, zlib licence) - the physics library
  the retail game ships statically linked. Its source is vendored in `engine/impl/bullet` as reference only -
  it is not compiled: the physics this project runs is recreated function by function from retail's own
  build of it (collision, broadphase, the constraint solver, integration), which is the largest verified
  subsystem so far.
