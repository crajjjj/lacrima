#pragma once

// Crying from a whipping: the one thing Lacrima starts by itself. A hit-event sink
// counts the hits an actor takes from a whip (a weapon or spell picked by name, by
// form or by keyword, `[whip] names` / `forms`); from the `[whip] hits`-th hit on,
// each one makes the actor cry for `[whip] seconds`. A pause of `[whip] window`
// seconds forgets the count.
namespace Lacrima::Whip
{
	// Registers the hit sink. Call once at kDataLoaded.
	void Install();

	// Takes the name and form lists from Settings and looks the forms up. Call at
	// kDataLoaded and after every settings reload.
	void Load();

	// Counts a_hits whip hits on the actor as if a whip had landed them, whatever
	// the enabled / player / npcs switches say (the console test). Returns what
	// happened, without a percent sign.
	std::string Test(RE::Actor* a_actor, int a_hits);

	// Forgets every count. Session reset.
	void Reset();
}
