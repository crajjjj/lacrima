#pragma once

#include "Face.h"
#include "GeometryMath.h"
#include "Settings.h"
#include "fluid/Clusters.h"
#include "fluid/Solver.h"
#include "fluid/Surface.h"

namespace Lacrima
{
	// Owns every crying actor. Requests (Cry, Stop, Shed, Wipe) and queries are safe
	// from any thread: requests queue up and run at the start of the next frame on
	// the main thread, queries read a snapshot. Everything else runs on the main
	// thread, from the frame driver in Hooks.cpp.
	class Tears
	{
	public:
		static Tears& Get();

		struct State
		{
			bool  crying{ false };
			float intensity{ 0.0f };
			float timeLeft{ 0.0f };
		};

		// a_streams: how many points along the lower lid this crying sheds from, 1 to 4;
		// 0 uses the setting. a_mascara: running mascara for this crying, 0 to 1;
		// negative uses the actor's SetMascara, else the setting. Both last until the
		// next Cry for the actor.
		void Cry(RE::ActorHandle a_actor, float a_intensity, float a_seconds, int a_streams = 0, float a_mascara = -1.0f);
		void Stop(RE::ActorHandle a_actor, bool a_wipe);
		void Shed(RE::ActorHandle a_actor, int a_eye);
		void Wipe(RE::ActorHandle a_actor);
		// Plays a_idle (the vanilla brow wipe when null) and dries the face when a hand
		// reaches the eyes, or when the gesture runs out of time.
		void WipeWithHand(RE::ActorHandle a_actor, RE::TESIdleForm* a_idle);
		void WipeAll();
		void CryNearby(float a_radius, float a_intensity, float a_seconds);
		// Per-actor override, for this session: how much running mascara (negative goes
		// back to the setting).
		void SetMascara(RE::ActorHandle a_actor, float a_strength);

		State       Query(RE::ActorHandle a_actor) const;
		// a_log also writes the line to Lacrima.log: the console command does, the
		// menu, which asks every frame, does not.
		std::string Status(RE::ActorHandle a_actor, bool a_log = false);
		std::size_t CryingCount() const;

		void Update(float a_delta, bool a_paused);
		void Reset();

		static constexpr std::size_t kRopeBones = 16;

	private:
		struct Request
		{
			enum class Kind
			{
				kCry,
				kStop,
				kShed,
				kWipe,
				kWipeAll,
				kNearby,
				kHandWipe,
				kMascara
			};

			Kind             kind{ Kind::kCry };
			RE::ActorHandle  actor;
			float            intensity{ 0.0f };
			float            seconds{ 0.0f };
			int              eye{ -1 };
			bool             wipe{ false };
			float            radius{ 0.0f };
			RE::TESIdleForm* idle{ nullptr };
			int              streams{ 0 };      // kCry: 0 = the setting
			float            mascara{ -1.0f };  // kCry: negative = the actor's SetMascara, else the setting
		};

		struct Blob
		{
			RE::NiPointer<RE::NiNode>        root;
			std::vector<RE::NiAVObject*>     bones;
			std::vector<Fluid::SurfacePoint> surface;
			// Set when the mesh still carries a dismember skin (meshes from 0.1.0),
			// whose partitions the game hides by body-part slot under an actor.
			RE::NiPointer<RE::BSDismemberSkinInstance> dismember;
			std::size_t                                seed{ 0 };
			bool                                       live{ false };
			bool                                       used{ false };
		};

		struct Rope
		{
			RE::NiPointer<RE::NiNode>                   root;
			std::array<RE::NiAVObject*, kRopeBones>     bones{};
			std::array<GeometryMath::Frame, kRopeBones> frames{};
			RE::NiPointer<RE::BSDismemberSkinInstance>  dismember;
			bool                                        used{ false };
		};

		// The head bone's placement this frame. Tears are simulated and drawn in head
		// space: the head bone's axes, its origin, metres. The skin stands still
		// there (bar expressions), the tear nodes hang under the head bone so the
		// engine carries them between updates, and nothing has to chase a moving
		// head. world = origin + rotate * (local * unit).
		struct Frame
		{
			RE::NiPoint3  origin;
			RE::NiMatrix3 rotate;                    // head -> world
			RE::NiMatrix3 inverse;                   // world -> head
			float         scale{ 1.0f };             // the head bone's world scale
			float         unit{ 70.0f };             // world units per simulation metre
			Fluid::Vec3   down{ 0.0f, 0.0f, -1.0f }; // world down, in head space
		};

