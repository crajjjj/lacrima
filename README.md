# Lacrima

Procedural tears for Skyrim SE/AE. Tears well up at the lower lid, cling, then run
down the face and follow its shape as the head moves and the expression changes. They
leave a wet trail that dries up, and a drop that reaches the chin clings for a moment,
then falls off. Optionally, each tear leaves a streak of running mascara behind.

Lacrima is a framework: it makes an actor cry when another mod, a script, the console
or its own settings menu asks it to. On its own it does nothing in game.

## Requirements

- SKSE and Address Library for SKSE Plugins
- Optional: [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) for the settings menu
- Optional: [ConsoleUtil Extended](https://www.nexusmods.com/skyrimspecialedition/mods/133569) for the `tears` console commands

## How it works

Lacrima reads the actor's face mesh, as the game has posed it this frame, expression
included, and turns the skin under the tears into a surface. Each eye gets a small
fluid simulation that lives in the head's own frame: drops are particles that hold
together, cling to the skin and slide down it, and because the frame moves with the
head they ride it without lag however fast the actor moves, while a tear at rest costs
nothing. The particles are drawn as beads, as wet trails, and as drops that fall off
the face. Actors far from the camera keep their tears paused until they come closer.

Tears start just below each eye, from one or more points along the lower lid
(`streams`), anywhere from the inner corner beside the nose to the outer corner, each a
little to one side of the last, so trails build up instead of retracing one line. A
tear wells on the lid, runs as a small bead and leaves a thin wet streak behind it; a
drop that reaches the underside of the chin clings there for a moment before it falls.
While an actor cries, their eyes glisten.

Running mascara is optional (`mascara`, off by default; other mods can turn it on per
actor or per cry). Each tear then carries pigment out of the lashes: a dark band builds
up under the lower lid and spreads over the skin under the eye (`mascara_spread`: a
band along the lid only, or the whole area under the eye), and a streak runs down the
exact path the tear took, as far as you let it (`mascara_length`: in cm from the eye, so
3, the default, stops it above the nose; 0 as far as the tear goes). Both
outlive the water and fade after a few minutes. A hand wiping the face smears them:
it drags them along the skin the way it moves, wider and flatter, and they go sooner.

Hands take tears away. A crying actor's own fingers and palms absorb the tears they
touch, in any animation that brings a hand to the face. `WipeTearsWithHand` plays a
wipe gesture (the vanilla brow wipe unless you pass another idle) and dries the face
once the hand comes near the eyes.

## Settings

With SKSE Menu Framework installed, the Lacrima pages (Tears, Look, Physics, Wipe, Test)
change everything live and save your changes when the menu closes. Without it, write
the values you want to change into `Data\SKSE\Plugins\Lacrima_User.toml`, using the
sections and keys of `Lacrima.toml`, which lists every setting with its default.

**Tears look white and solid?** Some ENB presets amplify materials that reflect or
give off a little light of their own, which the default water look does. Set Look
style to 1 (plain: no glow, no reflection), and lower Opacity if they still look
milky. Style 2 adds refraction, so the scene behind bends through the water.

**Tears start in the wrong place?** (By the ears, say.) Lacrima reads the eyes and
face meshes of the head to find the lower lids. Eye mods that build the eye from
several layers are handled: when the part the game lists as the eyes is not the
eyeballs, Lacrima measures the other layers and takes the one that is (`tears status`
says so). For a head read wrongly some other way, two settings on the Test page
correct it: Face forward (`face_forward`,
0 decides from the meshes, 1 or -1 forces which way the face looks; try the value
`tears status` does not show) and Eyes mesh space (`eyes_space`, try 1, then 2, if the
tears start far from the eyes). Both apply at once. `tears status` prints what was
read, where each eye was measured and which way the face was taken to look; please
paste that line in a report, with the head and eye mods you use. It is also written to
`Lacrima.log` when you run the command, and, with Detailed log on (Test page), by itself
whenever a crying starts.

Variety (Tears page) sets how much one tear differs from the next: where on the lid it
starts, how big it is, how far its mascara streak runs, and whether another follows
right behind. At 0 every tear runs the same way.

## Console

With ConsoleUtil Extended installed. Commands act on the actor selected in the console,
or on the player when none is selected.

| Command | What it does |
|---|---|
| `tears cry 0.7 30` | Cry at intensity 0.7 for 30 seconds (0 seconds = until stopped); `tears cry 0.7 30 3 1` sheds from 3 points per eye with running mascara |
| `tears stop` | Stop crying; `tears stop 1` also wipes the face |
| `tears tear` | Shed a single tear (`tears tear 0` left eye, `1` right, `2` both) |
| `tears wipe` / `tears wipeall` | Dry this face / every face, at once |
| `tears wipehand` | Wipe the tears away with a hand gesture |
| `tears nearby 0.7 30 1500` | Every loaded actor within 1500 units of the player cries |
| `tears status` | What Lacrima found on the head and what the tears are doing; the line is also written to `Lacrima.log` |
| `tears ducts 1` | Mark where tears start (`tears ducts 0` to hide) |
| `tears mount 1` | Draw the tears in world space instead of under the head bone (debug; a frame behind a turning head; `tears mount 0` is the default) |
| `tears mascara 1` | Running mascara on this actor (`0` none, `-1` = the setting) |
| `tears count` | How many actors are crying |
| `tears enable 0` | Turn Lacrima off for this session (`1` on) |
| `tears reload` | Read the toml files again |

## For mod authors

### Papyrus

```papyrus
If Lacrima.GetAPIVersion() > 0          ; 0: Lacrima is not installed
    Lacrima.Cry(akActor, 0.7, 20.0)     ; intensity 0.0-1.0, seconds (<= 0 = until stopped)
EndIf

Lacrima.StopCrying(akActor)             ; tears on the face dry up
Lacrima.StopCrying(akActor, true)       ; ... or vanish at once
Lacrima.ShedTear(akActor)               ; one tear, also when not crying
Lacrima.WipeTears(akActor)                    ; at once
Lacrima.WipeTearsWithHand(akActor)            ; with the brow-wipe gesture
Lacrima.WipeTearsWithHand(akActor, myEyeWipeIdle)
Bool  crying = Lacrima.IsCrying(akActor)
Float left   = Lacrima.GetTimeLeft(akActor)   ; -1 = until stopped

If Lacrima.GetAPIVersion() >= 2
    Lacrima.CryEx(akActor, 0.7, 20.0, 3, 1.0) ; cry from 3 points per eye, with full running mascara
    Lacrima.CryEx(akActor, 0.7, 20.0, 3)      ; the same, mascara left to the actor or the player
    Lacrima.SetMascara(akActor, 1.0)          ; this actor wears mascara: every cry shows it
    Lacrima.SetMascara(akActor, -1.0)         ; forget that: the player's setting again
EndIf
```

- `Cry` on an actor who already cries replaces the intensity and the time left.
- `CryEx` (API version 2) is `Cry` with two optional arguments, which hold until the
  actor's next `Cry` or `CryEx`:
  - streams: how many points per eye the tears start from, 1 to 4. 0, the default,
    uses the player's setting.
  - mascara: running mascara for this crying, from 0.0 (none) to 1.0 (full). -1.0,
    the default, means "not decided here": the actor's `SetMascara` value applies, or
    the player's setting if the actor has none. Plain `Cry` works that way.
- `SetMascara` (API version 2) says how much running mascara an actor wears, 0.0 to
  1.0; every cry of that actor shows it, unless a `CryEx` call passes its own value.
  -1.0 clears it, so the player's setting applies again.
- Which mascara a cry gets: the `CryEx` value if it gave one, else the actor's
  `SetMascara` value, else the player's setting (off unless they turned it on).
- `WipeTears` also takes off running mascara; `WipeTearsWithHand` smears it.
- Crying and `SetMascara` do not survive loading a save, and no `Lacrima_CryStop` is
  sent then: forget the actors you track on load, or ask `IsCrying` again.
- Calling a global function of a script that is not installed only logs an error, so
  the `GetAPIVersion` check is all an optional integration needs; check for 2 before
  calling `CryEx` or `SetMascara`.

Lacrima sends `Lacrima_CryStart` (numArg = intensity) and `Lacrima_CryStop` mod events,
with the actor as the sender, so an expression mod can react to crying it did not start:

```papyrus
RegisterForModEvent("Lacrima_CryStart", "OnCryStart")

Event OnCryStart(String asEventName, String asUnused, Float afIntensity, Form akSender)
    Actor crying = akSender as Actor
EndEvent
```

### SKSE plugins

`include/API/LacrimaAPI.h` declares the same calls as C exports of `Lacrima.dll`.
Resolve them at run time with `GetModuleHandleA("Lacrima.dll")` and `GetProcAddress`;
there is nothing to link. Every call is safe from any thread. `Lacrima_CryEx` and
`Lacrima_SetMascara` arrived in 0.2.0 (interface version 10100): null-check the
pointer, or gate on `Lacrima_GetInterfaceVersion()`.

## Add-ons

Two small optional add-ons make Lacrima react to SexLab on its own: an actor SexLab
marks as a victim cries while being penetrated, and keeps crying, more quietly, for a
while after it stops or the scene ends. One is for SexLab P+, which on 2.19 or newer
follows P+'s live penetration detection; the other is for classic SexLab, which goes by
the animation's tags. They are separate downloads on the mod page.

## Building

```
git clone --recurse-submodules https://github.com/crajjjj/lacrima.git
xmake f -m release
xmake                  # Lacrima.dll -> dist\SKSE\Plugins
xmake build papyrus    # scripts -> dist\Scripts, and Release\Lacrima-<version>.zip
python tools/make_tears.py   # regenerates the tear meshes and textures in dist
```

Needs Visual Studio 2022 and xmake 3.0 or newer. `PYRO_EXE` and `SKYRIM_GAME_PATH`
override where Pyro and the game are found.

## Credits

- The fluid simulation, the skin wrap and the mesh generators come from
  [OStrands](https://gitlab.com/LilRicefield/ostrands) by LeonVanWinkle (Saint),
  GPL-3.0. See NOTICE.txt.
- Lacrima by crajjjj.

## License

GNU General Public License v3.0; see LICENSE.
