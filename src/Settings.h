#pragma once

#include <variant>

namespace Lacrima
{
	// Every tunable lives here. Load reads the shipped Lacrima.toml and then
	// Lacrima_User.toml on top; the SKSE menu edits the live values and Save writes
	// only the ones that differ from the shipped file into Lacrima_User.toml, so an
	// update never overwrites what a player tuned.
	//
	// The values below are fallbacks for a key the toml lacks. The defaults players
	// see are the ones in dist\SKSE\Plugins\Lacrima.toml.
	struct Settings
	{
		bool enabled{ true };
		bool debugLog{ false };

		float rate{ 12.0f };
		int   drop{ 4 };
		float well{ 0.9f };
		float hold{ 0.8f };
		float ductPosition{ 0.3f };
		float ductBelow{ 0.9f };
		int   streams{ 2 };
		float streamSpread{ 0.6f };
		bool  bothEyes{ true };
		int   maxActors{ 6 };
		int   maxParticles{ 160 };
		float maxDistance{ 3000.0f };

		float size{ 0.8f };
		float trail{ 1.2f };
		float opacity{ 0.3f };
		float shine{ 6.0f };
		float gloss{ 300.0f };
		float reflection{ 1.0f };
		float glow{ 0.06f };
		float eyeShine{ 1.6f };
		float lifetime{ 30.0f };
		float fade{ 5.0f };

		float stickiness{ 2.0f };
		float friction{ 30.0f };
		float viscosity{ 20.0f };
		float cohesion{ 2.5f };
		float stretch{ 3.0f };
		float trailStep{ 2.5f };
		float cling{ 2.0f };
		float fall{ 0.6f };

		bool  hands{ true };
		float absorb{ 0.15f };
		float reach{ 7.0f };
		float gestureWait{ 3.5f };

		bool showDucts{ false };
		bool mountWorld{ true };  // tears under the cell node (the proven 0.1.0 way); false hangs them under the head bone (cheaper)

		static std::unique_lock<std::recursive_mutex> Lock();
		static Settings&                              Get();
		// What the menu saves. The menu edits both this and Get(); a script or the
		// console (SetEnabled, tears ducts) edits only Get(), so it lasts one session.
		static Settings&                              Persisted();
		static Settings                               Snapshot();
		static const Settings&                        Defaults();

		static void Load();
		static bool Save();
		static void ResetToDefaults();
	};

	struct Field
	{
		using Member = std::variant<bool Settings::*, int Settings::*, float Settings::*>;

		const char* section;
		const char* key;
		Member      member;
		float       min;
		float       max;
		const char* page;
		const char* label;
		const char* format;
		const char* tip;
	};

	std::span<const Field> Fields();
}