		// A tear duct in head space.
		struct Spot
		{
			Fluid::Vec3 point;
			Fluid::Vec3 normal;
			Fluid::Vec3 down;
		};

		// One eye's ducts, one per stream; a stream whose duct was not found is empty.
		using Spots = std::array<std::optional<Spot>, Face::kMaxStreams>;
		// The skin under one eye in head space: rows down from the lashes, each inner
		// corner to outer.
		using LidSpots = std::array<std::array<std::optional<Spot>, Face::kLidPoints>, Face::kLidRows>;

		// Where the tear nodes hang and what their transforms are expressed in: under
		// the head bone in head-bone units (the default, `tears mount 0`), or under the
		// cell node in world units as 0.1.0 did ([debug] mount_world, `tears mount 1`).
		// Under the head bone the engine carries the tears with the head in the same
		// frame, and a cheek whose tears all sleep skips Draw. The world mount poses
		// from the head transform Lacrima read before the engine moved the skeleton, so
		// it runs a frame behind a turning head: flat mascara marks dipped under the
		// skin on those frames and flickered. It stays as an A/B.
		struct Mount
		{
			bool         world{ false };
			const Frame* frame{ nullptr };

			RE::NiPoint3 Point(Fluid::Vec3 a_point) const;
			Fluid::Vec3  Direction(Fluid::Vec3 a_direction) const;
			float        Radius(float a_radius) const;
		};

		struct Droplet
		{
			Fluid::Vec3                center;
			float                      radius;
			Fluid::Vec3                velocity;
			std::optional<Fluid::Vec3> contact;
		};

		// Running mascara: a point of the dark streak a tear leaves on the skin, in head
		// space, laid where the tear left a drop of its trail (and every kStainSpacing
		// along the skin between drops), drawn as a flat ribbon lying just above the
		// skin. It outlives the water and fades; a hand drags it along the skin (a
		// smear: wider, gone sooner).
		struct Stain
		{
			Fluid::Vec3   position;
			Fluid::Vec3   normal{ 0.0f, 0.0f, 1.0f };  // the skin's, where it lies
			float         life{ 0.0f };      // seconds left
			float         lifetime{ 1.0f };  // seconds it had when laid
			float         strength{ 0.0f };  // pigment, 0 to 1
			std::uint32_t run{ 0 };
			std::uint32_t order{ 0 };        // its place along the tear's path
			std::uint8_t  stream{ 0 };
			float         start{ 0.0f };     // where along the lid the tear started, metres from the duct
			float         along{ 0.0f };     // metres down the streak from the lid, for [look] mascara_length
			float         reach{ 1.0f };     // the tear's share of mascara_length: its streak ends at along = mascara_length x reach
			bool          smeared{ false };
			Fluid::Vec3   dragDir;           // smearing: which way it moves along the skin ...
			float         drag{ 0.0f };      // ... how far it still has to go, metres ...
			float         dragged{ 0.0f };   // ... and how far it has gone in all
			float         widen{ 1.0f };     // how much wider the smearing made it
		};

		// The mascara under one eye, which the tears carry out of the lashes: a band
		// along the lower lid that spreads down over the skin under the eye with
		// [look] mascara_spread and with every tear, most where the tears ran and some
		// under both corners of the eye ([look] mascara_corners). Drawn as a ribbon per lid row
		// Face finds; a smear shifts it as a whole and spreads it further down.
		struct Smudge
		{
			float       strength{ 0.0f };  // 0 none, 1 full
			float       life{ 0.0f };
			float       lifetime{ 1.0f };
			bool        smeared{ false };
			Fluid::Vec3 offset;            // where smearing moved it, head space
			Fluid::Vec3 dragDir;
			float       drag{ 0.0f };
			float       dragged{ 0.0f };
			float       widen{ 1.0f };
		};

		// What a script or plugin set for one actor, for this session.
		struct Override
		{
			float mascara{ -1.0f };   // negative: the setting
		};

