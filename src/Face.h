#pragma once

namespace Lacrima::Face
{
	// A point on the skin of the face (or the eyeball), posed this frame, in world
	// units. velocity is how fast that bit of skin moves, so fluid resting on it can
	// be carried along.
	struct Patch
	{
		RE::NiPoint3 position;
		RE::NiPoint3 normal;
		RE::NiPoint3 velocity;
	};

	// Where one eye sheds its tears: a point on the skin just below the lower lid,
	// the skin's outward normal there, and "down the face" (gravity along the skin).
	struct Duct
	{
		RE::NiPoint3 point;
		RE::NiPoint3 normal;
		RE::NiPoint3 down;
		float        eyeRadius{ 0.0f };
		bool         valid{ false };
	};

	enum Eye : std::size_t
	{
		kLeft,
		kRight,
		kEyes
	};

	struct View
	{
		std::span<const Patch>  patches;
		std::array<Duct, kEyes> ducts;
		float                   scale{ 1.0f };
	};

	// Call once per frame before Of().
	void BeginFrame(float a_delta);

	// The actor's face, posed for this frame; nullopt when the actor has no loaded
	// face or eyes that Lacrima can read. Main thread only.
	std::optional<View> Of(RE::Actor* a_actor, float a_ductPosition, float a_ductBelow);

	// One line about what was found on the actor's head, for the status command.
	std::string Describe(RE::Actor* a_actor, float a_ductPosition, float a_ductBelow);

	void Reset();
}
