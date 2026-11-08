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
  (facegen dynamic data when present, so expressions move the surface), dedupes them
  on a 0.25-unit voxel and skins them every frame into `Patch`es with velocities. Eye
  centers come from the eyes mesh split by the mesh-space midline; mesh space has Z up
  and the face looking along +Y or -Y (decided from where the eyes sit). Each duct is
  the face vertex nearest a point below and in front of the eye, so it follows the
  skin from then on.
- **Tears** (`Tears.cpp`): one `Crier` per actor, one `Cheek` per eye. A cheek is a
  fluid domain in meters (70 units per meter) with its origin 400 units below the
  duct, recentred as the actor moves. A tear is a run of linked particles emitted at
  the duct and anchored there while it wells, then released. `Fluid::Solver` steps at
  120 Hz against the face patches near the particles. Particles that fall free get
  their life cut to `[physics] fall`. Drawing is OStrands' blob / rope / drop pass.
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