		// One eye's fluid: the particles running down that cheek, their solver, and
		// the meshes that draw them. OStrands calls this a domain.
		struct Cheek
		{
			std::uint32_t                  id{ 0 };
			std::vector<Fluid::Particle>   particles;
			std::vector<Fluid::Anchor>     anchors;
			std::unique_ptr<Fluid::Solver> solver;
			std::vector<Fluid::Capsule>    capsules;
			float                          spacing{ 0.0f };
			Fluid::Skin                    skin;
			Fluid::DensityField            field;
			Fluid::CellGrid                clusterGrid;
			float                          accumulator{ 0.0f };
			RE::NiPointer<RE::NiNode>      root;
			std::vector<Blob>              blobs;
			std::vector<Rope>              ropes;
			std::vector<Stain>             stains;
			Smudge                         smudge;
			std::vector<Rope>              stainRopes;
			float                          stainOpacity{ -1.0f };
			std::size_t                    shownStains{ 0 };
			Fluid::Vec3                    outward;  // along the lid, away from the nose, head space
			bool                           outwardKnown{ false };
			Fluid::Vec3                    seat;     // where the current tear wells, head space: a streak starts there
			bool                           seatKnown{ false };
			float                          opacity{ -1.0f };
			float                          shine{ -1.0f };
			float                          gloss{ -1.0f };
			float                          reflection{ -1.0f };
			float                          glow{ -1.0f };
			int                            style{ -1 };
			float                          refraction{ -1.0f };
			int                            envFade{ -1 };  // [look] env_light_fade the meshes carry; -1 unknown
			bool                           strayLogged{ false };  // a mascara point far from the eye was left out and logged
			Fluid::Vec3                    restDown;        // where down was when the sleeping tears settled
			bool                           rested{ false };
			bool                           dirty{ true };   // the fluid changed since the last draw
			bool                           probe{ false };  // debug: log the first solver step of the tear that just started

			std::uint32_t deposits{ 0 };  // drops left on the trail so far, numbering them

			bool          sheds{ true };
			bool          welling{ false };
			int           stream{ 0 };  // which of the eye's ducts the current tear runs from
			float         startOffset{ 0.0f };  // metres along the lid from that duct, picked per tear
			float         reach{ 1.0f };        // its mascara streak's share of [look] mascara_length, picked per tear
			float         wait{ 0.0f };
			float         slot{ 0.0f };
			float         interval{ 0.1f };
			float         hold{ 0.0f };
			int           left{ 0 };
			int           queued{ 0 };
			std::uint32_t run{ 0 };

			std::size_t shownBlobs{ 0 };
			std::size_t shownRopes{ 0 };

			// Lifetime counters for the status line: what happened to the particles.
			std::uint32_t emitted{ 0 };
			std::uint32_t cutFalling{ 0 };
			std::uint32_t cutByHand{ 0 };
			std::uint32_t expired{ 0 };
			std::size_t   lastPosed{ 0 };    // face patches the face wrap posed last frame
			std::size_t   lastPatches{ 0 };  // of those, under this cheek's fluid
			int           lastSteps{ 0 };
		};

		struct Crier
		{
			RE::ActorHandle                            actor;
			float                                      intensity{ 0.0f };
			float                                      target{ 0.0f };
			float                                      timeLeft{ 0.0f };
			bool                                       forever{ false };
			bool                                       crying{ false };
			bool                                       watched{ false };
			bool                                       statusLogged{ false };  // [general] debug_log: the status line went to the log since this crying started
			bool                                       frozen{ false };
			float                                      wipeArm{ 0.0f };
			int                                        callStreams{ 0 }; // what the last Cry asked for; 0 = the setting
			float                                      callMascara{ -1.0f };  // what the last Cry asked for; negative = the actor's or the setting
			int                                        streams{ 1 };     // per eye, that or the setting, this frame
			float                                      mascara{ 0.0f };  // the override or the setting, this frame
			RE::NiPointer<RE::NiAVObject>              skeleton;   // the 3D the bones below came from
			RE::NiPointer<RE::NiNode>                  head;
			std::vector<RE::NiPointer<RE::NiAVObject>> handBones;
			std::vector<Fluid::Capsule>                hands;      // in head space
			std::vector<Fluid::Vec3>                   handPrevious;
			Frame                                      frame;
			std::array<Cheek, Face::kEyes>             cheeks;
		};

		struct Meshes
		{
			RE::NiPointer<RE::NiNode> blob;
			RE::NiPointer<RE::NiNode> rope;
			RE::NiPointer<RE::NiNode> stain;
			bool                      failed{ false };
			bool                      stainFailed{ false };  // no mascara mesh: tears still show, streaks do not
		};

		void        Apply(const Request& a_request, const Settings& a_settings);
		Crier*      CrierFor(RE::ActorHandle a_actor, const Settings& a_settings, bool a_create, bool a_evict = true);
		static bool Enabled();
		void        StartCrying(Crier& a_crier, float a_intensity, float a_seconds, int a_streams, float a_mascara, const Settings& a_settings);
		void        StopCrying(Crier& a_crier);
		void        SendEvent(const char* a_name, RE::ActorHandle a_actor, float a_value);

