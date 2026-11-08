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

	// Tears can start from several points along the lower lid of one eye.
	constexpr std::size_t kMaxStreams = 4;
	using Ducts = std::array<std::array<Duct, kMaxStreams>, kEyes>;

	// A box in world units: where the fluid is this frame.
	struct Region
	{
		RE::NiPoint3 low;
		RE::NiPoint3 high;
	};

	struct View
	{
		std::span<const Patch> patches;
		Ducts                  ducts;  // [eye][stream]; streams past the configured count are invalid
		float                  scale{ 1.0f };
		// The skin in the patches moved this frame under an expression (facegen
		// morphs), so fluid resting on it must be stirred.
		bool morphing{ false };
	};

	// Call once per frame before Of().
	void BeginFrame(float a_delta);

	// Where the ducts go: a_streams points per eye (1 to kMaxStreams) centred on
	// a_position along the lower lid (0 inner corner, 1 outer), spread over a_spread
	// of that range, a_below eye radii under the eye centre.
	struct Placement
	{
		float position{ 0.3f };
		float below{ 0.9f };
		int   streams{ 1 };
		float spread{ 0.6f };

		bool operator==(const Placement&) const = default;
	};

	// The actor's face, posed for this frame; nullopt when the actor has no loaded
	// face or eyes that Lacrima can read. Only the skin that can fall inside
	// a_region, plus the skin around the ducts, is posed; null poses the ducts'
	// surroundings only. Main thread only.
	std::optional<View> Of(RE::Actor* a_actor, const Placement& a_placement, const Region* a_region);

	// One line about what was found on the actor's head, for the status command.
	std::string Describe(RE::Actor* a_actor, const Placement& a_placement);

	// The name of the bone both head parts are measured against.
	constexpr const char* kHeadBone = "NPC Head [Head]";

	void Reset();
}
