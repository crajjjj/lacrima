# CLAUDE.md

Guidance for Claude Code in this repository.

## What this is

Lacrima is an SKSE plugin (`Lacrima.dll`, C++23 on CommonLibSSE-NG, alandtse fork
v15.2.0 as a git submodule at `lib/commonlibsse-ng`) plus a small Papyrus layer. It
draws procedural tears: fluid that wells up below the eyes and runs down the face. It
is a framework: nothing cries until a script, another plugin, the console harness or
the settings menu asks.

The fluid core is adapted from OStrands (LeonVanWinkle / Saint, GPL-3.0); NOTICE.txt
lists exactly what. Lacrima is GPL-3.0 because of it.

## Build

- `xmake f -m release` then `xmake`: DLL into `dist\SKSE\Plugins`. Never run a bare
  `xmake f --<option>=...`: it resets the rest of the config; always pass `-m release`
  and every non-default option. `xmake f -m release --copy_to_mod=y` also copies
  `dist\*` into `%XSE_TES5_MODS_PATH%\Lacrima` (option `mod_name`).
- `xmake build papyrus`: Pyro compiles `papyrus\Source` into `dist\Scripts` and writes
  `Release\Lacrima-<version>.zip` (the whole `dist` tree). `xmake build release` also
  rebuilds the DLL first.
- `python tools/make_tears.py`: regenerates `dist\meshes\Lacrima` and
  `dist\textures\Lacrima`. The bone names (`BlobNN`, `RopeNN`) and the rope bone count
  (`ROPE_BONES` = `Tears::kRopeBones`) are shared with `src/Tears.cpp`.
- The version lives in `xmake.lua` (`PROJECT_VERSION`). Release tags are `v<version>`.
- `.gitignore` ignores `dist/**` except the hand-written config, the CUE yaml and the
  generated meshes and textures (which are committed so a build needs no Python).

## Architecture

- **Threading.** `Hooks.cpp` registers an input-event sink that queues one SKSE task
  per frame; that task runs `Tears::Update` on the main thread. Papyrus natives, C
  exports and the menu never touch the scene graph: they queue requests
  (`Tears::Cry/Stop/Shed/Wipe`) or read a snapshot (`Query`, `Status`). While the game
  is paused, `Update(0, true)` still applies requests and refreshes status text, but
  does not simulate.
- **Face** (`Face.cpp`): finds the actor's face and eyes head-part geometry by the head
  part record's editor ID (the name the game gives that geometry), reads the vertices
  (facegen dynamic data when present, so expressions move the surface) and dedupes the
  face's on a 0.25-unit voxel. The eyes only place the ducts: their centers come from
  the eyes mesh split by the mesh-space midline (mesh space has Z up and the face
  looking along +Y or -Y, decided from where the eyes sit); the eyeball is not a
  collision surface, it sits a millimetre behind the lid skin and would steal the
  nearest-patch pick. Each duct is the face vertex nearest a point below and in front
  of the eye. Per frame `Of(actor, ..., region)` skins only the points whose bind
  position can land in the caller's world-space region (the box is taken into each
  joint's skin space and tested on the point's heaviest joint) plus the points within
  6 units of a duct, and reports `morphing` when the expression moved any of them
  since the last frame.
- **Tears** (`Tears.cpp`): one `Crier` per actor, one `Cheek` per eye. Everything is
  simulated and drawn in **head space**: the head bone's axes and origin, in metres
  (`Frame`: 70 x the bone's scale world units per metre; world down and `9.81 / scale`
  are rotated into it each frame). The skin stands still there, so a tear at rest
  **sleeps** (the solver's sleep, which a world-space fluid on a moving body never
  reaches) and costs nothing until `Wake` stirs it: an expression moved the skin
  (`View::morphing`), the head tilted 15 degrees from where the tears settled, a hand
  came within reach, or an anchor / moving neighbour touched it. The tear nodes hang
  under the head bone with identity transforms, so the engine carries them between
  updates, and a cheek whose tears all sleep skips the solver and the draw
  (`Cheek::dirty`). No recentring, no speed caps to outrun, no cell clears. A tear is
  a run of linked particles emitted at the duct and anchored there while it wells,
  then released. `Fluid::Solver` steps at 120 Hz against the face patches near the
  awake particles, with one neighbour list per step (2.5 spacings of slack) on a
  sorted flat `CellGrid`. Particles that fall free get their life cut to
  `[physics] fall`. An actor beyond `[tears] max_distance` from the camera, or whose
  3D is app-culled (the player in first person), is **frozen**: no emission, no
  solver, no draw; trail life still ticks. Drawing is OStrands' blob / rope / drop pass
  on persistent scratch buffers.
- **Hands** (`Tears::PoseHands`): palm + thumb/index/middle/ring finger capsules from
  the skeleton's hand and finger bones, in head space with velocities relative to the
  head, passed to the solver as colliders (so a hand smears tears) when `[wipe] hands`
  is on; a particle within a capsule's reach has its life cut to `[wipe] absorb`, and
  a sleeping one is woken first. `WipeWithHand` plays an idle (default
  `IdleWipeBrow`, Skyrim.esm 0977EC) through `AIProcess::PlayIdle` and arms
  `Crier::wipeArm`; `Gesture` dries the face (`DryFace`) when any hand capsule comes
  within `[wipe] reach` of a duct, or when the arm runs out. The brow wipe passes the
  forehead, not the eyes, which is why reach is generous; a real eye-wipe idle can be
  passed by the caller.
- **Settings** (`Settings.cpp`): one field table drives the toml reader, the user-file
  writer and the menu pages. `Lacrima.toml` holds the shipped defaults;
  `Lacrima_User.toml` holds only what the player changed (written by the menu).
  Defaults belong in the shipped toml, not in `Settings.h`.
- **Menu** (`Menu.cpp`): SKSE Menu Framework pages. The vendored header's wrappers do
  not null-check their `GetProcAddress` results, so every wrapper used is on
  `kFrameworkExports`, probed at `kDataLoaded`.
- **APIs**: `Lacrima.psc` (natives in `Papyrus.cpp`, `kAPIVersion`), C exports in
  `Api.cpp` / `include/API/LacrimaAPI.h` / `exports.def` (interface version packed
  MMmmpp). Never change the arguments of a shipped native or export; add a new one.
- **Console**: `LacrimaTest.psc` + `dist\SKSE\CustomConsole\Lacrima.yaml` (ConsoleUtil
  Extended, alias `tears`). CUE passes args by position, so YAML arg order must match
  the function's parameters, and a returned string must never contain `%` (CUE prints
  it as a printf format).
