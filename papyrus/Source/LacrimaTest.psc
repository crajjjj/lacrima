Scriptname LacrimaTest Hidden
{Console harness for Lacrima, exposed through ConsoleUtil Extended by
SKSE\CustomConsole\Lacrima.yaml: "tears cry", "tears status" and so on.
Commands act on the console-selected actor, or on the player when none is selected.
Returned strings print to the console; they must never contain a percent sign.}

Function NearbyNative(Float afRadius, Float afIntensity, Float afSeconds) global native
Function ShowDuctsNative(Bool abShow) global native
Function MountWorldNative(Bool abWorld) global native
Function WipeAllNative() global native
Int Function CryingCountNative() global native

Actor Function Target() global
	Actor target = Game.GetCurrentConsoleRef() as Actor
	If !target
		target = Game.GetPlayer()
	EndIf
	Return target
EndFunction

String Function Who() global
	If Game.GetCurrentConsoleRef() as Actor
		Return "The selected actor"
	EndIf
	Return "The player"
EndFunction

Bool Function Loaded() global
	Return Lacrima.GetAPIVersion() > 0
EndFunction

; Params in YAML order: intensity, seconds
String Function Cry(Float intensity, Float seconds) global
	If !Loaded()
		Return "Lacrima.dll is not loaded."
	EndIf
	If !Lacrima.IsEnabled()
		Return "Lacrima is off; turn it on with: tears enable 1"
	EndIf
	Lacrima.Cry(Target(), intensity, seconds)
	If seconds > 0.0
		Return Who() + " cries at intensity " + intensity + " for " + seconds + " s."
	EndIf
	Return Who() + " cries at intensity " + intensity + " until stopped."
EndFunction

; Params in YAML order: wipe
String Function Stop(Int wipe) global
	Lacrima.StopCrying(Target(), wipe != 0)
	If wipe != 0
		Return Who() + " stops crying; tears wiped."
	EndIf
	Return Who() + " stops crying; the tears on the face dry up."
EndFunction

; Params in YAML order: eye
String Function Tear(Int eye) global
	If !Loaded()
		Return "Lacrima.dll is not loaded."
	EndIf
	Lacrima.ShedTear(Target(), eye)
	Return Who() + " sheds a tear."
EndFunction

String Function Wipe() global
	Lacrima.WipeTears(Target())
	Return Who() + " has a dry face."
EndFunction

String Function WipeHand() global
	If !Loaded()
		Return "Lacrima.dll is not loaded."
	EndIf
	Lacrima.WipeTearsWithHand(Target())
	Return Who() + " wipes the tears away."
EndFunction

String Function WipeAll() global
	WipeAllNative()
	Return "Every face is dry."
EndFunction

String Function Status() global
	If !Loaded()
		Return "Lacrima.dll is not loaded."
	EndIf
	Return Lacrima.GetStatus(Target())
EndFunction

String Function Count() global
	Return CryingCountNative() + " actor(s) crying."
EndFunction

; Params in YAML order: intensity, seconds, radius
String Function Nearby(Float intensity, Float seconds, Float radius) global
	If !Loaded()
		Return "Lacrima.dll is not loaded."
	EndIf
	NearbyNative(radius, intensity, seconds)
	Return "Actors within " + radius + " units of the player start crying."
EndFunction

; Params in YAML order: show
String Function Ducts(Int show) global
	ShowDuctsNative(show != 0)
	If show != 0
		Return "Tear starts are marked on the selected actor, or on the player."
	EndIf
	Return "Tear start markers off."
EndFunction

; Params in YAML order: world
String Function Mount(Int world) global
	MountWorldNative(world != 0)
	If world != 0
		Return "Tears are now drawn in world space under the cell node (the 0.1.0 way)."
	EndIf
	Return "Tears are now drawn under the head bone."
EndFunction

; Params in YAML order: enabled
String Function Enable(Int enabled) global
	Lacrima.SetEnabled(enabled != 0)
	If enabled != 0
		Return "Lacrima is on."
	EndIf
	Return "Lacrima is off; every tear is gone."
EndFunction

String Function Reload() global
	Lacrima.ReloadConfig()
	Return "Lacrima.toml and Lacrima_User.toml read again."
EndFunction

; Params in YAML order: streams
String Function Streams(Int streams) global
	If !Loaded()
		Return "Lacrima.dll is not loaded."
	EndIf
	Lacrima.SetStreams(Target(), streams)
	If streams <= 0
		Return Who() + " uses the streams setting again."
	EndIf
	Return Who() + " sheds tears from " + streams + " point(s) per eye."
EndFunction

; Params in YAML order: strength
String Function Mascara(Float strength) global
	If !Loaded()
		Return "Lacrima.dll is not loaded."
	EndIf
	Lacrima.SetMascara(Target(), strength)
	If strength < 0.0
		Return Who() + " uses the mascara setting again."
	ElseIf strength == 0.0
		Return Who() + " has no running mascara."
	EndIf
	Return Who() + " has running mascara at " + strength + "."
EndFunction
