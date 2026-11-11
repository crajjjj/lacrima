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
		float variety{ 0.5f };
		bool  bothEyes{ true };
		int   maxActors{ 6 };
		int   maxParticles{ 160 };
		float maxDistance{ 3000.0f };

		int   style{ 0 };  // 0 water, 1 plain (no glow, no reflection), 2 refraction
		float refraction{ 0.3f };
		float size{ 0.55f };
		float trail{ 0.6f };
		float opacity{ 0.05f };
		float shine{ 1.2f };
		float gloss{ 400.0f };
		float reflection{ 0.5f };
		float glow{ 0.0f };
		bool  envLightFade{ true };  // the water's reflection dims with the light on the face (SLSF2 EnvMap_Light_Fade), so tears do not shine in the dark
		float eyeShine{ 1.6f };
		float lifetime{ 30.0f };
		float fade{ 5.0f };
		float mascara{ 0.0f };
		float mascaraOpacity{ 0.5f };
		float mascaraWidth{ 1.0f };
		float mascaraSmudge{ 1.0f };
		float mascaraSpread{ 0.6f };
		float mascaraLength{ 3.0f };  // cm down the face a streak runs at most; 0 as far as the tear goes
		float mascaraLifetime{ 300.0f };

		float stickiness{ 1.5f };
		float friction{ 30.0f };
		float viscosity{ 20.0f };
		float cohesion{ 2.5f };
		float stretch{ 3.0f };
		float trailStep{ 2.5f };
		float cling{ 1.0f };
		float fall{ 0.6f };

		bool  hands{ true };
		float absorb{ 0.15f };
		float reach{ 7.0f };
		float gestureWait{ 3.5f };

		bool showDucts{ false };
		bool mountWorld{ false };  // false: tears under the head bone, carried by the engine in the same frame; true: under the cell node in world space (a frame behind a turning head)
		int  faceForward{ 0 };     // [debug] face_forward: 0 from the meshes, 1 / -1 forced
		int  eyesSpace{ 0 };       // [debug] eyes_space: 0 the eyes mesh's own skin transform, 1 the face's, 2 none

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