		void                        Bones(Crier& a_crier, RE::NiAVObject* a_root);
		void                        Locate(Crier& a_crier, RE::NiAVObject* a_root);
		bool                        Frozen(RE::NiAVObject* a_root, const Settings& a_settings) const;
		// The world-space box around the actor's tears, grown by a_margin metres:
		// around the awake ones only (where the face must be posed), or around all
		// of them (where an expression moving the skin must wake them).
		std::optional<Face::Region> Bounds(const Crier& a_crier, bool a_awakeOnly, float a_margin) const;
		static Spot                 SpotOf(const Frame& a_frame, const Face::Duct& a_duct);
		// Where along the lid a tear started: a_offset metres to the side of the duct.
		static Fluid::Vec3          LidPoint(const Spot& a_spot, float a_offset);

		Override OverrideOf(std::uint32_t a_actor) const;

		bool Prepare(Cheek& a_cheek, const Settings& a_settings);
		void Emit(Crier& a_crier, Cheek& a_cheek, const Spots& a_spots, const Settings& a_settings, float a_delta);
		void StartTear(Cheek& a_cheek, float a_strength, int a_streams, const Settings& a_settings);
		bool Wake(Cheek& a_cheek, const Frame& a_frame, bool a_morphing, std::span<const Fluid::Capsule> a_hands);
		void Deposit(Cheek& a_cheek, const Frame& a_frame, const Settings& a_settings, float a_mascara);
		void LayStain(Cheek& a_cheek, const Fluid::Particle& a_particle, std::uint32_t a_order, const Settings& a_settings, float a_mascara);
		void AgeStains(Cheek& a_cheek, float a_delta);
		// Hands touching mascara drag it along the skin; marks already dragging move on.
		void SmearStains(Cheek& a_cheek, const Frame& a_frame, const std::optional<Face::View>& a_view, const LidSpots& a_lids, float a_delta);
		static void Smear(Stain& a_stain, Fluid::Vec3 a_direction);
		static void SmearSmudge(Smudge& a_smudge, Fluid::Vec3 a_direction);
		static bool HasMascara(const Cheek& a_cheek);
		// The point on the posed face nearest a_point (head space), a_depth metres under
		// the skin; a_point itself when no skin is near. The skin's normal in a_normal.
		static Fluid::Vec3 OntoFace(const Frame& a_frame, const Face::View& a_view, Fluid::Vec3 a_point, float a_depth, Fluid::Vec3* a_normal);
		void Simulate(Cheek& a_cheek, const Frame& a_frame, const std::optional<Face::View>& a_view, std::span<const Fluid::Capsule> a_hands,
			const Settings& a_settings, float a_delta, float a_mascara);
		void PoseHands(Crier& a_crier, float a_delta);
		void Gesture(Crier& a_crier, const std::array<Spots, Face::kEyes>& a_spots, float a_scale, const Settings& a_settings, float a_delta);
		void DryFace(Crier& a_crier);
		void Expire(Cheek& a_cheek, const Frame& a_frame, const Settings& a_settings, float a_delta, float a_mascara);
		bool Attach(Cheek& a_cheek, const Crier& a_crier, RE::NiAVObject* a_root, bool a_world);
		void Draw(Cheek& a_cheek, const Mount& a_mount, const Settings& a_settings, const Spots& a_spots, const LidSpots& a_lids, bool a_markers);
		void Clear(Cheek& a_cheek);
		void Release(Cheek& a_cheek);
		bool Idle(const Crier& a_crier) const;

