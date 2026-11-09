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
  face's on a 0.25-unit voxel. **Head parts do not share a mesh space**: a vanilla
  eyes part is authored around the head bone (eye centres at z 3), a face part around
  the skeleton root (eyes at z 124). Both are taken into head-bone space through the
  skin-to-bone transform each skin holds for `NPC Head [Head]` (`ToHeadBone`) before
  anything is measured; comparing the raw spaces put every duct at the bottom of the
  neck. The eyes only place the ducts (centres from the eyes mesh split by the
  midline, forward +Y or -Y from where the eyes sit); the eyeball is not a collision
  surface, it sits a millimetre behind the lid skin and would steal the nearest-patch
  pick. `Placement` gives `streams` (1 to 4) start points per eye spread along the
  lower lid; each duct is the face vertex nearest its target, mapped back into face
  space. Per frame `Of(actor, placement, region, watch)` skins only the points whose
  bind position can land in `region` (the box is taken into each joint's skin space
  and tested on the point's heaviest joint) plus the points within 2.5 units of a
  duct, and reports `morphing` when an expression moved a point inside `watch` (the
  tears themselves, tight, tested in bind space without posing), so a blink does not
  stir a trail lower on the cheek. `Glisten` (wet eyes, `[look] eye_shine`) first
  gives the actor's eyes their own copy of the eye material (copy, then
  `SetMaterial(copy, true)`; the game shares one material between every actor with
  the same eyes), scales its reflection and highlight, and lapses half a second after
  the last call.
- **Tears** (`Tears.cpp`): one `Crier` per actor, one `Cheek` per eye. Everything is
  simulated and drawn in **head space**: the head bone's axes and origin, in metres
  (`Frame`: 70 x the bone's scale world units per metre; world down and `9.81 / scale`
  are rotated into it each frame). The skin stands still there, so a tear at rest
  **sleeps** (the solver's sleep, which a world-space fluid on a moving body never
  reaches) and costs nothing until `Wake` stirs it: an expression moved the skin under
  it (`View::morphing`), the head tilted 15 degrees from where the tears settled, a
  hand came within reach, or an anchor / moving neighbour touched it. `Wake` runs
  before the face is final, so woken tears get skin under them in the same step. No
  recentring, no speed caps to outrun, no cell clears. A tear is a run of linked
  particles born touching the lid at one of its eye's streams, each anchored to the
  seat while it wells, then released. Adhesion per particle falls with the fluid
  around it, so a bead runs while a lone drop sticks; a running tear leaves its
  hindmost drop asleep every `[physics] trail_step` (`Deposit`), and the trail rope is
  drawn from the lid through those deposits to what is left of the bead on the skin.
  On skin that faces down a drop clings for `[physics] cling` seconds (and stays
  awake meanwhile) before it falls. `Fluid::Solver` steps at 120 Hz against the face
  patches near the awake particles, with one neighbour list per step (2.5 spacings of
  slack) on a sorted flat `CellGrid`; only patches a particle is in front of collide
  (the inner eyelid sits a millimetre behind the skin), and the stick-pull keeps
  reaching a particle for 0.15 s after it loses the skin. A particle off the skin for
  0.2 s that moves down fast gets its life cut to `[physics] fall`. An actor beyond
  `[tears] max_distance` from the camera, or whose 3D is app-culled (the player in
  first person), is **frozen**: no emission, no solver, no draw, its tears hidden;
  trail life still ticks.
