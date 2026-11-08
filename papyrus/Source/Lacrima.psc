Scriptname Lacrima Hidden
{Procedural tears: fluid that wells up at the eyes and runs down the face.

Every function is global: Lacrima.Cry(akActor, 0.7, 20.0).

Lacrima sends two mod events with the crying actor as the sender:
  Lacrima_CryStart   numArg = intensity
  Lacrima_CryStop
Register with RegisterForModEvent("Lacrima_CryStart", "OnCryStart") and handle
  Event OnCryStart(String asEventName, String asUnused, Float afIntensity, Form akSender)

Crying does not survive loading a save, and no Lacrima_CryStop is sent then. A mod
that tracks crying actors should forget them on load, or ask IsCrying again.}

; Bumped whenever a function is added. 0 means Lacrima.dll did not load.
Int Function GetAPIVersion() global native

; Makes akActor cry. afIntensity is 0.0 to 1.0: how often and how heavily the eyes
; shed; 0.0 stops crying. afSeconds <= 0 cries until StopCrying. Calling Cry on an
; actor who already cries replaces the intensity and the time left. The actor needs
; loaded 3D; one who has none is ignored.
Function Cry(Actor akActor, Float afIntensity = 0.6, Float afSeconds = 20.0) global native

; Stops new tears. The tears already on the face run their course and dry up,
; unless abWipe is true, which removes them at once.
Function StopCrying(Actor akActor, Bool abWipe = false) global native

; Sheds a single tear, also from an actor who is not crying.
; aiEye: 0 left, 1 right, 2 both, -1 one at random.
Function ShedTear(Actor akActor, Int aiEye = -1) global native

; Removes the tears on the face at once, without changing whether the actor cries.
Function WipeTears(Actor akActor) global native

; Wipes the tears away by hand: plays akIdle (the vanilla brow wipe when None) and
; dries the face when a hand comes near the eyes, or after a few seconds if it never
; does. An idle that cannot play (the actor is busy, in furniture, in a scene) dries
; the face at once. Crying goes on; the next tear just takes a moment longer.
; Pass your own Idle for an animation that wipes the eyes properly.
Function WipeTearsWithHand(Actor akActor, Idle akIdle = None) global native

Bool Function IsCrying(Actor akActor) global native

; The intensity Cry set; 0.0 when not crying.
Float Function GetIntensity(Actor akActor) global native

; Seconds of crying left: -1.0 = until StopCrying, 0.0 = not crying.
Float Function GetTimeLeft(Actor akActor) global native

Bool Function IsEnabled() global native

; Turns tears on or off. Off removes every tear at once and ignores new requests.
; Lasts for this session only; the checkbox in the SKSE menu is the saved setting.
Function SetEnabled(Bool abEnabled) global native

; Reads Lacrima.toml and Lacrima_User.toml again.
Function ReloadConfig() global native

; One line about the actor's tears and what Lacrima found on their head, for
; debugging. The first call for an actor may answer that it is still reading the
; actor; ask again a moment later.
String Function GetStatus(Actor akActor) global native
