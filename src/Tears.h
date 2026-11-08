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

		void Cry(RE::ActorHandle a_actor, float a_intensity, float a_seconds);
		void Stop(RE::ActorHandle a_actor, bool a_wipe);
		void Shed(RE::ActorHandle a_actor, int a_eye);
		void Wipe(RE::ActorHandle a_actor);
		// Plays a_idle (the vanilla brow wipe when null) and dries the face when a hand
		// reaches the eyes, or when the gesture runs out of time.
		void WipeWithHand(RE::ActorHandle a_actor, RE::TESIdleForm* a_idle);
		void WipeAll();
		void CryNearby(float a_radius, float a_intensity, float a_seconds);

		State       Query(RE::ActorHandle a_actor) const;
		std::string Status(RE::ActorHandle a_actor);
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
				kHandWipe
			};

			Kind             kind{ Kind::kCry };
			RE::ActorHandle  actor;
			float            intensity{ 0.0f };
			float            seconds{ 0.0f };
			int              eye{ -1 };
			bool             wipe{ false };
			float            radius{ 0.0f };
			RE::TESIdleForm* idle{ nullptr };
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

		// Where the tear nodes hang and what their transforms are expressed in:
		// under the head bone in head-bone units (the default), or under the cell
		// node in world units as 0.1.0 did ([debug] mount_world, `tears mount 1`),
		// kept as an A/B for "the tears do not show".
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
			float                          opacity{ -1.0f };
			float                          shine{ -1.0f };
			float                          gloss{ -1.0f };
			Fluid::Vec3                    restDown;        // where down was when the sleeping tears settled
			bool                           rested{ false };
			bool                           dirty{ true };   // the fluid changed since the last draw

			bool          sheds{ true };
			bool          welling{ false };
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
			bool                                       frozen{ false };
			float                                      wipeArm{ 0.0f };
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
			bool                      failed{ false };
		};

		void        Apply(const Request& a_request, const Settings& a_settings);
		Crier*      CrierFor(RE::ActorHandle a_actor, const Settings& a_settings, bool a_create, bool a_evict = true);
		static bool Enabled();
		void        StartCrying(Crier& a_crier, float a_intensity, float a_seconds, const Settings& a_settings);
		void        StopCrying(Crier& a_crier);
		void        SendEvent(const char* a_name, RE::ActorHandle a_actor, float a_value);

		void                        Bones(Crier& a_crier, RE::NiAVObject* a_root);
		void                        Locate(Crier& a_crier, RE::NiAVObject* a_root);
		bool                        Frozen(RE::NiAVObject* a_root, const Settings& a_settings) const;
		std::optional<Face::Region> RegionOf(const Crier& a_crier) const;
		static Spot                 SpotOf(const Frame& a_frame, const Face::Duct& a_duct);

		bool Prepare(Cheek& a_cheek, const Settings& a_settings);
		void Emit(Crier& a_crier, Cheek& a_cheek, const Spot& a_spot, const Settings& a_settings, float a_delta);
		void StartTear(Cheek& a_cheek, float a_strength, const Settings& a_settings);
		void Wake(Cheek& a_cheek, const Frame& a_frame, bool a_morphing, std::span<const Fluid::Capsule> a_hands);
		void Deposit(Cheek& a_cheek, const Frame& a_frame, const Settings& a_settings);
		void Simulate(Cheek& a_cheek, const Frame& a_frame, const std::optional<Face::View>& a_view, std::span<const Fluid::Capsule> a_hands,
			const Settings& a_settings, float a_delta);
		void PoseHands(Crier& a_crier, float a_delta);
		void Gesture(Crier& a_crier, const std::array<std::optional<Spot>, Face::kEyes>& a_spots, float a_scale, const Settings& a_settings,
			float a_delta);
		void DryFace(Crier& a_crier);
		void Expire(Cheek& a_cheek, const Frame& a_frame, const Settings& a_settings, float a_delta);
		bool Attach(Cheek& a_cheek, const Crier& a_crier, RE::NiAVObject* a_root, bool a_world);
		void Draw(Cheek& a_cheek, const Mount& a_mount, const Settings& a_settings, const Spot* a_marker);
		void Clear(Cheek& a_cheek);
		void Release(Cheek& a_cheek);
		bool Idle(const Crier& a_crier) const;

		Meshes& MeshesFor();
		static RE::NiPointer<RE::BSDismemberSkinInstance> DismemberOf(RE::NiNode* a_root, const RE::BSFixedString& a_shape);
		static void                                       Reveal(RE::BSDismemberSkinInstance* a_skin);
		bool    CloneBlob(Blob& a_blob);
		bool    CloneRope(Rope& a_rope);
		Blob*   AcquireBlob(Cheek& a_cheek);
		Rope*   AcquireRope(Cheek& a_cheek);
		void    Shade(RE::NiAVObject* a_root, const Settings& a_settings);
		void    Reshade(Cheek& a_cheek, const Settings& a_settings);
		void    PoseBlob(const Mount& a_mount, Blob& a_blob);
		void    PoseDrop(const Mount& a_mount, Blob& a_blob, Fluid::Vec3 a_center, float a_radius, Fluid::Vec3 a_up, Fluid::Vec3 a_velocity = {},
			   std::optional<Fluid::Vec3> a_contact = std::nullopt);
		void    PoseRope(const Mount& a_mount, Cheek& a_cheek, Rope& a_rope, std::size_t a_begin, std::size_t a_end, const Fluid::Anchor* a_anchor,
			   float a_width, float a_fade);
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
		std::vector<std::uint32_t>                                 _order;
		Meshes                                                     _meshes;
		std::vector<Fluid::Vec3>                                   _directions;
		std::vector<Fluid::SurfacePoint>                           _scratch;
		std::vector<Fluid::Particle>                               _ropePoints;
		std::vector<float>                                         _ropeAlong;
		std::vector<Blob>                                          _spareBlobs;
		std::vector<Rope>                                          _spareRopes;
		// Draw and Expire scratch, kept across frames so a frame allocates nothing.
		std::vector<std::size_t>              _pooled;
		std::vector<std::size_t>              _owner;
		std::vector<std::size_t>              _claimed;
		std::vector<std::size_t>              _seeds;
		std::vector<std::size_t>              _remap;
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