- **Mounts.** The shipped default (`[debug] mount_world = true`, `tears mount 1`) hangs
  each cheek's tear root under the cell node with the inverse of its transform and
  poses the nodes in world units, as 0.1.0 did; it is the mount seen working in game.
  `tears mount 0` hangs the root under the head bone with identity transforms, so the
  engine carries the tears and a cheek whose tears all sleep skips the draw
  (`Cheek::dirty`); a mount switch re-poses every bead. Drawing is OStrands' blob /
  rope / drop pass on persistent scratch buffers. The tear meshes use a plain
  `NiSkinInstance`: the engine hides a dismember partition whenever an equipped item
  covers its slot (TNG uses slot 52). The material is an environment-mapped,
  translucent lighting shader without depth write, with a faint glow of its own
  (`[look] glow`) so a streak reads as a light wet line from any angle. `[look] style`
  switches it live in `Shade`: 0 water (that), 1 plain (no own-emit flag, emissive 0,
  envMapScale 0: some ENB presets turn glow or reflection into solid white, a player
  report), 2 refraction (plain + the `kRefraction` flag and `refractionPower`,
  untested on the envmap shader type). Changing the style flips shader flags, so
  `Shade` rebuilds the passes (`SetupGeometry` / `FinishSetupGeometry`), and does so
  for every new or recycled mesh too, since a clone may carry another style's flags.
- **Variety** (`[tears] variety`, in `StartTear` and at the end of `Emit`): each tear
  starts up to 6 mm (at 1) to one side of its duct along the lid (`startOffset`, kept on
  its particles and streak points as `start` so the trail and the mascara streak start
  where it did, `LidPoint`), varies more in size with an occasional big one, and now
  and then another follows right behind.
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
- **Running mascara** (`Cheek::stains`, `LayStain`, `DrawStains`): when the actor's
  mascara strength is above 0, every drop a running tear leaves behind (`Deposit`) also
  lays a `Stain` point, taken down onto the skin (`OntoSkin`), and a bead that dries on
  the skin ends its streak (`kBeadOrder`). Stains have no physics: they sit in head
  space, age for `[look] mascara_lifetime` and thin out over the last 30% of it. Each
  run is drawn as one rope from the lid of its stream through its points in order to
  the bead while it still runs, using `meshes\Lacrima\tear_stain.nif`: the trail rope's
  geometry and RopeNN bones with a matte dark material and its own pool
  (`stainRopes`, `_spareStains`, at most 16 streaks and 160 points per cheek, oldest
  run dropped first). A hand capsule touching a streak, or a hand-wipe gesture
  (`DryFace`), smears it (wider, at most 35% of its lifetime left); `Clear` (WipeTears,
  StopCrying with wipe) removes it. An actor with streaks stays a crier (busy, not
  idle) until they fade. The streak mesh is optional: without it tears still show.
- **Per call, then per actor, then the setting.** `CryEx` (and `Cry`, with 0 / -1)
  carries a stream count and a mascara strength in the `kCry` request; `StartCrying`
  keeps them as `Crier::callStreams` / `Crier::callMascara` until the next cry, and
  `PlacementOf(settings, crier.streams)` gives Face the count. Mascara also has a
  per-actor session override, `SetMascara` (`Tears::_overrides`, the `kMascara`
  request), since whether an actor wears makeup outlives any one cry. Each frame
  `Crier::mascara` = the call's if not negative, else the actor's, else the setting;
  overrides clear on load with everything else.
- **Settings** (`Settings.cpp`): one field table drives the toml reader, the user-file
  writer and the menu pages. `Lacrima.toml` holds the shipped defaults;
  `Lacrima_User.toml` holds only what the player changed (written by the menu).
  Defaults belong in the shipped toml, not in `Settings.h`.
- **Menu** (`Menu.cpp`): SKSE Menu Framework pages. The vendored header's wrappers do
  not null-check their `GetProcAddress` results, so every wrapper used is on
  `kFrameworkExports`, probed at `kDataLoaded`.
- **APIs**: `Lacrima.psc` (natives in `Papyrus.cpp`, `kAPIVersion`: 2 added
  `CryEx` / `SetMascara`), C exports in `Api.cpp` / `include/API/LacrimaAPI.h` /
  `exports.def` (interface version packed MMmmpp: 10100 added the same two). Never
  change the arguments of a shipped native or export; add a new one.
- **Console**: `LacrimaTest.psc` + `dist\SKSE\CustomConsole\Lacrima.yaml` (ConsoleUtil
  Extended, alias `tears`). CUE passes args by position, so YAML arg order must match
  the function's parameters, and a returned string must never contain `%` (CUE prints
  it as a printf format).
