#pragma once

// Crying and run mascara as faction membership, so anything that takes conditions
// (dialogue, packages, animation replacers) can tell without calling Lacrima. The two
// factions come from the optional Lacrima.esp; without it this module only sends the
// mascara mod events.
//
//   LacrimaCryingFaction   while the actor cries; rank 0 to 100 = the intensity asked for
//   LacrimaMascaraFaction  while run mascara shows on the face; rank 0 to 100 = how much
//
// Faction membership is written into the save, crying is not: the actors Lacrima put
// into a faction are kept in the SKSE co-save and taken out again when the save loads.
namespace Lacrima::Factions
{
	struct Member
	{
		RE::FormID  actor{ 0 };
		std::int8_t crying{ -1 };   // rank, or -1 when not crying
		std::int8_t mascara{ -1 };  // rank, or -1 when the face is clean
	};

	// Looks the factions up. Call at kDataLoaded.
	void Load();

	// Registers the co-save callbacks. Call from SKSEPluginLoad.
	void RegisterSerialization();

	// Everyone who cries or shows mascara right now; whoever is missing leaves the
	// factions. Main thread. a_events false: no mascara mod events (a session reset).
	void Sync(std::span<const Member> a_members, bool a_events = true);
}
