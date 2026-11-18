#pragma once

// Mod events Lacrima listens for, so a mod can make an actor cry without compiling
// against Lacrima.psc or checking that Lacrima is installed. The actor is the sender.
//
//   Lacrima_Cry          numArg = intensity 0 to 1 (0 or less: 0.6)
//                        strArg = "seconds streams", each optional, separated by a
//                        space or a comma: seconds (none given: 20; 0: until
//                        stopped), streams (0: the setting)
//   Lacrima_StopCrying   numArg not 0: the tears are wiped off at once
//   Lacrima_SetMascara   numArg = running mascara for this actor, 0 none to 1 full;
//                        negative: the player's setting again
//
// The same as Lacrima.CryEx (without its mascara), StopCrying and SetMascara.
namespace Lacrima::Events
{
	// Registers the sink. Call once at kDataLoaded.
	void Install();
}