		Meshes& MeshesFor();
		static RE::NiPointer<RE::BSDismemberSkinInstance> DismemberOf(RE::NiNode* a_root, const RE::BSFixedString& a_shape);
		static void                                       Reveal(RE::BSDismemberSkinInstance* a_skin);
		bool    CloneBlob(Blob& a_blob);
		bool    CloneRope(Rope& a_rope, RE::NiNode* a_mesh, const RE::BSFixedString& a_shape);
		Blob*   AcquireBlob(Cheek& a_cheek);
		Rope*   AcquireRope(Cheek& a_cheek);
		Rope*   AcquireStainRope(Cheek& a_cheek);
		void    Shade(RE::NiAVObject* a_root, const Settings& a_settings, bool a_restyle);
		void    ShadeStain(RE::NiAVObject* a_root, const Settings& a_settings);
		void    Reshade(Cheek& a_cheek, const Settings& a_settings);
		static bool Restyles(const Cheek& a_cheek, const Settings& a_settings);
		static bool Reshades(const Cheek& a_cheek, const Settings& a_settings);
		void    DrawStains(Cheek& a_cheek, const Mount& a_mount, const Settings& a_settings, const Spots& a_spots, const LidSpots& a_lids);
		// Poses a_rope as a ribbon along _stainPath with _stainRadius, lying on the skin
		// along _stainNormal; a_taper thins it toward the end.
		void    PoseStain(const Mount& a_mount, Rope& a_rope, bool a_taper);
		void    BendFlat(Rope& a_rope, const std::array<RE::NiPoint3, kRopeBones>& a_positions, const std::array<float, kRopeBones>& a_radii,
			   const std::array<RE::NiPoint3, kRopeBones>& a_normals);
		void    PoseBlob(const Mount& a_mount, Blob& a_blob);
		void    PoseDrop(const Mount& a_mount, Blob& a_blob, Fluid::Vec3 a_center, float a_radius, Fluid::Vec3 a_up, Fluid::Vec3 a_velocity = {},
			   std::optional<Fluid::Vec3> a_contact = std::nullopt);
		void    PoseRope(const Mount& a_mount, Cheek& a_cheek, Rope& a_rope, std::size_t a_begin, std::size_t a_end, const Fluid::Anchor* a_anchor,
			   float a_width, float a_fade);
		// Poses a_rope along the particles in _ropePoints (filled by the caller).
		void    PoseRopeFrom(const Mount& a_mount, Cheek& a_cheek, Rope& a_rope, const Fluid::Anchor* a_anchor, float a_width, float a_fade);
		void    Bend(Rope& a_rope, const std::array<RE::NiPoint3, kRopeBones>& a_positions, const std::array<float, kRopeBones>& a_radii);

		static Fluid::Vec3  ToLocal(const Frame& a_frame, const RE::NiPoint3& a_point);
		static Fluid::Vec3  ToLocalDirection(const Frame& a_frame, const RE::NiPoint3& a_direction);
		static RE::NiPoint3 ToWorld(const Frame& a_frame, Fluid::Vec3 a_point);
		static RE::NiPoint3 ToBone(Fluid::Vec3 a_point);

		void        Publish();
		std::string Describe(RE::Actor* a_actor, const Crier* a_crier, const Settings& a_settings) const;
		void        RefreshStatus(const Settings& a_settings);

		mutable std::mutex                                                       _lock;
		std::vector<Request>                                                     _pending;
		std::unordered_map<std::uint32_t, State>                                 _states;
		std::unordered_map<std::uint32_t, std::string>                           _statuses;
		std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> _wanted;
		std::chrono::steady_clock::time_point                                    _statusAt{};

		std::unordered_map<std::uint32_t, std::unique_ptr<Crier>> _criers;
		std::unordered_map<std::uint32_t, Override>                _overrides;  // main thread only
		std::vector<std::uint32_t>                                 _order;
		Meshes                                                     _meshes;
		std::vector<Fluid::Vec3>                                   _directions;
		std::vector<Fluid::SurfacePoint>                           _scratch;
		std::vector<Fluid::Particle>                               _ropePoints;
		std::vector<float>                                         _ropeAlong;
		std::vector<Blob>                                          _spareBlobs;
		std::vector<Rope>                                          _spareRopes;
		std::vector<Rope>                                          _spareStains;
		std::vector<Fluid::Vec3>                                   _stainPath;
		std::vector<Fluid::Vec3>                                   _stainNormal;
		std::vector<float>                                         _stainRadius;
		std::vector<std::size_t>                                   _stainOrder;
		// Draw and Expire scratch, kept across frames so a frame allocates nothing.
		std::vector<std::size_t>              _pooled;
		std::vector<std::size_t>              _owner;
		std::vector<std::size_t>              _claimed;
		std::vector<std::size_t>              _seeds;
		std::vector<std::size_t>              _remap;
		std::vector<std::size_t>              _trail;
		std::vector<std::vector<std::size_t>> _hints;
		std::vector<Fluid::Cluster>           _clusters;
		std::vector<Fluid::Piece>             _pieces;
		std::vector<Fluid::Run>               _runs;
		std::vector<Droplet>                  _droplets;
		std::mt19937                                               _rng{ std::random_device{}() };
		std::uint32_t                                              _nextId{ 0 };
		std::uint32_t                                              _nextRun{ 0 };
		int                                                        _cloneBudget{ 0 };
		float                                                      _statTime{ 0.0f };
	};
}
