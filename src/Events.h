#pragma once

// Mod events Lacrima listens for, so a mod can make an actor cry without compiling
// against Lacrima.psc or checking that Lacrima is installed. The actor is the sender.
//
//   Lacrima_Cry          numArg = intensity 0 to 1 (0 or less: 0.6)
//                        strArg = "seconds streams mascara", each optional, separated
//                        by spaces or commas: seconds (none given: 20; 0: until
//                        stopped), streams (0: the setting), mascara (-1: not set)
//   Lacrima_StopCrying   numArg not 0: the tears are wiped off at once
//
// The same as Lacrima.CryEx and Lacrima.StopCrying.
namespace Lacrima::Events
{
	// Registers the sink. Call once at kDataLoaded.
	void Install();
}
