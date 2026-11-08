#pragma once

namespace Lacrima::Face
{
	// A point on the skin of the face, posed this frame, in world units.
	struct Patch
	{
		RE::NiPoint3 position;
		RE::NiPoint3 normal;
	};

	// Where one eye sheds its tears: a point on the skin just below the lower lid,
	// the skin's outward normal there, and "down the face" (gravity along the skin).
	struct Duct
	{
		RE::NiPoint3 point;
		RE::NiPoint3 normal;
		RE::NiPoint3 down;
		bool         valid{ false };
	};

	enum Eye : std::size_t
	{
		kLeft,
		kRight,
		kEyes
	};

	// A box in world units: where the fluid is this frame.
	struct Region
	{
		RE::NiPoint3 low;
		RE::NiPoint3 high;
	};

	struct View
	{
		std::span<const Patch>  patches;
		std::array<Duct, kEyes> ducts;
		float                   scale{ 1.0f };
		// The skin in the patches moved this frame under an expression (facegen
		// morphs), so fluid resting on it must be stirred.
		bool morphing{ false };
	};

	// Call once per frame before Of().
	void BeginFrame(float a_delta);

	// The actor's face, posed for this frame; nullopt when the actor has no loaded
	// face or eyes that Lacrima can read. Only the skin that can fall inside
	// a_region, plus the skin around the ducts, is posed; null poses the ducts'
	// surroundings only. Main thread only.
	std::optional<View> Of(RE::Actor* a_actor, float a_ductPosition, float a_ductBelow, const Region* a_region);

	// One line about what was found on the actor's head, for the status command.
	std::string Describe(RE::Actor* a_actor, float a_ductPosition, float a_ductBelow);

	// The name of the bone both head parts are measured against.
	constexpr const char* kHeadBone = "NPC Head [Head]";

	void Reset();
}
