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
				kNearby
			};

			Kind            kind{ Kind::kCry };
			RE::ActorHandle actor;
			float           intensity{ 0.0f };
			float           seconds{ 0.0f };
			int             eye{ -1 };
			bool            wipe{ false };
			float           radius{ 0.0f };
		};

		struct Blob
		{
			RE::NiPointer<RE::NiNode>        root;
			std::vector<RE::NiAVObject*>     bones;
			std::vector<Fluid::SurfacePoint> surface;
			std::size_t                      seed{ 0 };
			bool                             live{ false };
			bool                             used{ false };
		};

		struct Rope
		{
			RE::NiPointer<RE::NiNode>                   root;
			std::array<RE::NiAVObject*, kRopeBones>     bones{};
			std::array<GeometryMath::Frame, kRopeBones> frames{};
			bool                                        used{ false };
		};

		// One eye's fluid: the particles running down that cheek, their solver, and
		// the meshes that draw them. OStrands calls this a domain.
		struct Cheek
		{
			std::uint32_t                  id{ 0 };
			bool                           placed{ false };
			RE::NiPoint3                   origin;
			RE::TESObjectCELL*             cell{ nullptr };
			std::vector<Fluid::Particle>   particles;
			std::vector<Fluid::Anchor>     anchors;
			std::unique_ptr<Fluid::Solver> solver;
			float                          spacing{ 0.0f };
			Fluid::Skin                    skin;
			Fluid::Ground                  ground{ 1.0f };
			Fluid::DensityField            field;
			Fluid::CellGrid                clusterGrid;
			float                          accumulator{ 0.0f };
			RE::NiPointer<RE::NiNode>      root;
			std::vector<Blob>              blobs;
			std::vector<Rope>              ropes;
			float                          opacity{ -1.0f };
			float                          shine{ -1.0f };
			float                          gloss{ -1.0f };

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
		};

		struct Crier
		{
			RE::ActorHandle            actor;
			float                      intensity{ 0.0f };
			float                      target{ 0.0f };
			float                      timeLeft{ 0.0f };
			bool                       forever{ false };
			bool                       crying{ false };
			bool                       watched{ false };
			std::array<Cheek, Face::kEyes> cheeks;
		};

		struct Meshes
		{
			RE::NiPointer<RE::NiNode> blob;
			RE::NiPointer<RE::NiNode> rope;
			bool                      failed{ false };
		};

		void   Apply(const Request& a_request, const Settings& a_settings);
		Crier* CrierFor(RE::ActorHandle a_actor, const Settings& a_settings, bool a_create);
		void   StartCrying(Crier& a_crier, float a_intensity, float a_seconds, const Settings& a_settings);
		void   StopCrying(Crier& a_crier);
		void   SendEvent(const char* a_name, RE::ActorHandle a_actor, float a_value);

		bool Place(Cheek& a_cheek, RE::Actor* a_actor, const Face::Duct& a_duct, const Settings& a_settings);
		void Emit(Crier& a_crier, Cheek& a_cheek, const Face::Duct& a_duct, const Settings& a_settings, float a_delta);
		void StartTear(Cheek& a_cheek, float a_strength, const Settings& a_settings);
		void Simulate(Cheek& a_cheek, const std::optional<Face::View>& a_view, const Settings& a_settings, float a_delta);
		void Expire(Cheek& a_cheek, const Settings& a_settings, float a_delta);
		bool Attach(Cheek& a_cheek, RE::Actor* a_actor);
		void Draw(Cheek& a_cheek, const Settings& a_settings, const Face::Duct* a_marker);
		void Clear(Cheek& a_cheek);
		void Release(Cheek& a_cheek);
		bool Idle(const Crier& a_crier) const;

		Meshes& MeshesFor();
		bool    CloneBlob(Blob& a_blob);
		bool    CloneRope(Rope& a_rope);
		Blob*   AcquireBlob(Cheek& a_cheek);
		Rope*   AcquireRope(Cheek& a_cheek);
		void    Shade(RE::NiAVObject* a_root, const Settings& a_settings);
		void    Reshade(Cheek& a_cheek, const Settings& a_settings);
		void    PoseBlob(Cheek& a_cheek, Blob& a_blob);
		void    PoseDrop(Cheek& a_cheek, Blob& a_blob, Fluid::Vec3 a_center, float a_radius, Fluid::Vec3 a_velocity = {},
			   std::optional<Fluid::Vec3> a_contact = std::nullopt);
		void    PoseRope(Cheek& a_cheek, Rope& a_rope, std::size_t a_begin, std::size_t a_end, const Fluid::Anchor* a_anchor, float a_width,
			   float a_fade);
		void    Bend(Rope& a_rope, const std::array<RE::NiPoint3, kRopeBones>& a_positions, const std::array<float, kRopeBones>& a_radii);

		Fluid::Vec3  ToLocal(const Cheek& a_cheek, const RE::NiPoint3& a_point) const;
		RE::NiPoint3 ToWorld(const Cheek& a_cheek, Fluid::Vec3 a_point) const;

		void        Publish();
		std::string Describe(RE::Actor* a_actor, const Crier* a_crier, const Settings& a_settings) const;
		void        RefreshStatus(const Settings& a_settings);

		mutable std::mutex   _lock;
		std::vector<Request> _pending;
		std::unordered_map<std::uint32_t, State>       _states;
		std::unordered_map<std::uint32_t, std::string> _statuses;
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
		std::mt19937                                               _rng{ std::random_device{}() };
		std::uint32_t                                              _nextId{ 0 };
		std::uint32_t                                              _nextRun{ 0 };
		int                                                        _cloneBudget{ 0 };
		float                                                      _clock{ 0.0f };
		float                                                      _statTime{ 0.0f };
		bool                                                       _enabled{ true };
	};
}
