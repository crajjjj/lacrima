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
	// ... and from both corners of the eye: two more ducts after the streams', the
	// inner corner's at kInnerCorner and the outer corner's at kOuterCorner.
	constexpr std::size_t kInnerCorner = kMaxStreams;
	constexpr std::size_t kOuterCorner = kMaxStreams + 1;
	constexpr std::size_t kMaxDucts = kMaxStreams + 2;
	using Ducts = std::array<std::array<Duct, kMaxDucts>, kEyes>;

	// The skin under the lower lid, inner corner to outer along the curve of the eye,
	// in rows from just under the lashes downward: where running mascara gathers. The
	// same shape as a duct.
	constexpr std::size_t kLidPoints = 8;
	constexpr std::size_t kLidRows = 3;
	using Lids = std::array<std::array<std::array<Duct, kLidPoints>, kLidRows>, kEyes>;

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
		// The skin inside the watch region moved this frame under an expression
		// (facegen morphs), so fluid resting on it must be stirred.
		bool morphing{ false };
		Lids lids;  // [eye][row down from the lashes][point along the lid]
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
		// For a head Lacrima reads wrongly ([debug] face_forward, eyes_space): which way
		// the face looks in head-bone space (0 decides from the meshes, else +1 / -1
		// along y), and which transform takes the eyes mesh into head-bone space (0 its
		// own skin's, 1 the face mesh's, 2 none). A change rebuilds the head.
		int forward{ 0 };
		int eyesSpace{ 0 };

		bool operator==(const Placement&) const = default;
	};

	// The actor's face, posed for this frame; nullopt when the actor has no loaded
	// face or eyes that Lacrima can read. Only the skin that can fall inside
	// a_region, plus the skin around the ducts, is posed; null poses the ducts'
	// surroundings only. View::morphing reports an expression moving the skin
	// inside a_watch (the tears themselves, tight), which needs no posing; null
	// watches nothing. Main thread only.
	std::optional<View> Of(RE::Actor* a_actor, const Placement& a_placement, const Region* a_region, const Region* a_watch = nullptr);

	// One line about what was found on the actor's head, for the status command.
	std::string Describe(RE::Actor* a_actor, const Placement& a_placement);

	// Wet eyes: scales the eyes' reflection and highlight by a_factor (1 restores
	// them). Call every frame while it should hold; it lapses on its own half a
	// second after the last call, and the original values are put back. The first
	// call gives the actor's eyes their own copy of the eye material, which the
	// game otherwise shares between every actor wearing the same eyes.
	void Glisten(RE::Actor* a_actor, float a_factor);

	// The name of the bone both head parts are measured against.
	constexpr const char* kHeadBone = "NPC Head [Head]";

	void Reset();
}
