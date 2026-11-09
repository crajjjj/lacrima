#include "Tears.h"

// The fluid rendering here (clusters wrapped into blobs, linked runs posed as
// ropes, drops squashed against the skin) is adapted from OStrands'
// FluidManager.cpp by LeonVanWinkle (Saint), GPL-3.0. OStrands feeds it from
// climax emitters and openings, in world space; Lacrima feeds it from the tear
// ducts, with one small simulation per eye that lives in the head bone's own
// frame and whose nodes hang under that bone.

namespace Lacrima
{
	namespace
	{
		using Vec3 = Fluid::Vec3;

		constexpr float       kUnitsPerMeter = 70.0f;
		constexpr float       kBaseSpacing = 0.0035f;
		constexpr float       kThreshold = 1.75f;
		constexpr float       kBulkEnter = 3.0f;
		constexpr float       kBulkLeave = 2.5f;
		constexpr float       kCovered = 0.3f;
		constexpr float       kSlotSpacing = 0.9f;
		constexpr float       kLinkSpacing = 0.6f;
		constexpr std::size_t kBlobMinimum = 6;
		constexpr float       kSmoothing = 0.05f;
		constexpr float       kStep = 1.0f / 120.0f;
		constexpr int         kMaxSteps = 6;
		constexpr std::size_t kMaxBlobs = 32;
		constexpr std::size_t kMaxRopes = 32;
		constexpr int         kClonesPerFrame = 2;
		constexpr float       kStreak = 1.5f;
		constexpr float       kStreakStart = 0.3f;
		constexpr float       kMaxStretch = 3.0f;
		constexpr float       kSquash = 0.45f;
		constexpr float       kSkinCell = 0.03f;  // the skin lookup finds a patch within half of this: a vanilla head's cheek has 1 cm gaps
		constexpr float       kSkinMargin = 0.06f;
		constexpr float       kWatchMargin = 0.006f;  // metres around the tears where an expression moving the skin wakes them
		constexpr float       kTipGrace = 0.15f;      // seconds off the skin after which a piece of the bead no longer ends the trail
		constexpr float       kWellSpeed = 0.02f;
		constexpr float       kFallSpeed = 0.25f;
		// A particle must be off every surface this long before falling counts: free
		// fall passes kFallSpeed within 30 ms, before a released tear can land.
		constexpr float kFallGrace = 0.2f;
		constexpr float       kRamp = 1.5f;
		constexpr float       kMarkerRadius = 1.5f;  // units; big on purpose, it is a "do the meshes show at all" probe
		constexpr float       kGravity = 9.81f;
		constexpr float       kMinScale = 0.05f;
		// Sleeping tears wake when the head has tilted this far (cos 15 degrees) from
		// where they settled: gravity pulls them a new way.
		constexpr float kTiltWake = 0.966f;
		constexpr auto  kStatusEvery = 250ms;
		constexpr auto  kStatusWanted = 60s;

		constexpr float      kWipeFade = 0.3f;
		constexpr float      kWipeRest = 2.0f;
		constexpr float      kPalmRadius = 1.1f;
		constexpr float      kFingerRadius = 0.45f;
		constexpr float      kTipExtension = 0.7f;
		constexpr float      kMaxHandSpeed = 500.0f;  // world units per second, relative to the head; faster is a teleport
		constexpr RE::FormID kWipeIdle = 0x000977EC;  // IdleWipeBrow, Skyrim.esm

		constexpr const char* kHeadBone = "NPC Head [Head]";

		// Per side: the hand, then the thumb, index, middle and ring fingers, three
		// bones each from base to tip.
		constexpr std::size_t kSideBones = 13;
		constexpr std::array  kHandBones{
			"NPC L Hand [LHnd]",
			"NPC L Finger00 [LF00]", "NPC L Finger01 [LF01]", "NPC L Finger02 [LF02]",
			"NPC L Finger10 [LF10]", "NPC L Finger11 [LF11]", "NPC L Finger12 [LF12]",
			"NPC L Finger20 [LF20]", "NPC L Finger21 [LF21]", "NPC L Finger22 [LF22]",
			"NPC L Finger30 [LF30]", "NPC L Finger31 [LF31]", "NPC L Finger32 [LF32]",
			"NPC R Hand [RHnd]",
			"NPC R Finger00 [RF00]", "NPC R Finger01 [RF01]", "NPC R Finger02 [RF02]",
			"NPC R Finger10 [RF10]", "NPC R Finger11 [RF11]", "NPC R Finger12 [RF12]",
			"NPC R Finger20 [RF20]", "NPC R Finger21 [RF21]", "NPC R Finger22 [RF22]",
			"NPC R Finger30 [RF30]", "NPC R Finger31 [RF31]", "NPC R Finger32 [RF32]",
		};

		constexpr const char* kBlobPath = "Lacrima\\tear_blob.nif";
		constexpr const char* kRopePath = "Lacrima\\tear_rope.nif";
		constexpr const char* kStainPath = "Lacrima\\tear_stain.nif";

		// Running mascara.
		constexpr float       kStainRadius = 0.0007f;   // metres at width 1: a streak about 1.4 mm wide
		constexpr float       kStainLift = 0.0002f;     // how far above the skin a streak lies
		constexpr float       kStainFade = 0.3f;        // the last share of its life a streak spends fading
		constexpr float       kSmearWidth = 1.8f;
		constexpr float       kSmearLife = 0.35f;       // a smeared streak keeps at most this share of its lifetime
		constexpr std::size_t kMaxStainRuns = 16;       // streaks per cheek; the oldest goes past this
		constexpr std::size_t kMaxStains = 160;         // points per cheek
		constexpr std::uint32_t kBeadOrder = 0x7FFFFFFF; // where the bead came to rest: the end of its streak

		struct MeshNames
		{
			RE::BSFixedString blobShape{ "BlobShape" };
			RE::BSFixedString ropeShape{ "RopeShape" };
			RE::BSFixedString stainShape{ "StainShape" };

			static const MeshNames& Get()
			{
				static const MeshNames names;
				return names;
			}
		};

		Vec3         ToVec(const RE::NiPoint3& a_point) { return { a_point.x, a_point.y, a_point.z }; }
		RE::NiPoint3 ToPoint(Vec3 a_vec) { return { a_vec.x, a_vec.y, a_vec.z }; }

		RE::NiMatrix3 Rotation(const GeometryMath::Frame& a_frame)
		{
			const auto& [x, y, z] = a_frame;
			RE::NiMatrix3 m;
			m.entry[0][0] = x.x; m.entry[1][0] = x.y; m.entry[2][0] = x.z;
			m.entry[0][1] = y.x; m.entry[1][1] = y.y; m.entry[2][1] = y.z;
			m.entry[0][2] = z.x; m.entry[1][2] = z.y; m.entry[2][2] = z.z;
			return m;
		}

		RE::NiMatrix3 ArcRotation(Vec3 a_from, Vec3 a_to)
		{
			RE::NiMatrix3 m;
			const float   c = Fluid::Dot(a_from, a_to);
			if (c < -0.999f) {
				Vec3 axis = std::abs(a_from.x) < 0.9f ? Vec3{ 1.0f, 0.0f, 0.0f } : Vec3{ 0.0f, 1.0f, 0.0f };
				axis = Fluid::Normalize(axis - a_from * Fluid::Dot(axis, a_from), { 0.0f, 0.0f, 1.0f });
				const float a[3]{ axis.x, axis.y, axis.z };
				for (int r = 0; r < 3; ++r) {
					for (int col = 0; col < 3; ++col) {
						m.entry[r][col] = 2.0f * a[r] * a[col] - (r == col ? 1.0f : 0.0f);
					}
				}
				return m;
			}
			const Vec3  v = Fluid::Cross(a_from, a_to);
			const float k = 1.0f / (1.0f + c);
			m.entry[0][0] = 1.0f - k * (v.y * v.y + v.z * v.z);
			m.entry[0][1] = -v.z + k * v.x * v.y;
			m.entry[0][2] = v.y + k * v.x * v.z;
			m.entry[1][0] = v.z + k * v.x * v.y;
			m.entry[1][1] = 1.0f - k * (v.x * v.x + v.z * v.z);
			m.entry[1][2] = -v.x + k * v.y * v.z;
			m.entry[2][0] = -v.y + k * v.x * v.z;
			m.entry[2][1] = v.x + k * v.y * v.z;
			m.entry[2][2] = 1.0f - k * (v.x * v.x + v.y * v.y);
			return m;
		}

		Vec3 Stretch(Vec3 a_value, Vec3 a_axis, float a_factor)
		{
			const float side = 1.0f / std::sqrt(a_factor);
			return a_value * side + a_axis * (Fluid::Dot(a_value, a_axis) * (a_factor - side));
		}

		// The simulation metre is a_scale world metres (the head bone's scale), so
		// gravity scales inversely to stay 9.81 m/s^2 in the world.
		Fluid::Parameters ParametersOf(const Settings& a_settings, float a_scale, Vec3 a_down)
		{
			return { 4, a_settings.cohesion, a_settings.viscosity, kGravity / a_scale, a_down, a_settings.stretch, a_settings.friction,
				a_settings.stickiness, a_settings.cling };
		}

		std::string Clean(std::string a_text)
		{
			std::ranges::replace(a_text, '%', ' ');
			return a_text;
		}

		Face::Placement PlacementOf(const Settings& a_settings)
		{
			return { a_settings.ductPosition, a_settings.ductBelow, a_settings.streams, a_settings.streamSpread };
		}

		float SegmentDistance(Vec3 a_point, Vec3 a_from, Vec3 a_to)
		{
			const Vec3  span = a_to - a_from;
			const float length = Fluid::LengthSquared(span);
			const float t = length > 1e-12f ? std::clamp(Fluid::Dot(a_point - a_from, span) / length, 0.0f, 1.0f) : 0.0f;
			return Fluid::Length(a_point - (a_from + span * t));
		}

		// The point on the skin under a_point (a particle rides a little above it), just
		// above the surface; a_point itself when no skin is near.
		Vec3 OntoSkin(const Fluid::Skin& a_skin, Vec3 a_point)
		{
			const auto&       patches = a_skin.Patches();
			const std::size_t k = a_skin.Nearest(a_point);
			if (k == Fluid::kNone || k >= patches.size()) {
				return a_point;
			}
			const auto& patch = patches[k];
			const float height = Fluid::Dot(a_point - patch.position, patch.normal);
			return a_point - patch.normal * (height - kStainLift);
		}

		std::string NameOf(RE::Actor* a_actor)
		{
			const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
			return Clean(name && *name ? name : "someone");
		}
	}

	Tears& Tears::Get()
	{
		static Tears tears;
		return tears;
	}

	// ------------------------------------------------------------------ requests

	void Tears::Cry(RE::ActorHandle a_actor, float a_intensity, float a_seconds)
	{
		if (!a_actor) {
			return;
		}
		const float intensity = std::isfinite(a_intensity) ? std::clamp(a_intensity, 0.0f, 1.0f) : 0.6f;
		const float seconds = std::isfinite(a_seconds) ? a_seconds : 0.0f;
		if (!Enabled()) {
			return;
		}
		std::scoped_lock guard(_lock);
		_pending.push_back({ Request::Kind::kCry, a_actor, intensity, seconds });
		auto& state = _states[a_actor.native_handle()];
		state.crying = intensity > 0.0f;
		state.intensity = intensity;
		state.timeLeft = intensity > 0.0f ? (seconds > 0.0f ? seconds : -1.0f) : 0.0f;
	}

	void Tears::Stop(RE::ActorHandle a_actor, bool a_wipe)
	{
		if (!a_actor) {
			return;
		}
		std::scoped_lock guard(_lock);
		_pending.push_back({ Request::Kind::kStop, a_actor, 0.0f, 0.0f, -1, a_wipe });
		if (auto it = _states.find(a_actor.native_handle()); it != _states.end()) {
			it->second = {};
		}
	}

	void Tears::Shed(RE::ActorHandle a_actor, int a_eye)
	{
		if (!a_actor) {
			return;
		}
		if (!Enabled()) {
			return;
		}
		std::scoped_lock guard(_lock);
		_pending.push_back({ Request::Kind::kShed, a_actor, 0.0f, 0.0f, a_eye });
	}

	void Tears::Wipe(RE::ActorHandle a_actor)
	{
		if (!a_actor) {
			return;
		}
		std::scoped_lock guard(_lock);
		_pending.push_back({ Request::Kind::kWipe, a_actor });
	}

	void Tears::WipeWithHand(RE::ActorHandle a_actor, RE::TESIdleForm* a_idle)
	{
		if (!a_actor) {
			return;
		}
		std::scoped_lock guard(_lock);
		Request request{ Request::Kind::kHandWipe, a_actor };
		request.idle = a_idle;
		_pending.push_back(request);
	}

	void Tears::WipeAll()
	{
		std::scoped_lock guard(_lock);
		_pending.push_back({ Request::Kind::kWipeAll });
	}

	void Tears::CryNearby(float a_radius, float a_intensity, float a_seconds)
	{
		if (!Enabled()) {
			return;
		}
		Request request{ Request::Kind::kNearby };
		request.intensity = std::isfinite(a_intensity) ? std::clamp(a_intensity, 0.0f, 1.0f) : 0.6f;
		request.seconds = std::isfinite(a_seconds) ? a_seconds : 0.0f;
		request.radius = std::isfinite(a_radius) ? std::max(a_radius, 0.0f) : 0.0f;
		std::scoped_lock guard(_lock);
		_pending.push_back(request);
	}

	void Tears::SetStreams(RE::ActorHandle a_actor, int a_streams)
	{
		if (!a_actor) {
			return;
		}
		Request request{ Request::Kind::kStreams, a_actor };
		request.eye = std::clamp(a_streams, 0, static_cast<int>(Face::kMaxStreams));
		std::scoped_lock guard(_lock);
		_pending.push_back(request);
	}

	void Tears::SetMascara(RE::ActorHandle a_actor, float a_strength)
	{
		if (!a_actor) {
			return;
		}
		Request request{ Request::Kind::kMascara, a_actor };
		request.intensity = std::isfinite(a_strength) ? std::min(a_strength, 1.0f) : -1.0f;
		std::scoped_lock guard(_lock);
		_pending.push_back(request);
	}

	Tears::Override Tears::OverrideOf(std::uint32_t a_actor) const
	{
		const auto it = _overrides.find(a_actor);
		return it != _overrides.end() ? it->second : Override{};
	}

	Face::Placement Tears::PlacementFor(std::uint32_t a_actor, const Settings& a_settings) const
	{
		auto placement = PlacementOf(a_settings);
		if (const auto over = OverrideOf(a_actor); over.streams > 0) {
			placement.streams = over.streams;
		}
		return placement;
	}

	Tears::State Tears::Query(RE::ActorHandle a_actor) const
	{
		std::scoped_lock guard(_lock);
		const auto       it = _states.find(a_actor.native_handle());
		return it != _states.end() ? it->second : State{};
	}

	std::size_t Tears::CryingCount() const
	{
		std::scoped_lock guard(_lock);
		return static_cast<std::size_t>(std::ranges::count_if(_states, [](const auto& a_entry) { return a_entry.second.crying; }));
	}

	std::string Tears::Status(RE::ActorHandle a_actor)
	{
		if (!a_actor) {
			return "No actor.";
		}
		std::scoped_lock guard(_lock);
		_wanted[a_actor.native_handle()] = std::chrono::steady_clock::now();
		const auto it = _statuses.find(a_actor.native_handle());
		return it != _statuses.end() ? it->second : "Reading this actor; run the command again in a moment.";
	}

	// ---------------------------------------------------------------- lifecycle

	void Tears::Reset()
	{
		{
			std::scoped_lock guard(_lock);
			_pending.clear();
			_states.clear();
			_statuses.clear();
			_wanted.clear();
		}
		for (auto& [handle, crier] : _criers) {
			for (auto& cheek : crier->cheeks) {
				Release(cheek);
			}
		}
		_criers.clear();
		_order.clear();
		_overrides.clear();
		_spareBlobs.clear();
		_spareRopes.clear();
		_spareStains.clear();
	}

	void Tears::SendEvent(const char* a_name, RE::ActorHandle a_actor, float a_value)
	{
		auto* source = SKSE::GetModCallbackEventSource();
		const auto actor = a_actor.get();
		if (!source || !actor) {
			return;
		}
		SKSE::ModCallbackEvent event{ RE::BSFixedString(a_name), RE::BSFixedString(""), a_value, actor.get() };
		source->SendEvent(&event);
	}

	bool Tears::Enabled()
	{
		auto guard = Settings::Lock();
		return Settings::Get().enabled;
	}

	Tears::Crier* Tears::CrierFor(RE::ActorHandle a_actor, const Settings& a_settings, bool a_create, bool a_evict)
	{
		const auto key = a_actor.native_handle();
		if (auto it = _criers.find(key); it != _criers.end()) {
			return it->second.get();
		}
		if (!a_create) {
			return nullptr;
		}
		const auto actor = a_actor.get();
		if (!actor || !actor->Get3D(false)) {
			return nullptr;
		}
		if (!a_evict && _order.size() >= static_cast<std::size_t>(a_settings.maxActors)) {
			return nullptr;
		}
		while (_order.size() >= static_cast<std::size_t>(a_settings.maxActors)) {
			const auto oldest = _order.front();
			_order.erase(_order.begin());
			if (auto it = _criers.find(oldest); it != _criers.end()) {
				if (it->second->crying) {
					SendEvent("Lacrima_CryStop", it->second->actor, 0.0f);
				}
				for (auto& cheek : it->second->cheeks) {
					Release(cheek);
				}
				_criers.erase(it);
			}
		}
		auto crier = std::make_unique<Crier>();
		crier->actor = a_actor;
		for (auto& cheek : crier->cheeks) {
			cheek.id = ++_nextId;
		}
		_order.push_back(key);
		return _criers.emplace(key, std::move(crier)).first->second.get();
	}

	void Tears::StartCrying(Crier& a_crier, float a_intensity, float a_seconds, const Settings& a_settings)
	{
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const bool                            started = !a_crier.crying;
		a_crier.target = a_intensity;
		a_crier.forever = a_seconds <= 0.0f;
		a_crier.timeLeft = a_crier.forever ? -1.0f : a_seconds;
		a_crier.crying = true;
		if (started) {
			const std::size_t only = unit(_rng) < 0.5f ? Face::kLeft : Face::kRight;
			for (std::size_t e = 0; e < Face::kEyes; ++e) {
				auto& cheek = a_crier.cheeks[e];
				cheek.sheds = a_settings.bothEyes || e == only;
				if (!cheek.welling) {
					cheek.wait = 0.2f + 1.2f * unit(_rng);
				}
			}
			SendEvent("Lacrima_CryStart", a_crier.actor, a_intensity);
		}
	}

	void Tears::StopCrying(Crier& a_crier)
	{
		if (a_crier.crying) {
			SendEvent("Lacrima_CryStop", a_crier.actor, 0.0f);
		}
		a_crier.crying = false;
		a_crier.target = 0.0f;
		a_crier.timeLeft = 0.0f;
		a_crier.forever = false;
	}

	void Tears::Apply(const Request& a_request, const Settings& a_settings)
	{
		switch (a_request.kind) {
		case Request::Kind::kCry:
			if (a_request.intensity <= 0.0f) {
				if (auto* crier = CrierFor(a_request.actor, a_settings, false)) {
					StopCrying(*crier);
				}
			} else if (auto* crier = CrierFor(a_request.actor, a_settings, true)) {
				StartCrying(*crier, a_request.intensity, a_request.seconds, a_settings);
			}
			break;
		case Request::Kind::kStop:
			if (auto* crier = CrierFor(a_request.actor, a_settings, false)) {
				StopCrying(*crier);
				if (a_request.wipe) {
					for (auto& cheek : crier->cheeks) {
						Clear(cheek);
					}
				}
			}
			break;
		case Request::Kind::kShed:
			if (auto* crier = CrierFor(a_request.actor, a_settings, true)) {
				std::uniform_int_distribution<int> coin(0, 1);
				const int                          eye = a_request.eye >= 0 && a_request.eye <= 2 ? a_request.eye : coin(_rng);
				for (std::size_t e = 0; e < Face::kEyes; ++e) {
					if (eye == 2 || static_cast<std::size_t>(eye) == e) {
						++crier->cheeks[e].queued;
					}
				}
			}
			break;
		case Request::Kind::kWipe:
			if (auto* crier = CrierFor(a_request.actor, a_settings, false)) {
				for (auto& cheek : crier->cheeks) {
					Clear(cheek);
				}
			}
			break;
		case Request::Kind::kWipeAll:
			for (auto& [key, crier] : _criers) {
				for (auto& cheek : crier->cheeks) {
					Clear(cheek);
				}
			}
			break;
		case Request::Kind::kHandWipe:
			{
				const auto actor = a_request.actor.get();
				auto*      idle = a_request.idle ? a_request.idle : RE::TESForm::LookupByID<RE::TESIdleForm>(kWipeIdle);
				auto*      process = actor ? actor->GetActorRuntimeData().currentProcess : nullptr;
				const bool playing = idle && process && actor->Get3D(false) && process->PlayIdle(actor.get(), idle, nullptr);
				if (auto* crier = CrierFor(a_request.actor, a_settings, false)) {
					if (playing) {
						crier->wipeArm = a_settings.gestureWait;
					} else {
						DryFace(*crier);
					}
				}
				if (a_settings.debugLog) {
					logger::info("{}: wipe gesture {}", NameOf(actor.get()), playing ? "started" : "could not play; face dried at once");
				}
			}
			break;
		case Request::Kind::kNearby:
			{
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* lists = RE::ProcessLists::GetSingleton();
				if (!player || !lists || a_request.intensity <= 0.0f) {
					break;
				}
				const auto  center = player->GetPosition();
				std::size_t started = 0;
				std::size_t skipped = 0;
				for (const auto& handle : lists->highActorHandles) {
					const auto actor = handle.get();
					if (!actor || actor->IsDead() || !actor->Get3D(false) || actor->GetPosition().GetDistance(center) > a_request.radius) {
						continue;
					}
					if (!Face::Of(actor.get(), PlacementFor(handle.native_handle(), a_settings), nullptr)) {
						continue;
					}
					if (auto* crier = CrierFor(handle, a_settings, true, false)) {
						StartCrying(*crier, a_request.intensity, a_request.seconds, a_settings);
						++started;
					} else {
						++skipped;
					}
				}
				logger::info("{} nearby actor(s) started crying{}", started,
					skipped ? std::format("; {} more had no free slot ([tears] max_actors)", skipped) : std::string{});
			}
			break;
		case Request::Kind::kStreams:
		case Request::Kind::kMascara:
			{
				const auto key = a_request.actor.native_handle();
				auto       over = OverrideOf(key);
				if (a_request.kind == Request::Kind::kStreams) {
					over.streams = a_request.eye;
				} else {
					over.mascara = a_request.intensity;
				}
				if (over.streams <= 0 && over.mascara < 0.0f) {
					_overrides.erase(key);
				} else {
					_overrides[key] = over;
				}
			}
			break;
		}
	}

	bool Tears::Idle(const Crier& a_crier) const
	{
		return !a_crier.crying && !a_crier.watched && a_crier.wipeArm <= 0.0f && std::ranges::all_of(a_crier.cheeks, [](const Cheek& a_cheek) {
			return a_cheek.particles.empty() && a_cheek.stains.empty() && !a_cheek.welling && a_cheek.queued == 0;
		});
	}

	// -------------------------------------------------------------- frame update

	void Tears::Update(float a_delta, bool a_paused)
	{
		const Settings       settings = Settings::Snapshot();
		std::vector<Request> requests;
		{
			std::scoped_lock guard(_lock);
			requests.swap(_pending);
		}
		if (!settings.enabled) {
			for (auto& [key, crier] : _criers) {
				if (crier->crying) {
					SendEvent("Lacrima_CryStop", crier->actor, 0.0f);
				}
				for (auto& cheek : crier->cheeks) {
					Release(cheek);
				}
			}
			_criers.clear();
			_order.clear();
			Face::Reset();  // puts back eyes left glistening; BeginFrame, which would lapse them, does not run while off
			{
				std::scoped_lock guard(_lock);
				_states.clear();
			}
			RefreshStatus(settings);
			return;
		}
		for (const auto& request : requests) {
			Apply(request, settings);
		}

		RE::ActorHandle watch;
		if (settings.showDucts) {
			const auto selected = RE::Console::GetSelectedRef();
			auto*      actor = selected ? selected->As<RE::Actor>() : nullptr;
			if (!actor) {
				actor = RE::PlayerCharacter::GetSingleton();
			}
			if (actor && actor->Get3D(false)) {
				watch = actor->GetHandle();
				CrierFor(watch, settings, true, false);
			}
		}
		for (auto& [key, crier] : _criers) {
			crier->watched = watch && crier->actor == watch;
		}

		if (a_paused || a_delta <= 0.0f) {
			Publish();
			RefreshStatus(settings);
			return;
		}
		a_delta = std::min(a_delta, 0.05f);
		_cloneBudget = kClonesPerFrame;
		Face::BeginFrame(a_delta);

		for (auto it = _criers.begin(); it != _criers.end();) {
			auto&      crier = *it->second;
			const auto actor = crier.actor.get();
			auto*      root = actor ? actor->Get3D(false) : nullptr;
			auto*      cell = actor ? actor->GetParentCell() : nullptr;
			if (!root || !cell || !cell->IsAttached()) {
				if (crier.crying) {
					SendEvent("Lacrima_CryStop", crier.actor, 0.0f);
				}
				for (auto& cheek : crier.cheeks) {
					Release(cheek);
				}
				std::erase(_order, it->first);
				it = _criers.erase(it);
				continue;
			}

			if (crier.crying && !crier.forever) {
				crier.timeLeft -= a_delta;
				if (crier.timeLeft <= 0.0f) {
					StopCrying(crier);
				}
			}
			{
				const auto over = OverrideOf(it->first);
				crier.streams = std::clamp(over.streams > 0 ? over.streams : settings.streams, 1, static_cast<int>(Face::kMaxStreams));
				crier.mascara = std::clamp(over.mascara >= 0.0f ? over.mascara : settings.mascara, 0.0f, 1.0f);
			}
			const auto placement = PlacementFor(it->first, settings);
			const float goal = crier.crying ? crier.target : 0.0f;
			const float step = a_delta / kRamp;
			crier.intensity = crier.intensity < goal ? std::min(goal, crier.intensity + step) : std::max(goal, crier.intensity - step);
			if (settings.eyeShine > 1.0f && crier.intensity > 0.001f) {
				Face::Glisten(actor.get(), 1.0f + (settings.eyeShine - 1.0f) * crier.intensity);  // lapses by itself once this stops
			}

			// Between tears a cheek only counts down; the face is read when there is
			// fluid to simulate, a tear to start, a marker to draw or a gesture to watch.
			bool busy = crier.watched || crier.wipeArm > 0.0f;
			for (auto& cheek : crier.cheeks) {
				const bool due = crier.crying && cheek.sheds && crier.intensity > 0.01f;
				if (!cheek.welling && cheek.queued == 0 && due) {
					cheek.wait -= a_delta;
				}
				busy = busy || !cheek.particles.empty() || !cheek.stains.empty() || cheek.welling || cheek.queued > 0 || (due && cheek.wait <= 0.0f);
			}
			if (!busy) {
				for (auto& cheek : crier.cheeks) {
					if (cheek.root) {
						Release(cheek);
					}
				}
				if (Idle(crier)) {
					std::erase(_order, it->first);
					it = _criers.erase(it);
				} else {
					++it;
				}
				continue;
			}

			Bones(crier, root);
			Locate(crier, root);
			const bool frozen = Frozen(root, settings);
			if (frozen != crier.frozen) {
				crier.frozen = frozen;
				for (auto& cheek : crier.cheeks) {
					cheek.dirty = true;
				}
			}
			if (frozen) {
				// Out of sight: the tears stay where they are on the head, the trails
				// still dry up, and a gesture just runs out.
				crier.hands.clear();
				crier.handPrevious.clear();
				if (crier.wipeArm > 0.0f) {
					crier.wipeArm -= a_delta;
					if (crier.wipeArm <= 0.0f) {
						DryFace(crier);
						crier.wipeArm = 0.0f;
					}
				}
				for (auto& cheek : crier.cheeks) {
					cheek.capsules.clear();
					Expire(cheek, crier.frame, settings, a_delta, crier.mascara);
					if (cheek.particles.empty() && cheek.stains.empty() && cheek.root) {
						Release(cheek);
					} else if (cheek.root) {
						// Under the cell node nothing hides the tears with the actor (first
						// person) or carries them along; Attach shows them again.
						cheek.root->SetAppCulled(true);
					}
				}
				if (Idle(crier)) {
					std::erase(_order, it->first);
					it = _criers.erase(it);
					continue;
				}
				++it;
				continue;
			}

			auto       region = Bounds(crier, true, kSkinMargin);
			const auto underTears = Bounds(crier, false, kWatchMargin);
			auto       view = Face::Of(actor.get(), placement, region ? &*region : nullptr, underTears ? &*underTears : nullptr);
			std::array<Spots, Face::kEyes> spots{};
			if (view) {
				for (std::size_t e = 0; e < Face::kEyes; ++e) {
					for (std::size_t s = 0; s < Face::kMaxStreams; ++s) {
						if (view->ducts[e][s].valid) {
							spots[e][s] = SpotOf(crier.frame, view->ducts[e][s]);
						}
					}
				}
			}
			if (settings.hands || crier.wipeArm > 0.0f) {
				PoseHands(crier, a_delta);
			} else {
				crier.hands.clear();
				crier.handPrevious.clear();
			}
			Gesture(crier, spots, view ? view->scale : crier.frame.scale, settings, a_delta);
			const auto  hands = settings.hands ? std::span<const Fluid::Capsule>(crier.hands) : std::span<const Fluid::Capsule>{};
			const Mount mount{ settings.mountWorld, &crier.frame };
			// Wake before the face is final: tears that wake need skin under them in the
			// same step, and with every tear asleep only the ducts' surroundings were posed.
			bool woke = false;
			for (auto& cheek : crier.cheeks) {
				woke = Wake(cheek, crier.frame, view && view->morphing, hands) || woke;
			}
			if (woke && view) {
				region = Bounds(crier, true, kSkinMargin);
				view = Face::Of(actor.get(), placement, region ? &*region : nullptr, underTears ? &*underTears : nullptr);
			}
			for (std::size_t e = 0; e < Face::kEyes; ++e) {
				auto&      cheek = crier.cheeks[e];
				const bool any = std::ranges::any_of(spots[e], [](const std::optional<Spot>& a_spot) { return a_spot.has_value(); });
				if (!Prepare(cheek, settings)) {
					continue;
				}
				if (any) {
					Emit(crier, cheek, spots[e], settings, a_delta);
				} else if (cheek.welling || cheek.queued > 0) {
					std::erase_if(cheek.anchors, [&](const Fluid::Anchor& a_anchor) { return a_anchor.owner == cheek.id; });
					cheek.welling = false;
					cheek.left = 0;
					cheek.queued = 0;
					cheek.dirty = true;
				}
				try {
					Simulate(cheek, crier.frame, view, hands, settings, a_delta, crier.mascara);
					Expire(cheek, crier.frame, settings, a_delta, crier.mascara);
				} catch (const std::exception& error) {
					logger::error("{}: tears on one cheek cleared: {}", NameOf(actor.get()), error.what());
					Clear(cheek);
				}
				const bool marker = crier.watched && any;
				if (cheek.particles.empty() && cheek.stains.empty() && !marker) {
					if (cheek.root) {
						Release(cheek);
					}
					continue;
				}
				if (Attach(cheek, crier, root, mount.world)) {
					Draw(cheek, mount, settings, spots[e], marker);
				}
			}

			if (Idle(crier)) {
				for (auto& cheek : crier.cheeks) {
					Release(cheek);
				}
				std::erase(_order, it->first);
				it = _criers.erase(it);
				continue;
			}
			++it;
		}

		Publish();
		RefreshStatus(settings);

		if (settings.debugLog) {
			_statTime += a_delta;
			if (_statTime >= 10.0f) {
				_statTime = 0.0f;
				for (const auto& [key, crier] : _criers) {
					if (!crier->crying && !crier->watched) {
						continue;
					}
					const auto actor = crier->actor.get();
					logger::info("{}", Describe(actor.get(), crier.get(), settings));
				}
			}
		}
	}

	void Tears::Publish()
	{
		std::scoped_lock guard(_lock);
		std::unordered_set<std::uint32_t> pending;
		for (const auto& request : _pending) {
			pending.insert(request.actor.native_handle());
		}
		std::erase_if(_states, [&](const auto& a_entry) { return !_criers.contains(a_entry.first) && !pending.contains(a_entry.first); });
		for (const auto& [key, crier] : _criers) {
			auto& state = _states[key];
			state.crying = crier->crying;
			state.intensity = crier->crying ? crier->target : 0.0f;
			state.timeLeft = crier->crying ? (crier->forever ? -1.0f : std::max(crier->timeLeft, 0.0f)) : 0.0f;
		}
	}

	// --------------------------------------------------------------- head space

	void Tears::Bones(Crier& a_crier, RE::NiAVObject* a_root)
	{
		if (a_crier.skeleton.get() == a_root && a_crier.handBones.size() == kHandBones.size()) {
			return;
		}
		a_crier.skeleton.reset(a_root);
		auto* head = a_root->GetObjectByName(kHeadBone);
		a_crier.head.reset(head ? head->AsNode() : nullptr);
		if (Settings::Snapshot().debugLog) {
			const auto& r = a_root->world.translate;
			if (head) {
				const auto& h = head->world.translate;
				const char* parent = head->parent && head->parent->name.c_str() ? head->parent->name.c_str() : "?";
				logger::info("head bone \"{}\" under \"{}\" at world ({:.1f}, {:.1f}, {:.1f}), scale {:.3f}; actor 3D root at ({:.1f}, {:.1f}, {:.1f}); head is {:.1f} units above the root",
					kHeadBone, parent, h.x, h.y, h.z, head->world.scale, r.x, r.y, r.z, h.z - r.z);
			} else {
				logger::info("no \"{}\" under the actor 3D root at ({:.1f}, {:.1f}, {:.1f})", kHeadBone, r.x, r.y, r.z);
			}
		}
		a_crier.handBones.clear();
		for (const char* name : kHandBones) {
			a_crier.handBones.emplace_back(a_root->GetObjectByName(name));
		}
		a_crier.hands.clear();
		a_crier.handPrevious.clear();
		for (auto& cheek : a_crier.cheeks) {
			cheek.rested = false;
			cheek.dirty = true;
		}
	}

	void Tears::Locate(Crier& a_crier, RE::NiAVObject* a_root)
	{
		const RE::NiAVObject* anchor = a_crier.head ? static_cast<RE::NiAVObject*>(a_crier.head.get()) : a_root;
		auto&                 frame = a_crier.frame;
		frame.origin = anchor->world.translate;
		frame.rotate = anchor->world.rotate;
		frame.inverse = anchor->world.rotate.Transpose();
		frame.scale = std::isfinite(anchor->world.scale) ? std::max(anchor->world.scale, kMinScale) : 1.0f;
		frame.unit = kUnitsPerMeter * frame.scale;
		frame.down = Fluid::Normalize(ToVec(frame.inverse * RE::NiPoint3{ 0.0f, 0.0f, -1.0f }), { 0.0f, 0.0f, -1.0f });
	}

	bool Tears::Frozen(RE::NiAVObject* a_root, const Settings& a_settings) const
	{
		if (a_root->GetAppCulled()) {
			return true;
		}
		const auto* camera = RE::PlayerCamera::GetSingleton();
		const auto* eye = camera ? camera->cameraRoot.get() : nullptr;
		return eye && eye->world.translate.GetDistance(a_root->world.translate) > a_settings.maxDistance;
	}

	std::optional<Face::Region> Tears::Bounds(const Crier& a_crier, bool a_awakeOnly, float a_margin) const
	{
		// Where the face must be posed: around every tear, as long as any tear is
		// awake. Sleeping tears do not collide, so a face with only those needs no
		// skin beyond the ducts' surroundings, which Face poses anyway.
		bool awake = false;
		bool any = false;
		Vec3 low{};
		Vec3 high{};
		for (const auto& cheek : a_crier.cheeks) {
			for (const auto& particle : cheek.particles) {
				awake = awake || !particle.asleep;
				if (!any) {
					low = high = particle.position;
					any = true;
				} else {
					low = { std::min(low.x, particle.position.x), std::min(low.y, particle.position.y), std::min(low.z, particle.position.z) };
					high = { std::max(high.x, particle.position.x), std::max(high.y, particle.position.y), std::max(high.z, particle.position.z) };
				}
			}
		}
		if (!any || (a_awakeOnly && !awake)) {
			return std::nullopt;
		}
		low = low - Vec3{ a_margin, a_margin, a_margin };
		high = high + Vec3{ a_margin, a_margin, a_margin };
		Face::Region region{ ToWorld(a_crier.frame, low), ToWorld(a_crier.frame, low) };
		for (int c = 1; c < 8; ++c) {
			const Vec3         corner{ c & 1 ? high.x : low.x, c & 2 ? high.y : low.y, c & 4 ? high.z : low.z };
			const RE::NiPoint3 p = ToWorld(a_crier.frame, corner);
			region.low = { std::min(region.low.x, p.x), std::min(region.low.y, p.y), std::min(region.low.z, p.z) };
			region.high = { std::max(region.high.x, p.x), std::max(region.high.y, p.y), std::max(region.high.z, p.z) };
		}
		return region;
	}

	Tears::Spot Tears::SpotOf(const Frame& a_frame, const Face::Duct& a_duct)
	{
		return { ToLocal(a_frame, a_duct.point), ToLocalDirection(a_frame, a_duct.normal), ToLocalDirection(a_frame, a_duct.down) };
	}

	Vec3 Tears::ToLocal(const Frame& a_frame, const RE::NiPoint3& a_point)
	{
		return ToVec((a_frame.inverse * (a_point - a_frame.origin)) / a_frame.unit);
	}

	Vec3 Tears::ToLocalDirection(const Frame& a_frame, const RE::NiPoint3& a_direction)
	{
		return Fluid::Normalize(ToVec(a_frame.inverse * a_direction), { 0.0f, 0.0f, 1.0f });
	}

	RE::NiPoint3 Tears::ToWorld(const Frame& a_frame, Vec3 a_point)
	{
		return a_frame.origin + a_frame.rotate * (ToPoint(a_point) * a_frame.unit);
	}

	// Node-local units under the head bone, whose own scale supplies the rest.
	RE::NiPoint3 Tears::ToBone(Vec3 a_point)
	{
		return ToPoint(a_point) * kUnitsPerMeter;
	}

	// -------------------------------------------------------------------- ducts

	bool Tears::Prepare(Cheek& a_cheek, const Settings& a_settings)
	{
		const float spacing = kBaseSpacing * a_settings.size;
		if (a_cheek.solver && a_cheek.spacing == spacing) {
			return true;
		}
		try {
			a_cheek.solver = std::make_unique<Fluid::Solver>(spacing, Fluid::Parameters{});
			a_cheek.spacing = spacing;
		} catch (const std::exception& error) {
			logger::error("tear solver: {}", error.what());
			return false;
		}
		return true;
	}

	void Tears::StartTear(Cheek& a_cheek, float a_strength, int a_streams, const Settings& a_settings)
	{
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const float                           size = static_cast<float>(a_settings.drop) * (0.5f + 0.5f * a_strength) * (0.75f + 0.5f * unit(_rng));
		a_cheek.run = ++_nextRun;
		// each tear picks one of the eye's streams
		const int streams = std::clamp(a_streams, 1, static_cast<int>(Face::kMaxStreams));
		a_cheek.stream = streams > 1 ? std::uniform_int_distribution<int>(0, streams - 1)(_rng) : 0;
		a_cheek.left = std::max(2, static_cast<int>(std::lround(size)));
		a_cheek.interval = a_settings.well / static_cast<float>(a_cheek.left);
		a_cheek.slot = 0.0f;
		a_cheek.hold = a_settings.hold * (0.6f + 0.8f * unit(_rng));
		a_cheek.welling = true;
	}

	void Tears::Emit(Crier& a_crier, Cheek& a_cheek, const Spots& a_spots, const Settings& a_settings, float a_delta)
	{
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const bool                            started = !a_cheek.welling;
		if (!a_cheek.welling) {
			if (a_cheek.queued > 0) {
				--a_cheek.queued;
				StartTear(a_cheek, std::max(a_crier.intensity, 0.6f), a_crier.streams, a_settings);
			} else if (a_crier.crying && a_cheek.sheds && a_crier.intensity > 0.01f && a_cheek.wait <= 0.0f) {
				StartTear(a_cheek, a_crier.intensity, a_crier.streams, a_settings);
			}
			if (!a_cheek.welling) {
				return;
			}
		}
		// The duct of the stream this tear runs from, or another one that was found.
		const Spot* chosen = a_spots[static_cast<std::size_t>(std::clamp(a_cheek.stream, 0, static_cast<int>(Face::kMaxStreams) - 1))] ?
		                         &*a_spots[static_cast<std::size_t>(a_cheek.stream)] : nullptr;
		for (const auto& candidate : a_spots) {
			if (!chosen && candidate) {
				chosen = &*candidate;
			}
		}
		if (!chosen) {
			return;
		}
		const Spot& a_spot = *chosen;
		if (started) {
			if (a_settings.debugLog) {
				const auto& frame = a_crier.frame;
				logger::info("{}: a tear of {} drops starts at head-space ({:.3f}, {:.3f}, {:.3f}) m, normal ({:.2f}, {:.2f}, {:.2f}), down ({:.2f}, {:.2f}, {:.2f}); "
				             "frame scale {:.3f}, unit {:.1f}, gravity down ({:.2f}, {:.2f}, {:.2f})",
					NameOf(a_crier.actor.get().get()), a_cheek.left, a_spot.point.x, a_spot.point.y, a_spot.point.z, a_spot.normal.x, a_spot.normal.y,
					a_spot.normal.z, a_spot.down.x, a_spot.down.y, a_spot.down.z, frame.scale, frame.unit, frame.down.x, frame.down.y, frame.down.z);
				a_cheek.probe = true;
			}
		}

		const float spacing = a_cheek.spacing;
		const float lift = 0.6f * spacing;
		const Vec3  side = Fluid::Normalize(Fluid::Cross(a_spot.normal, a_spot.down), { 1.0f, 0.0f, 0.0f });
		const Vec3  seat = a_spot.point + a_spot.normal * lift;
		auto&       particles = a_cheek.particles;
		bool        born = false;
		a_cheek.slot -= a_delta;
		while (a_cheek.left > 0 && a_cheek.slot <= 0.0f) {
			a_cheek.slot += a_cheek.interval;
			--a_cheek.left;
			if (particles.size() >= static_cast<std::size_t>(a_settings.maxParticles)) {
				continue;
			}
			const Vec3      jitter = (side * (unit(_rng) - 0.5f) + a_spot.down * (unit(_rng) - 0.5f)) * (0.6f * spacing);
			Fluid::Particle particle;
			particle.life = a_settings.lifetime * (0.85f + 0.3f * unit(_rng));
			particle.position = seat + jitter;
			particle.velocity = a_spot.down * kWellSpeed;
			particle.run = a_cheek.run;
			particle.stream = static_cast<std::uint8_t>(std::clamp(a_cheek.stream, 0, static_cast<int>(Face::kMaxStreams) - 1));
			// Born on the lid: the solver's stick-pull only reaches for particles already
			// touching, and a tear that starts free falls off the face before it lands.
			particle.touching = true;
			if (!particles.empty() && particles.back().run == a_cheek.run) {
				const float gap = Fluid::Length(particle.position - particles.back().position);
				if (gap < 2.0f * spacing) {
					particles.back().link = std::max(kLinkSpacing * spacing, gap);
				}
			}
			// Every welling particle hangs from the lid on its own leash, so the bead
			// gathers there instead of running off as soon as it is born; the leashes
			// go when the tear is released.
			const float rest = std::max(Fluid::Length(particle.position - seat), 0.5f * spacing);
			a_cheek.anchors.push_back({ particles.size(), seat, rest, a_cheek.id });
			particles.push_back(particle);
			born = true;
			++a_cheek.emitted;
			a_cheek.dirty = true;
		}
		if (!born) {
			for (auto& anchor : a_cheek.anchors) {
				if (anchor.owner == a_cheek.id) {
					anchor.point = seat;  // the lid moves with the expression
				}
			}
		}
		if (a_cheek.left > 0) {
			return;
		}
		a_cheek.hold -= a_delta;
		if (a_cheek.hold > 0.0f) {
			return;
		}
		std::erase_if(a_cheek.anchors, [&](const Fluid::Anchor& a) { return a.owner == a_cheek.id; });
		for (auto& particle : particles) {
			if (particle.run == a_cheek.run) {
				particle.travel = 0.0f;  // the run starts here; the shuffling while welling does not count
			}
		}
		a_cheek.welling = false;
		const float rate = a_settings.rate * std::max(a_crier.intensity, 0.05f);
		a_cheek.wait = 60.0f / rate * (0.5f + unit(_rng));
	}

	// ---------------------------------------------------------------- simulation

	bool Tears::Wake(Cheek& a_cheek, const Frame& a_frame, bool a_morphing, std::span<const Fluid::Capsule> a_hands)
	{
		bool woke = false;
		if (!a_cheek.rested) {
			a_cheek.restDown = a_frame.down;
			a_cheek.rested = true;
		}
		// Sleeping tears rest on skin that stands still in head space. They wake when
		// the skin moves under them (an expression), when the head has tilted enough
		// for gravity to pull them a new way, or when a hand comes for them.
		const bool all = a_morphing || Fluid::Dot(a_cheek.restDown, a_frame.down) < kTiltWake;
		bool       asleep = false;
		for (auto& particle : a_cheek.particles) {
			if (!particle.asleep) {
				continue;
			}
			bool wake = all;
			for (std::size_t h = 0; h < a_hands.size() && !wake; ++h) {
				const auto& hand = a_hands[h];
				wake = SegmentDistance(particle.position, hand.a, hand.b) < hand.radius + 3.0f * a_cheek.spacing;
			}
			if (wake) {
				particle.asleep = false;
				particle.idle = 0.0f;
				a_cheek.dirty = true;
				woke = true;
			} else {
				asleep = true;
			}
		}
		if (!asleep) {
			a_cheek.restDown = a_frame.down;
		}
		return woke;
	}

	void Tears::Simulate(Cheek& a_cheek, const Frame& a_frame, const std::optional<Face::View>& a_view, std::span<const Fluid::Capsule> a_hands,
		const Settings& a_settings, float a_delta, float a_mascara)
	{
		a_cheek.capsules.assign(a_hands.begin(), a_hands.end());
		auto& particles = a_cheek.particles;
		if (particles.empty() || !a_cheek.solver) {
			a_cheek.accumulator = 0.0f;
			return;
		}
		bool awake = false;
		Vec3 low{};
		Vec3 high{};
		for (const auto& particle : particles) {
			if (particle.asleep) {
				continue;
			}
			if (!awake) {
				low = high = particle.position;
				awake = true;
			} else {
				low = { std::min(low.x, particle.position.x), std::min(low.y, particle.position.y), std::min(low.z, particle.position.z) };
				high = { std::max(high.x, particle.position.x), std::max(high.y, particle.position.y), std::max(high.z, particle.position.z) };
			}
		}
		if (!awake) {
			// every tear rests on the skin; nothing to step
			a_cheek.accumulator = 0.0f;
			a_cheek.lastSteps = 0;
			return;
		}
		a_cheek.accumulator = std::min(a_cheek.accumulator + a_delta, kMaxSteps * kStep);
		auto& patches = a_cheek.skin.Patches();
		patches.clear();
		if (a_view) {
			low = low - Vec3{ kSkinMargin, kSkinMargin, kSkinMargin };
			high = high + Vec3{ kSkinMargin, kSkinMargin, kSkinMargin };
			for (const auto& patch : a_view->patches) {
				const Vec3 position = ToLocal(a_frame, patch.position);
				if (position.x < low.x || position.y < low.y || position.z < low.z || position.x > high.x || position.y > high.y ||
					position.z > high.z) {
					continue;
				}
				patches.push_back({ position, ToLocalDirection(a_frame, patch.normal) });
			}
		}
		a_cheek.skin.Build(kSkinCell);
		a_cheek.solver->Configure(ParametersOf(a_settings, a_frame.scale, a_frame.down));
		const Fluid::Colliders colliders{ a_cheek.capsules, {}, &a_cheek.skin };
		int                    steps = 0;
		while (a_cheek.accumulator >= kStep && steps < kMaxSteps) {
			a_cheek.solver->Step(particles, a_cheek.anchors, colliders, kStep);
			a_cheek.accumulator -= kStep;
			++steps;
		}
		if (steps == kMaxSteps) {
			a_cheek.accumulator = 0.0f;
		}
		if (steps > 0) {
			a_cheek.dirty = true;
		}
		a_cheek.lastPosed = a_view ? a_view->patches.size() : 0;
		a_cheek.lastPatches = patches.size();
		a_cheek.lastSteps = steps;
		std::erase_if(a_cheek.anchors, [](const Fluid::Anchor& a_anchor) { return a_anchor.rest <= 0.0f; });
		if (steps > 0) {
			Deposit(a_cheek, a_frame, a_settings, a_mascara);
		}
		if (a_cheek.probe && steps > 0) {
			// One line per tear about its first solver step: is the face where the
			// particles are, and did they land on it?
			a_cheek.probe = false;
			const auto&       first = particles.back();
			const std::size_t nearest = a_cheek.skin.Nearest(first.position);
			std::string       nearby = "no patch within 1 cm";
			if (nearest != Fluid::kNone) {
				const auto& patch = patches[nearest];
				nearby = std::format("nearest patch {:.1f} mm away, depth {:.1f} mm along its normal ({:.2f}, {:.2f}, {:.2f})",
					Fluid::Length(first.position - patch.position) * 1000.0f, Fluid::Dot(first.position - patch.position, patch.normal) * 1000.0f,
					patch.normal.x, patch.normal.y, patch.normal.z);
			}
			std::size_t touching = 0;
			for (const auto& particle : particles) {
				touching += particle.touching ? 1 : 0;
			}
			const RE::NiPoint3 world = ToWorld(a_frame, first.position);
			logger::info("first step: {} of {} posed patches under the fluid, {} of {} particles touching; newest particle at head-space ({:.3f}, {:.3f}, {:.3f}) m = world ({:.1f}, {:.1f}, {:.1f}); {}; {} anchors",
				patches.size(), a_cheek.lastPosed, touching, particles.size(), first.position.x, first.position.y, first.position.z, world.x, world.y, world.z, nearby,
				a_cheek.anchors.size());
			if (!patches.empty()) {
				Vec3 lo = patches.front().position;
				Vec3 hi = lo;
				for (const auto& patch : patches) {
					lo = { std::min(lo.x, patch.position.x), std::min(lo.y, patch.position.y), std::min(lo.z, patch.position.z) };
					hi = { std::max(hi.x, patch.position.x), std::max(hi.y, patch.position.y), std::max(hi.z, patch.position.z) };
				}
				logger::info("first step: skin under the fluid spans head-space x [{:.3f}, {:.3f}] y [{:.3f}, {:.3f}] z [{:.3f}, {:.3f}] m", lo.x, hi.x, lo.y,
					hi.y, lo.z, hi.z);
			} else if (a_view && !a_view->patches.empty()) {
				const Vec3 sample = ToLocal(a_frame, a_view->patches.front().position);
				logger::info("first step: the posed face is elsewhere; its first patch is at head-space ({:.3f}, {:.3f}, {:.3f}) m = world ({:.1f}, {:.1f}, {:.1f})",
					sample.x, sample.y, sample.z, a_view->patches.front().position.x, a_view->patches.front().position.y, a_view->patches.front().position.z);
			}
		}
	}

	// A running tear leaves its trail behind: every trail_step of travel the hindmost
	// awake particle of a tear stays where it is, asleep, so the bead thins as it
	// runs and the last drop, alone, sticks. The link to the deposit stretches into
	// the trail the rope pass draws.
	void Tears::Deposit(Cheek& a_cheek, const Frame& a_frame, const Settings& a_settings, float a_mascara)
	{
		auto&       particles = a_cheek.particles;
		const float step = a_settings.trailStep * 0.01f;
		const Vec3  up = -a_frame.down;
		std::size_t begin = 0;
		while (begin < particles.size()) {
			std::size_t end = begin + 1;
			while (end < particles.size() && particles[end].run == particles[begin].run) {
				++end;
			}
			std::size_t awake = 0;
			std::size_t hindmost = Fluid::kNone;
			float       highest = -std::numeric_limits<float>::max();
			for (std::size_t i = begin; i < end; ++i) {
				const auto& particle = particles[i];
				if (particle.asleep || !particle.touching) {
					continue;
				}
				++awake;
				const float height = Fluid::Dot(particle.position, up);
				if (height > highest) {
					highest = height;
					hindmost = i;
				}
			}
			if (awake >= 2 && hindmost != Fluid::kNone && particles[hindmost].travel >= step) {
				auto& particle = particles[hindmost];
				particle.asleep = true;
				particle.velocity = {};
				particle.idle = 0.0f;
				particle.bulk = false;  // a drop on the trail is drawn as part of the trail, not as part of the bead
				particle.order = ++a_cheek.deposits;
				LayStain(a_cheek, particle, particle.order, a_settings, a_mascara);
				// the rest of the bead starts counting again: the next drop is left another
				// step further on, not on the next frame
				for (std::size_t i = begin; i < end; ++i) {
					particles[i].travel = 0.0f;
				}
				a_cheek.dirty = true;
			}
			begin = end;
		}
	}

	void Tears::Expire(Cheek& a_cheek, const Frame& a_frame, const Settings& a_settings, float a_delta, float a_mascara)
	{
		auto&      particles = a_cheek.particles;
		bool       expired = false;
		const auto touched = [&](Vec3 a_point) {
			return std::ranges::any_of(a_cheek.capsules, [&](const Fluid::Capsule& a_capsule) {
				const float reach = a_capsule.radius + a_cheek.spacing;
				return SegmentDistance(a_point, a_capsule.a, a_capsule.b) < reach;
			});
		};
		AgeStains(a_cheek, a_delta);
		if (!a_cheek.capsules.empty()) {
			// a hand passing over a mascara streak smears it
			for (auto& stain : a_cheek.stains) {
				if (!stain.smeared && touched(stain.position)) {
					stain.smeared = true;
					stain.life = std::min(stain.life, stain.lifetime * kSmearLife);
					a_cheek.dirty = true;
				}
			}
		}
		for (auto& particle : particles) {
			particle.life -= a_delta;
			if (particle.airborne > kFallGrace && Fluid::Dot(particle.velocity, a_frame.down) > kFallSpeed && particle.life > a_settings.fall) {
				particle.life = a_settings.fall;
				++a_cheek.cutFalling;
			}
			if (particle.life > a_settings.absorb && !a_cheek.capsules.empty() && touched(particle.position)) {
				particle.life = a_settings.absorb;
				particle.wiped = true;
				++a_cheek.cutByHand;
			}
			if (particle.life <= 0.0f) {
				expired = true;
				// what was left of a bead dried on the skin: its streak ends here
				if (particle.order == 0 && particle.touching && particle.run != 0 &&
					std::ranges::none_of(a_cheek.stains, [&](const Stain& a_stain) { return a_stain.run == particle.run && a_stain.order == kBeadOrder; })) {
					LayStain(a_cheek, particle, kBeadOrder, a_settings, a_mascara);
				}
			}
		}
		if (!expired) {
			return;
		}
		a_cheek.dirty = true;
		auto& remap = _remap;
		remap.assign(particles.size(), Fluid::kNone);
		std::size_t write = 0;
		bool        previousKept = false;
		for (std::size_t read = 0; read < particles.size(); ++read) {
			if (particles[read].life <= 0.0f) {
				if (previousKept) {
					particles[write - 1].link = 0.0f;
				}
				previousKept = false;
				continue;
			}
			remap[read] = write;
			particles[write++] = particles[read];
			previousKept = true;
		}
		particles.resize(write);
		a_cheek.expired += static_cast<std::uint32_t>(remap.size() - write);
		for (auto& blob : a_cheek.blobs) {
			if (blob.live) {
				blob.seed = blob.seed < remap.size() ? remap[blob.seed] : Fluid::kNone;
				blob.live = blob.seed != Fluid::kNone;
			}
		}
		std::erase_if(a_cheek.anchors, [&](Fluid::Anchor& a_anchor) {
			a_anchor.particle = a_anchor.particle < remap.size() ? remap[a_anchor.particle] : Fluid::kNone;
			return a_anchor.particle == Fluid::kNone;
		});
	}

	void Tears::LayStain(Cheek& a_cheek, const Fluid::Particle& a_particle, std::uint32_t a_order, const Settings& a_settings, float a_mascara)
	{
		if (a_mascara <= 0.0f || _meshes.stainFailed) {
			return;
		}
		auto& stains = a_cheek.stains;
		// Too many streaks on this cheek: the oldest one goes.
		std::size_t runs = 0;
		std::uint32_t oldest = std::numeric_limits<std::uint32_t>::max();
		for (std::size_t i = 0; i < stains.size(); ++i) {
			const bool first = std::ranges::none_of(stains.begin(), stains.begin() + static_cast<std::ptrdiff_t>(i),
				[&](const Stain& a_other) { return a_other.run == stains[i].run; });
			if (first) {
				++runs;
				oldest = std::min(oldest, stains[i].run);
			}
		}
		const bool newRun = std::ranges::none_of(stains, [&](const Stain& a_stain) { return a_stain.run == a_particle.run; });
		if ((newRun && runs >= kMaxStainRuns) || stains.size() >= kMaxStains) {
			std::erase_if(stains, [&](const Stain& a_stain) { return a_stain.run == oldest; });
		}
		Stain stain;
		stain.position = OntoSkin(a_cheek.skin, a_particle.position);
		stain.lifetime = a_settings.mascaraLifetime;
		stain.strength = a_mascara;
		stain.run = a_particle.run;
		stain.order = a_order;
		stain.stream = a_particle.stream;
		stain.smeared = a_particle.wiped;
		stain.life = stain.smeared ? stain.lifetime * kSmearLife : stain.lifetime;
		stains.push_back(stain);
		a_cheek.dirty = true;
	}

	void Tears::AgeStains(Cheek& a_cheek, float a_delta)
	{
		bool fading = false;
		for (auto& stain : a_cheek.stains) {
			stain.life -= a_delta;
			fading = fading || stain.life < stain.lifetime * kStainFade;
		}
		if (std::erase_if(a_cheek.stains, [](const Stain& a_stain) { return a_stain.life <= 0.0f; }) > 0 || fading) {
			a_cheek.dirty = true;  // a streak fading out thins every frame
		}
	}

	void Tears::PoseHands(Crier& a_crier, float a_delta)
	{
		const auto& frame = a_crier.frame;
		auto&       next = a_crier.hands;
		next.clear();
		const auto local = [&](const RE::NiPoint3& a_point) { return ToLocal(frame, a_point); };
		for (std::size_t side = 0; side < 2; ++side) {
			const auto bone = [&](std::size_t a_index) { return a_crier.handBones[side * kSideBones + a_index].get(); };
			auto*      hand = bone(0);
			auto*      middle = bone(1 + 2 * 3);
			if (hand && middle) {
				next.push_back({ local(hand->world.translate), local(middle->world.translate), kPalmRadius * hand->world.scale / frame.unit });
			}
			for (std::size_t chain = 0; chain < 4; ++chain) {
				auto* base = bone(1 + chain * 3);
				auto* mid = bone(2 + chain * 3);
				auto* end = bone(3 + chain * 3);
				if (!base || !mid || !end) {
					continue;
				}
				const float        radius = kFingerRadius * base->world.scale / frame.unit;
				const RE::NiPoint3 tip = end->world.translate + (end->world.translate - mid->world.translate) * kTipExtension;
				next.push_back({ local(base->world.translate), local(mid->world.translate), radius });
				next.push_back({ local(mid->world.translate), local(end->world.translate), radius });
				next.push_back({ local(end->world.translate), local(tip), radius });
			}
		}
		// Velocities relative to the head, which is what the tears feel.
		const bool  moving = a_crier.handPrevious.size() == next.size() * 2 && a_delta > 0.0f;
		const float cap = kMaxHandSpeed / frame.unit;
		a_crier.handPrevious.resize(next.size() * 2);
		for (std::size_t k = 0; k < next.size(); ++k) {
			auto& piece = next[k];
			if (moving) {
				piece.velocityA = (piece.a - a_crier.handPrevious[2 * k]) * (1.0f / a_delta);
				piece.velocityB = (piece.b - a_crier.handPrevious[2 * k + 1]) * (1.0f / a_delta);
				if (Fluid::Length(piece.velocityA) > cap || Fluid::Length(piece.velocityB) > cap) {
					piece.velocityA = piece.velocityB = {};
				}
			}
			a_crier.handPrevious[2 * k] = piece.a;
			a_crier.handPrevious[2 * k + 1] = piece.b;
		}
	}

	void Tears::Gesture(Crier& a_crier, const std::array<Spots, Face::kEyes>& a_spots, float a_scale, const Settings& a_settings, float a_delta)
	{
		if (a_crier.wipeArm <= 0.0f) {
			return;
		}
		a_crier.wipeArm -= a_delta;
		bool        reached = false;
		const float reach = a_settings.reach * a_scale / a_crier.frame.unit;
		for (const auto& eye : a_spots) {
			for (const auto& spot : eye) {
				if (!spot) {
					continue;
				}
				for (const auto& hand : a_crier.hands) {
					reached = reached || SegmentDistance(spot->point, hand.a, hand.b) - hand.radius < reach;
				}
			}
		}
		if (reached || a_crier.wipeArm <= 0.0f) {
			DryFace(a_crier);
			a_crier.wipeArm = 0.0f;
		}
	}

	void Tears::DryFace(Crier& a_crier)
	{
		for (auto& cheek : a_crier.cheeks) {
			for (auto& particle : cheek.particles) {
				particle.life = std::min(particle.life, kWipeFade);
			}
			std::erase_if(cheek.anchors, [&](const Fluid::Anchor& a_anchor) { return a_anchor.owner == cheek.id; });
			cheek.welling = false;
			cheek.left = 0;
			cheek.queued = 0;
			cheek.wait = std::max(cheek.wait, kWipeRest);
			cheek.dirty = true;
			// wiping by hand smears the mascara rather than taking it off
			for (auto& stain : cheek.stains) {
				stain.smeared = true;
				stain.life = std::min(stain.life, stain.lifetime * kSmearLife);
			}
		}
	}

	void Tears::Clear(Cheek& a_cheek)
	{
		a_cheek.particles.clear();
		a_cheek.stains.clear();  // a clean face: the mascara goes with the tears
		a_cheek.anchors.clear();
		a_cheek.welling = false;
		a_cheek.left = 0;
		a_cheek.queued = 0;
		a_cheek.accumulator = 0.0f;
		a_cheek.rested = false;
		a_cheek.dirty = true;
	}

	// ----------------------------------------------------------------- rendering

	bool Tears::Attach(Cheek& a_cheek, const Crier& a_crier, RE::NiAVObject* a_root, bool a_world)
	{
		// The tears hang under the head bone, so the engine carries them with the
		// head; their nodes are posed in head space and need no world transform. The
		// world mount (A/B) hangs them under the cell node with the inverse of its
		// transform, so their nodes are posed in world units, as 0.1.0 did.
		RE::NiNode* parent = a_world ? a_root->parent : (a_crier.head ? a_crier.head.get() : a_root->AsNode());
		if (!parent) {
			return false;
		}
		if (!a_cheek.root) {
			a_cheek.root.reset(RE::NiNode::Create(0));
			if (!a_cheek.root) {
				return false;
			}
			a_cheek.root->name = "LacrimaTears";
			a_cheek.root->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		}
		// Under the head bone the selective-update flags let the skeleton's own
		// per-frame update reach this subtree, so settled tears that skip Draw still
		// ride the head. Under the cell node the root stays as plain as 0.1.0's.
		if (a_world) {
			a_cheek.root->GetFlags().reset(RE::NiAVObject::Flag::kSelectiveUpdate, RE::NiAVObject::Flag::kSelectiveUpdateTransforms,
				RE::NiAVObject::Flag::kSelectiveUpdateController);
		} else {
			a_cheek.root->GetFlags().set(RE::NiAVObject::Flag::kSelectiveUpdate, RE::NiAVObject::Flag::kSelectiveUpdateTransforms,
				RE::NiAVObject::Flag::kSelectiveUpdateController);
		}
		if (a_cheek.root->parent != parent) {
			if (a_cheek.root->parent) {
				a_cheek.root->parent->DetachChild2(a_cheek.root.get());
			}
			parent->AttachChild(a_cheek.root.get(), true);
			a_cheek.dirty = true;
			// Bead bones posed in the other mount's units: no bead may keep its pose.
			for (auto& blob : a_cheek.blobs) {
				blob.live = false;
			}
			if (Settings::Snapshot().debugLog) {
				const char* name = parent->name.c_str();
				logger::info("tear root attached under \"{}\" ({} mount), actor 3D {}culled", name && *name ? name : "(unnamed)",
					a_world ? "world" : "head bone", a_root->GetAppCulled() ? "" : "not ");
			}
		}
		a_cheek.root->local = a_world ? parent->world.Invert() : RE::NiTransform{};
		a_cheek.root->SetAppCulled(a_root->GetAppCulled());
		return true;
	}

	void Tears::Release(Cheek& a_cheek)
	{
		if (a_cheek.root && a_cheek.root->parent) {
			a_cheek.root->parent->DetachChild2(a_cheek.root.get());
		}
		if (a_cheek.root) {
			for (auto& blob : a_cheek.blobs) {
				if (!blob.root || _spareBlobs.size() >= kMaxBlobs * 2) {
					continue;
				}
				a_cheek.root->DetachChild2(blob.root.get());
				blob.root->SetAppCulled(true);
				blob.used = false;
				blob.live = false;
				blob.surface.clear();
				_spareBlobs.push_back(std::move(blob));
			}
			for (auto& rope : a_cheek.ropes) {
				if (!rope.root || _spareRopes.size() >= kMaxRopes * 2) {
					continue;
				}
				a_cheek.root->DetachChild2(rope.root.get());
				rope.root->SetAppCulled(true);
				rope.used = false;
				_spareRopes.push_back(std::move(rope));
			}
			for (auto& rope : a_cheek.stainRopes) {
				if (!rope.root || _spareStains.size() >= kMaxStainRuns * 2) {
					continue;
				}
				a_cheek.root->DetachChild2(rope.root.get());
				rope.root->SetAppCulled(true);
				rope.used = false;
				_spareStains.push_back(std::move(rope));
			}
		}
		a_cheek.blobs.clear();
		a_cheek.ropes.clear();
		a_cheek.stainRopes.clear();
		a_cheek.root.reset();
		a_cheek.opacity = a_cheek.shine = a_cheek.gloss = a_cheek.stainOpacity = -1.0f;
		Clear(a_cheek);
	}

	Tears::Meshes& Tears::MeshesFor()
	{
		auto& meshes = _meshes;
		if (meshes.failed || (meshes.blob && meshes.rope)) {
			return meshes;
		}
		RE::BSModelDB::DBTraits::ArgsType args{};
		for (auto [path, target] : { std::pair{ kBlobPath, &meshes.blob }, std::pair{ kRopePath, &meshes.rope } }) {
			const auto error = RE::BSModelDB::Demand(path, *target, args);
			if (error != RE::BSResource::ErrorCode::kNone || !*target) {
				logger::error("could not load meshes\\{} (error {}); tears stay invisible", path, std::to_underlying(error));
				meshes.failed = true;
				return meshes;
			}
		}
		if (_directions.empty()) {
			for (std::size_t i = 0;; ++i) {
				auto* bone = meshes.blob->GetObjectByName(std::format("Blob{:02}", i));
				if (!bone) {
					break;
				}
				_directions.push_back(Fluid::Normalize(ToVec(bone->local.translate), { 0.0f, 0.0f, 1.0f }));
			}
			_scratch.resize(_directions.size());
		}
		if (_directions.size() < 12 || !meshes.rope->GetObjectByName(std::format("Rope{:02}", kRopeBones - 1))) {
			logger::error("the tear meshes are from another version; reinstall Lacrima");
			meshes.failed = true;
			return meshes;
		}
		// The mascara streak mesh is optional: without it tears still show.
		const auto error = RE::BSModelDB::Demand(kStainPath, meshes.stain, args);
		if (error != RE::BSResource::ErrorCode::kNone || !meshes.stain ||
			!meshes.stain->GetObjectByName(std::format("Rope{:02}", kRopeBones - 1))) {
			logger::warn("could not load meshes\\{} (error {}); no running mascara", kStainPath, std::to_underlying(error));
			meshes.stain.reset();
			meshes.stainFailed = true;
		}
		return meshes;
	}

	RE::NiPointer<RE::BSDismemberSkinInstance> Tears::DismemberOf(RE::NiNode* a_root, const RE::BSFixedString& a_shape)
	{
		auto* object = a_root ? a_root->GetObjectByName(a_shape) : nullptr;
		auto* geometry = object ? object->AsGeometry() : nullptr;
		auto* skin = geometry ? geometry->GetGeometryRuntimeData().skinInstance.get() : nullptr;
		return RE::NiPointer<RE::BSDismemberSkinInstance>(skin ? netimmerse_cast<RE::BSDismemberSkinInstance*>(skin) : nullptr);
	}

	// Under an actor's 3D the game hides every dismember partition whose body-part
	// slot an equipped item covers. The shipped meshes carry no partitions since
	// 0.1.1; a mesh that still does (0.1.0 left installed) is shown again each draw.
	void Tears::Reveal(RE::BSDismemberSkinInstance* a_skin)
	{
		if (!a_skin) {
			return;
		}
		const auto& data = a_skin->GetRuntimeData();
		if (!data.partitions) {
			return;
		}
		for (std::int32_t i = 0; i < data.numPartitions; ++i) {
			if (!data.partitions[i].visible) {
				a_skin->UpdateDismemberPartion(data.partitions[i].slot, true);
			}
		}
	}

	bool Tears::CloneBlob(Blob& a_blob)
	{
		auto& meshes = MeshesFor();
		auto* clone = meshes.failed ? nullptr : meshes.blob->Clone();
		auto* node = clone ? clone->AsNode() : nullptr;
		if (!node) {
			return false;
		}
		a_blob.root.reset(node);
		a_blob.bones.clear();
		for (std::size_t i = 0; i < _directions.size(); ++i) {
			auto* bone = node->GetObjectByName(std::format("Blob{:02}", i));
			if (!bone) {
				return false;
			}
			a_blob.bones.push_back(bone);
		}
		node->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		if (auto* shape = node->GetObjectByName(MeshNames::Get().blobShape)) {
			shape->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		}
		a_blob.dismember = DismemberOf(node, MeshNames::Get().blobShape);
		return true;
	}

	bool Tears::CloneRope(Rope& a_rope, RE::NiNode* a_mesh, const RE::BSFixedString& a_shape)
	{
		auto* clone = a_mesh ? a_mesh->Clone() : nullptr;
		auto* node = clone ? clone->AsNode() : nullptr;
		if (!node) {
			return false;
		}
		a_rope.root.reset(node);
		for (std::size_t i = 0; i < kRopeBones; ++i) {
			a_rope.bones[i] = node->GetObjectByName(std::format("Rope{:02}", i));
			if (!a_rope.bones[i]) {
				return false;
			}
		}
		node->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		if (auto* shape = node->GetObjectByName(a_shape)) {
			shape->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		}
		a_rope.dismember = DismemberOf(node, a_shape);
		return true;
	}

	void Tears::Shade(RE::NiAVObject* a_root, const Settings& a_settings)
	{
		const auto& names = MeshNames::Get();
		for (const auto& name : { names.blobShape, names.ropeShape }) {
			auto* shape = a_root->GetObjectByName(name);
			auto* geometry = shape ? shape->AsGeometry() : nullptr;
			auto* shader = geometry ? geometry->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
			if (!shader) {
				continue;
			}
			shader->SetMaterialAlpha(a_settings.opacity);
			if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(shader)) {
				if (lighting->emissiveColor) {
					*lighting->emissiveColor = RE::NiColor(1.0f, 1.0f, 1.0f);
				}
				lighting->emissiveMult = a_settings.glow;
			}
			if (shader->GetMaterialType() == RE::BSShaderMaterial::Type::kLighting) {
				if (auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(shader->GetBaseMaterial())) {
					material->specularColorScale = a_settings.shine;
					material->specularPower = a_settings.gloss;
					if (material->GetFeature() == RE::BSShaderMaterial::Feature::kEnvironmentMap) {
						static_cast<RE::BSLightingShaderMaterialEnvmap*>(material)->envMapScale = a_settings.reflection;
					}
				}
			}
		}
	}

	void Tears::ShadeStain(RE::NiAVObject* a_root, const Settings& a_settings)
	{
		auto* shape = a_root->GetObjectByName(MeshNames::Get().stainShape);
		auto* geometry = shape ? shape->AsGeometry() : nullptr;
		if (auto* shader = geometry ? geometry->GetGeometryRuntimeData().shaderProperty.get() : nullptr) {
			shader->SetMaterialAlpha(a_settings.mascaraOpacity);
		}
	}

	void Tears::Reshade(Cheek& a_cheek, const Settings& a_settings)
	{
		if (a_cheek.stainOpacity != a_settings.mascaraOpacity) {
			for (auto& rope : a_cheek.stainRopes) {
				ShadeStain(rope.root.get(), a_settings);
			}
			a_cheek.stainOpacity = a_settings.mascaraOpacity;
		}
		if (a_cheek.opacity == a_settings.opacity && a_cheek.shine == a_settings.shine && a_cheek.gloss == a_settings.gloss &&
			a_cheek.reflection == a_settings.reflection && a_cheek.glow == a_settings.glow) {
			return;
		}
		for (auto& blob : a_cheek.blobs) {
			Shade(blob.root.get(), a_settings);
		}
		for (auto& rope : a_cheek.ropes) {
			Shade(rope.root.get(), a_settings);
		}
		a_cheek.opacity = a_settings.opacity;
		a_cheek.shine = a_settings.shine;
		a_cheek.gloss = a_settings.gloss;
		a_cheek.reflection = a_settings.reflection;
		a_cheek.glow = a_settings.glow;
	}

	Tears::Blob* Tears::AcquireBlob(Cheek& a_cheek)
	{
		for (auto& blob : a_cheek.blobs) {
			if (!blob.used) {
				blob.used = true;
				return &blob;
			}
		}
		if (a_cheek.blobs.size() >= kMaxBlobs) {
			return nullptr;
		}
		Blob blob;
		if (!_spareBlobs.empty()) {
			blob = std::move(_spareBlobs.back());
			_spareBlobs.pop_back();
		} else if (_cloneBudget <= 0 || (--_cloneBudget, !CloneBlob(blob))) {
			return nullptr;
		}
		blob.root->local = RE::NiTransform{};
		a_cheek.root->AttachChild(blob.root.get(), true);
		a_cheek.opacity = -1.0f;
		blob.used = true;
		a_cheek.blobs.push_back(std::move(blob));
		return &a_cheek.blobs.back();
	}

	Tears::Rope* Tears::AcquireRope(Cheek& a_cheek)
	{
		for (auto& rope : a_cheek.ropes) {
			if (!rope.used) {
				rope.used = true;
				return &rope;
			}
		}
		if (a_cheek.ropes.size() >= kMaxRopes) {
			return nullptr;
		}
		Rope rope;
		if (!_spareRopes.empty()) {
			rope = std::move(_spareRopes.back());
			_spareRopes.pop_back();
		} else if (_cloneBudget <= 0 || (--_cloneBudget, !CloneRope(rope, MeshesFor().rope.get(), MeshNames::Get().ropeShape))) {
			return nullptr;
		}
		rope.root->local = RE::NiTransform{};
		a_cheek.root->AttachChild(rope.root.get(), true);
		a_cheek.opacity = -1.0f;
		rope.used = true;
		a_cheek.ropes.push_back(std::move(rope));
		return &a_cheek.ropes.back();
	}

	Tears::Rope* Tears::AcquireStainRope(Cheek& a_cheek)
	{
		for (auto& rope : a_cheek.stainRopes) {
			if (!rope.used) {
				rope.used = true;
				return &rope;
			}
		}
		if (a_cheek.stainRopes.size() >= kMaxStainRuns || MeshesFor().stainFailed) {
			return nullptr;
		}
		Rope rope;
		if (!_spareStains.empty()) {
			rope = std::move(_spareStains.back());
			_spareStains.pop_back();
		} else if (_cloneBudget <= 0 || (--_cloneBudget, !CloneRope(rope, MeshesFor().stain.get(), MeshNames::Get().stainShape))) {
			return nullptr;
		}
		rope.root->local = RE::NiTransform{};
		a_cheek.root->AttachChild(rope.root.get(), true);
		a_cheek.stainOpacity = -1.0f;
		rope.used = true;
		a_cheek.stainRopes.push_back(std::move(rope));
		return &a_cheek.stainRopes.back();
	}

	RE::NiPoint3 Tears::Mount::Point(Vec3 a_point) const
	{
		return world ? ToWorld(*frame, a_point) : ToBone(a_point);
	}

	Vec3 Tears::Mount::Direction(Vec3 a_direction) const
	{
		return world ? ToVec(frame->rotate * ToPoint(a_direction)) : a_direction;
	}

	float Tears::Mount::Radius(float a_radius) const
	{
		return a_radius * (world ? frame->unit : kUnitsPerMeter);
	}

	void Tears::PoseBlob(const Mount& a_mount, Blob& a_blob)
	{
		for (std::size_t i = 0; i < a_blob.bones.size(); ++i) {
			RE::NiTransform world;
			world.translate = a_mount.Point(a_blob.surface[i].position);
			world.rotate = ArcRotation(_directions[i], a_mount.Direction(a_blob.surface[i].normal));
			a_blob.bones[i]->local = world;
		}
		a_blob.root->SetAppCulled(false);
	}

	void Tears::PoseDrop(const Mount& a_mount, Blob& a_blob, Vec3 a_center, float a_radius, Vec3 a_up, Vec3 a_velocity, std::optional<Vec3> a_contact)
	{
		const float flying = a_contact ? 0.0f : std::max(Fluid::Length(a_velocity) - kStreakStart, 0.0f);
		const float stretch = 1.0f + std::min(flying * kStreak, kMaxStretch - 1.0f);
		const Vec3  along = Fluid::Normalize(a_velocity, a_up);
		const float squash = a_contact ? kSquash : 1.0f;
		const Vec3  normal = a_contact ? *a_contact : a_up;
		const Vec3  center = a_contact ? a_center - normal * (a_radius * (1.0f - squash) * 0.7f) : a_center;
		for (std::size_t i = 0; i < a_blob.bones.size(); ++i) {
			const Vec3      direction = _directions[i];
			const Vec3      offset = Stretch(Stretch(direction, along, stretch), normal, squash);
			const Vec3      facing = Fluid::Normalize(Stretch(Stretch(direction, along, 1.0f / stretch), normal, 1.0f / squash), direction);
			RE::NiTransform world;
			world.translate = a_mount.Point(center + offset * a_radius);
			world.rotate = ArcRotation(direction, a_mount.Direction(facing));
			a_blob.bones[i]->local = world;
		}
		a_blob.live = false;
		a_blob.root->SetAppCulled(false);
	}

	void Tears::PoseRope(const Mount& a_mount, Cheek& a_cheek, Rope& a_rope, std::size_t a_begin, std::size_t a_end, const Fluid::Anchor* a_anchor,
		float a_width, float a_fade)
	{
		_ropePoints.assign(a_cheek.particles.begin() + static_cast<std::ptrdiff_t>(a_begin), a_cheek.particles.begin() + static_cast<std::ptrdiff_t>(a_end));
		PoseRopeFrom(a_mount, a_cheek, a_rope, a_anchor, a_width, a_fade);
	}

	void Tears::PoseRopeFrom(const Mount& a_mount, Cheek& a_cheek, Rope& a_rope, const Fluid::Anchor* a_anchor, float a_width, float a_fade)
	{
		const float spacing = a_cheek.spacing;
		const float volume = spacing * spacing * spacing;
		const float fade = std::max(a_fade, 1e-3f);
		auto&       points = _ropePoints;
		if (a_anchor) {
			Fluid::Particle tip = points.back();
			tip.position = a_anchor->point;
			points.push_back(tip);
		}
		const std::size_t count = points.size();
		auto&             along = _ropeAlong;
		along.assign(count, 0.0f);
		for (std::size_t k = 1; k < count; ++k) {
			along[k] = along[k - 1] + Fluid::Length(points[k].position - points[k - 1].position);
		}
		const float nominal = std::sqrt(volume / (std::numbers::pi_v<float> * kSlotSpacing * spacing));
		const float total = along.back();
		const float cap = nominal;
		const auto  gapAt = [&](std::size_t k) {
            const std::size_t lo = k > 0 ? k - 1 : 0;
            const std::size_t hi = std::min(k + 2, count - 1);
            return (along[hi] - along[lo]) / static_cast<float>(std::max<std::size_t>(hi - lo, 1));
		};

		std::array<RE::NiPoint3, kRopeBones> positions{};
		std::array<float, kRopeBones>        radii{};
		std::size_t                          segment = 0;
		for (std::size_t b = 0; b < kRopeBones; ++b) {
			const float t = static_cast<float>(b) / static_cast<float>(kRopeBones - 1);
			const float s = -cap + (total + 2.0f * cap) * t;
			const float clamped = std::clamp(s, 0.0f, total);
			while (segment + 2 < count && along[segment + 1] < clamped) {
				++segment;
			}
			const auto& a = points[segment];
			const auto& c = points[std::min(segment + 1, count - 1)];
			const float length = along[std::min(segment + 1, count - 1)] - along[segment];
			const float u = length > 1e-6f ? std::clamp((clamped - along[segment]) / length, 0.0f, 1.0f) : 0.0f;
			Vec3        point = a.position + (c.position - a.position) * u;
			const Vec3  tangent = Fluid::Normalize(c.position - a.position, { 0.0f, 0.0f, 1.0f });
			if (s < 0.0f) {
				point = point + tangent * s;
			} else if (s > total) {
				point = point + tangent * (s - total);
			}
			const float gap = std::max(gapAt(segment), 0.3f * spacing);
			float       radius = std::clamp(std::sqrt(volume / (std::numbers::pi_v<float> * gap)), 0.2f * nominal, 1.6f * nominal);
			const float weight = std::clamp((a.life + (c.life - a.life) * u) / fade, 0.0f, 1.0f);
			radius *= std::sqrt(weight) * a_width;
			if (b == 0 || (b + 1 == kRopeBones && !a_anchor)) {
				radius *= 0.55f;
			} else if (b == 1 || (b + 2 == kRopeBones && !a_anchor)) {
				radius *= 0.85f;
			}
			positions[b] = a_mount.Point(point);
			radii[b] = a_mount.Radius(radius);
		}
		Bend(a_rope, positions, radii);
	}

	void Tears::Bend(Rope& a_rope, const std::array<RE::NiPoint3, kRopeBones>& a_positions, const std::array<float, kRopeBones>& a_radii)
	{
		GeometryMath::UpdateFrames(a_rope.frames, a_positions, -1);
		for (std::size_t b = 0; b < kRopeBones; ++b) {
			RE::NiTransform world;
			world.rotate = Rotation(a_rope.frames[b]);
			world.translate = a_positions[b];
			world.scale = std::max(a_radii[b], 0.0005f);
			a_rope.bones[b]->local = world;
		}
		a_rope.root->SetAppCulled(false);
	}

	void Tears::DrawStains(Cheek& a_cheek, const Mount& a_mount, const Settings& a_settings, const Spots& a_spots)
	{
		for (auto& rope : a_cheek.stainRopes) {
			rope.used = false;
		}
		std::size_t shown = 0;
		auto&       stains = a_cheek.stains;
		if (!stains.empty()) {
			// the streaks in the order they were laid, each from the lid down its path
			auto& order = _stainOrder;
			order.resize(stains.size());
			std::iota(order.begin(), order.end(), std::size_t{ 0 });
			std::ranges::stable_sort(order, [&](std::size_t a_lhs, std::size_t a_rhs) {
				return stains[a_lhs].run != stains[a_rhs].run ? stains[a_lhs].run < stains[a_rhs].run : stains[a_lhs].order < stains[a_rhs].order;
			});
			const float width = kStainRadius * a_settings.mascaraWidth;
			const auto  radiusOf = [&](const Stain& a_stain) {
				const float fade = std::clamp(a_stain.life / std::max(a_stain.lifetime * kStainFade, 1e-3f), 0.0f, 1.0f);
				return width * (0.6f + 0.4f * a_stain.strength) * (a_stain.smeared ? kSmearWidth : 1.0f) * std::sqrt(fade);
			};
			for (std::size_t begin = 0; begin < order.size();) {
				std::size_t end = begin + 1;
				while (end < order.size() && stains[order[end]].run == stains[order[begin]].run) {
					++end;
				}
				const Stain& first = stains[order[begin]];
				const Stain& last = stains[order[end - 1]];
				auto&        path = _stainPath;
				auto&        radius = _stainRadius;
				path.clear();
				radius.clear();
				const auto& origin = a_spots[std::min<std::size_t>(first.stream, Face::kMaxStreams - 1)];
				if (origin) {
					path.push_back(origin->point + origin->normal * kStainLift);
					radius.push_back(radiusOf(first));
				}
				for (std::size_t i = begin; i < end; ++i) {
					path.push_back(stains[order[i]].position);
					radius.push_back(radiusOf(stains[order[i]]));
				}
				// a tear still running: its streak reaches the bead
				if (last.order != kBeadOrder) {
					Vec3        sum{};
					std::size_t count = 0;
					for (const auto& particle : a_cheek.particles) {
						if (particle.run == first.run && particle.order == 0 && (particle.touching || particle.airborne < kTipGrace)) {
							sum += particle.position;
							++count;
						}
					}
					if (count > 0) {
						path.push_back(OntoSkin(a_cheek.skin, sum * (1.0f / static_cast<float>(count))));
						radius.push_back(radiusOf(last));
					}
				}
				if (path.size() >= 2) {
					if (Rope* rope = AcquireStainRope(a_cheek)) {
						PoseStain(a_mount, *rope);
						++shown;
					}
				}
				begin = end;
			}
		}
		for (auto& rope : a_cheek.stainRopes) {
			if (rope.used) {
				Reveal(rope.dismember.get());
			} else {
				rope.root->SetAppCulled(true);
			}
		}
		a_cheek.shownStains = shown;
	}

	// Poses a streak along _stainPath: darkest at the lid, thinning down the cheek.
	void Tears::PoseStain(const Mount& a_mount, Rope& a_rope)
	{
		const auto&       path = _stainPath;
		const auto&       radius = _stainRadius;
		const std::size_t count = path.size();
		auto&             along = _ropeAlong;
		along.assign(count, 0.0f);
		for (std::size_t k = 1; k < count; ++k) {
			along[k] = along[k - 1] + Fluid::Length(path[k] - path[k - 1]);
		}
		const float total = std::max(along.back(), 1e-6f);
		std::array<RE::NiPoint3, kRopeBones> positions{};
		std::array<float, kRopeBones>        radii{};
		std::size_t                          segment = 0;
		for (std::size_t b = 0; b < kRopeBones; ++b) {
			const float t = static_cast<float>(b) / static_cast<float>(kRopeBones - 1);
			const float s = total * t;
			while (segment + 2 < count && along[segment + 1] < s) {
				++segment;
			}
			const std::size_t next = std::min(segment + 1, count - 1);
			const float       length = along[next] - along[segment];
			const float       u = length > 1e-6f ? std::clamp((s - along[segment]) / length, 0.0f, 1.0f) : 0.0f;
			float r = (radius[segment] + (radius[next] - radius[segment]) * u) * (1.0f - 0.35f * t);
			if (b == 0 || b + 1 == kRopeBones) {
				r *= 0.6f;
			}
			positions[b] = a_mount.Point(path[segment] + (path[next] - path[segment]) * u);
			radii[b] = a_mount.Radius(r);
		}
		Bend(a_rope, positions, radii);
	}

	void Tears::Draw(Cheek& a_cheek, const Mount& a_mount, const Settings& a_settings, const Spots& a_spots, bool a_markers)
	{
		auto& meshes = MeshesFor();
		if (meshes.failed) {
			return;
		}
		auto&      particles = a_cheek.particles;
		const bool restyled = a_cheek.opacity != a_settings.opacity || a_cheek.shine != a_settings.shine || a_cheek.gloss != a_settings.gloss ||
		                      a_cheek.reflection != a_settings.reflection || a_cheek.glow != a_settings.glow ||
		                      (!a_cheek.stainRopes.empty() && a_cheek.stainOpacity != a_settings.mascaraOpacity);
		if (!a_mount.world && !a_cheek.dirty && !restyled && !a_markers) {
			// Nothing moved since the last draw, and the nodes hang under the head
			// bone, so they are already where the tears are. Only a trail thinning
			// out as it dries still needs redrawing.
			bool settled = true;
			for (const auto& particle : particles) {
				if (!particle.asleep || particle.life <= a_settings.fade) {
					settled = false;
					break;
				}
			}
			if (settled) {
				return;
			}
		}
		const Vec3  up = -a_mount.frame->down;
		const float spacing = a_cheek.spacing;
		const float volume = spacing * spacing * spacing;
		const float kernel = 2.0f * spacing;
		a_cheek.field.Rebuild(particles, kernel, a_settings.fade);
		const auto& field = a_cheek.field;
		auto&       pooled = _pooled;
		pooled.clear();
		for (std::size_t i = 0; i < particles.size(); ++i) {
			if (!particles[i].asleep) {
				const float count = field.Count(particles[i].position);
				particles[i].bulk = particles[i].bulk ? count >= kBulkLeave : count >= kBulkEnter;
			}
			if (particles[i].bulk) {
				pooled.push_back(i);
			}
		}
		auto& clusters = _clusters;
		Fluid::FindClusters(particles, pooled, 2.0f * spacing, a_cheek.clusterGrid, clusters);
		auto& owner = _owner;
		owner.assign(particles.size(), Fluid::kNone);
		for (std::size_t c = 0; c < clusters.size(); ++c) {
			for (const auto member : clusters[c].members) {
				owner[member] = c;
			}
		}
		for (auto& blob : a_cheek.blobs) {
			blob.used = false;
		}
		for (auto& rope : a_cheek.ropes) {
			rope.used = false;
		}
		auto& hints = _hints;
		hints.resize(clusters.size());
		for (auto& hinted : hints) {
			hinted.clear();
		}
		for (std::size_t b = 0; b < a_cheek.blobs.size(); ++b) {
			auto& blob = a_cheek.blobs[b];
			if (blob.live && blob.seed < particles.size() && owner[blob.seed] != Fluid::kNone) {
				hints[owner[blob.seed]].push_back(b);
				blob.used = true;
			} else {
				blob.live = false;
			}
		}

		const float blend = 1.0f - std::exp(-RE::GetSecondsSinceLastFrame() / kSmoothing);
		auto&       claimed = _claimed;
		claimed.clear();
		auto& droplets = _droplets;
		droplets.clear();
		const auto contactOf = [&](std::span<const std::size_t> a_members) -> std::optional<Vec3> {
            const auto& skin = a_cheek.skin.Patches();
            for (const auto m : a_members) {
                const auto& particle = particles[m];
                if (!particle.touching || particle.surface < 0) {
                    continue;
                }
                const auto index = static_cast<std::size_t>(particle.surface);
                if (index < a_cheek.capsules.size()) {
                    const auto& capsule = a_cheek.capsules[index];
                    return Fluid::Normalize(particle.position - (capsule.a + (capsule.b - capsule.a) * particle.along), up);
                }
                if (index - a_cheek.capsules.size() < skin.size()) {
                    return skin[index - a_cheek.capsules.size()].normal;
                }
            }
            return std::nullopt;
		};
		const auto velocityOf = [&](std::span<const std::size_t> a_members) {
			Vec3 sum{};
			for (const auto m : a_members) {
				sum += particles[m].velocity;
			}
			return a_members.empty() ? sum : sum * (1.0f / static_cast<float>(a_members.size()));
		};
		const auto weightOf = [&](std::span<const std::size_t> a_members) {
			float sum = 0.0f;
			for (const auto m : a_members) {
				sum += field.Weight(particles[m]);
			}
			return a_members.empty() ? 0.0f : sum / static_cast<float>(a_members.size());
		};
		for (std::size_t c = 0; c < clusters.size(); ++c) {
			const auto& cluster = clusters[c];
			if (cluster.members.size() < kBlobMinimum) {
				droplets.push_back({ cluster.center, Fluid::VolumeRadius(cluster.members.size(), volume) * std::cbrt(weightOf(cluster.members)),
					velocityOf(cluster.members), contactOf(cluster.members) });
				continue;
			}
			auto& seeds = _seeds;
			seeds.clear();
			for (const auto b : hints[c]) {
				seeds.push_back(a_cheek.blobs[b].seed);
			}
			const bool settled = std::ranges::all_of(cluster.members, [&](std::size_t a_member) {
				return particles[a_member].asleep && particles[a_member].life > a_settings.fade;
			});
			Fluid::SplitCluster(particles, cluster, 4.0f * spacing, seeds, _pieces);
			for (const auto& piece : _pieces) {
				Blob* blob = piece.hint != Fluid::kNone ? &a_cheek.blobs[hints[c][piece.hint]] : AcquireBlob(a_cheek);
				if (!blob) {
					continue;
				}
				if (piece.hint != Fluid::kNone) {
					claimed.push_back(hints[c][piece.hint]);
				}
				const bool warm = blob->live && blob->surface.size() == _directions.size();
				if (settled && warm && piece.hint != Fluid::kNone) {
					blob->seed = piece.seed;
					if (a_mount.world) {
						PoseBlob(a_mount, *blob);  // world coordinates move with the head even when the tear does not
					}
					continue;
				}
				const auto previous = warm ? std::span<const Fluid::SurfacePoint>(blob->surface) : std::span<const Fluid::SurfacePoint>{};
				if (!Fluid::WrapSurface(field, particles[piece.seed].position, piece.extent + kernel, kThreshold, _directions, previous, _scratch)) {
					PoseDrop(a_mount, *blob, piece.center, Fluid::VolumeRadius(piece.count, volume), up);
					continue;
				}
				if (warm) {
					for (std::size_t i = 0; i < _scratch.size(); ++i) {
						auto&       current = blob->surface[i];
						const auto& target = _scratch[i];
						current.position = current.position + (target.position - current.position) * blend;
						current.normal = Fluid::Normalize(current.normal + (target.normal - current.normal) * blend, target.normal);
						current.distance = target.distance;
					}
				} else {
					blob->surface = _scratch;
				}
				blob->seed = piece.seed;
				blob->live = true;
				PoseBlob(a_mount, *blob);
			}
		}
		for (auto& hinted : hints) {
			for (const auto b : hinted) {
				if (std::ranges::find(claimed, b) == claimed.end()) {
					a_cheek.blobs[b].used = false;
					a_cheek.blobs[b].live = false;
				}
			}
		}

		std::size_t ropes = 0;
		Fluid::FindRuns(particles, _runs);
		for (const auto& run : _runs) {
			const auto  held = std::ranges::find_if(a_cheek.anchors, [&](const Fluid::Anchor& a) { return a.particle + 1 == run.end; });
			const auto* anchor = held != a_cheek.anchors.end() ? &*held : nullptr;
			if (particles[run.begin].order > 0) {
				// drops left on a trail are drawn by the trail pass below, plus a bead each
				for (std::size_t i = run.begin; i < run.end; ++i) {
					const auto& drop = particles[i];
					droplets.push_back({ drop.position, Fluid::VolumeRadius(1, volume) * std::cbrt(field.Weight(drop)), drop.velocity, contactOf({ &i, 1 }) });
				}
				continue;
			}
			if (run.Size() >= 2 || anchor) {
				if (Rope* rope = AcquireRope(a_cheek)) {
					PoseRope(a_mount, a_cheek, *rope, run.begin, run.end, anchor, a_settings.trail, a_settings.fade);
					++ropes;
				}
				continue;
			}
			const auto& single = particles[run.begin];
			if (field.BulkCount(single.position) > kCovered) {
				continue;
			}
			const std::size_t index = run.begin;
			droplets.push_back({ single.position, Fluid::VolumeRadius(1, volume) * std::cbrt(field.Weight(single)), single.velocity,
				contactOf({ &index, 1 }) });
		}
		// A welling tear hangs from the lid on one rope, from its newest particle to
		// the seat. Its other leashes are not drawn: stacked translucent tubes at one
		// spot blend into an opaque dark lump.
		const Fluid::Anchor* newest = nullptr;
		for (const auto& anchor : a_cheek.anchors) {
			if (anchor.particle < particles.size() && particles[anchor.particle].bulk && (!newest || anchor.particle > newest->particle)) {
				newest = &anchor;
			}
		}
		if (newest && Fluid::Length(particles[newest->particle].position - newest->point) > 1.5f * spacing) {
			if (Rope* rope = AcquireRope(a_cheek)) {
				PoseRope(a_mount, a_cheek, *rope, newest->particle, newest->particle + 1, newest, a_settings.trail, a_settings.fade);
				++ropes;
			}
		}
		// The trail a tear left behind: the drops it deposited, in the order it left
		// them, and the bead itself while it still runs, as one rope down the cheek.
		for (std::size_t begin = 0; begin < particles.size();) {
			std::size_t end = begin + 1;
			while (end < particles.size() && particles[end].run == particles[begin].run) {
				++end;
			}
			auto& trail = _trail;
			trail.clear();
			Vec3        beadSum{};
			float       beadLife = 0.0f;
			std::size_t bead = 0;
			for (std::size_t i = begin; i < end; ++i) {
				const auto& particle = particles[i];
				if (particle.order > 0) {
					trail.push_back(i);
				} else if (particle.touching || particle.airborne < kTipGrace) {
					// what is left of the bead on the skin, running or come to rest; a
					// piece falling off the face is drawn as a drop, not as rope through the air
					beadSum += particle.position;
					beadLife += particle.life;
					++bead;
				}
			}
			// the streak starts at the lid, where the tear welled
			const std::optional<Spot>& origin = a_spots[particles[begin].stream];
			if (trail.size() + (bead ? 1 : 0) + (origin ? 1 : 0) >= 2 && (trail.size() + (bead ? 1 : 0)) >= 1) {
				std::ranges::sort(trail, [&](std::size_t a_lhs, std::size_t a_rhs) { return particles[a_lhs].order < particles[a_rhs].order; });
				_ropePoints.clear();
				if (origin) {
					Fluid::Particle lid = particles[trail.empty() ? begin : trail.front()];
					lid.position = origin->point + origin->normal * (0.6f * spacing);
					_ropePoints.push_back(lid);
				}
				for (const auto i : trail) {
					_ropePoints.push_back(particles[i]);
				}
				if (bead) {
					Fluid::Particle tip = _ropePoints.back();
					tip.position = beadSum * (1.0f / static_cast<float>(bead));
					tip.life = beadLife / static_cast<float>(bead);
					_ropePoints.push_back(tip);
				}
				if (Rope* rope = AcquireRope(a_cheek)) {
					PoseRopeFrom(a_mount, a_cheek, *rope, nullptr, a_settings.trail, a_settings.fade);
					++ropes;
				}
			}
			begin = end;
		}
		for (const auto& droplet : droplets) {
			if (droplet.radius <= 0.0f) {
				continue;
			}
			if (Blob* blob = AcquireBlob(a_cheek)) {
				PoseDrop(a_mount, *blob, droplet.center, droplet.radius, up, droplet.velocity, droplet.contact);
			}
		}
		if (a_markers) {
			for (const auto& spot : a_spots) {
				if (!spot) {
					continue;
				}
				if (Blob* blob = AcquireBlob(a_cheek)) {
					PoseDrop(a_mount, *blob, spot->point, kMarkerRadius / a_mount.frame->unit, up);
				}
			}
		}

		DrawStains(a_cheek, a_mount, a_settings, a_spots);

		std::size_t blobs = 0;
		for (auto& blob : a_cheek.blobs) {
			if (blob.used) {
				++blobs;
				Reveal(blob.dismember.get());
			} else {
				blob.live = false;
				blob.root->SetAppCulled(true);
			}
		}
		for (auto& rope : a_cheek.ropes) {
			if (rope.used) {
				Reveal(rope.dismember.get());
			} else {
				rope.root->SetAppCulled(true);
			}
		}
		Reshade(a_cheek, a_settings);
		RE::NiUpdateData update{};
		a_cheek.root->Update(update);
		a_cheek.shownBlobs = blobs;
		a_cheek.shownRopes = ropes;
		a_cheek.dirty = false;
	}

	// ------------------------------------------------------------------- status

	std::string Tears::Describe(RE::Actor* a_actor, const Crier* a_crier, const Settings& a_settings) const
	{
		std::string text = NameOf(a_actor) + ": ";
		if (!a_settings.enabled) {
			text += "Lacrima is off. ";
		}
		if (a_crier && a_crier->crying) {
			text += a_crier->forever ? std::format("crying at {:.2f} until stopped. ", a_crier->target) :
			                           std::format("crying at {:.2f}, {:.0f}s left. ", a_crier->target, std::max(a_crier->timeLeft, 0.0f));
		} else {
			text += "not crying. ";
		}
		if (a_crier) {
			static constexpr std::array kNames{ "left", "right" };
			for (std::size_t e = 0; e < Face::kEyes; ++e) {
				const auto& cheek = a_crier->cheeks[e];
				std::size_t asleep = 0;
				for (const auto& particle : cheek.particles) {
					asleep += particle.asleep ? 1 : 0;
				}
				text += std::format("{} cheek: {} particles ({} at rest), {} trails, {} drops, {} mascara streaks{}; so far {} emitted, {} cut falling, "
				                    "{} taken by a hand, {} expired; skin under the fluid {} of {} posed, {} steps. ",
					kNames[e], cheek.particles.size(), asleep, cheek.shownRopes, cheek.shownBlobs, cheek.shownStains, cheek.sheds ? "" : ", not shedding",
					cheek.emitted, cheek.cutFalling, cheek.cutByHand, cheek.expired, cheek.lastPatches, cheek.lastPosed, cheek.lastSteps);
			}
			text += std::format("{} stream(s) per eye, mascara {:.2f}. ", a_crier->streams, a_crier->mascara);
			text += a_settings.mountWorld ? "Drawn in world space under the cell node (mount_world). " : "Drawn under the head bone. ";
			if (a_crier->frozen) {
				text += "Paused: out of sight or too far from the camera. ";
			}
			if (!a_crier->head) {
				text += "No head bone; using the actor's root. ";
			}
			// Where the first drawn drop actually is, for a "nothing shows" report.
			for (const auto& cheek : a_crier->cheeks) {
				const auto drawn = std::ranges::find_if(cheek.blobs, [](const Blob& a_blob) { return a_blob.used && a_blob.root && !a_blob.bones.empty(); });
				if (drawn == cheek.blobs.end()) {
					continue;
				}
				const auto& bone = drawn->bones.front()->world.translate;
				const auto& head = a_crier->frame.origin;
				bool        culled = false;
				for (const RE::NiAVObject* node = drawn->root.get(); node; node = node->parent) {
					culled = culled || node->GetAppCulled();
				}
				text += std::format("A drop is drawn at ({:.0f}, {:.0f}, {:.0f}), {:.1f} units from the head bone, {}, {}. ", bone.x, bone.y, bone.z,
					bone.GetDistance(head), culled ? "culled somewhere up its tree" : "not culled",
					drawn->dismember ? "with a dismember skin (old mesh)" : "plain skin");
				break;
			}
		}
		if (!a_settings.enabled) {
			return text;
		}
		if (a_actor && a_actor->Get3D(false)) {
			text += "Head: " + Clean(Face::Describe(a_actor, PlacementFor(a_actor->GetHandle().native_handle(), a_settings)));
		} else {
			text += "No 3D loaded.";
		}
		if (_meshes.failed) {
			text += " The tear meshes did not load; see Lacrima.log.";
		} else if (_meshes.stainFailed) {
			text += " The mascara mesh did not load, so there is no running mascara; see Lacrima.log.";
		}
		return text;
	}

	void Tears::RefreshStatus(const Settings& a_settings)
	{
		const auto                 now = std::chrono::steady_clock::now();
		std::vector<std::uint32_t> wanted;
		bool                       missing = false;
		{
			std::scoped_lock guard(_lock);
			std::erase_if(_wanted, [&](const auto& a_entry) { return now - a_entry.second > kStatusWanted; });
			std::erase_if(_statuses, [&](const auto& a_entry) { return !_wanted.contains(a_entry.first); });
			for (const auto& [key, at] : _wanted) {
				wanted.push_back(key);
				missing = missing || !_statuses.contains(key);
			}
		}
		if (wanted.empty() || (!missing && now - _statusAt < kStatusEvery)) {
			return;
		}
		_statusAt = now;
		std::vector<std::pair<std::uint32_t, std::string>> texts;
		for (const auto key : wanted) {
			const auto actor = RE::Actor::LookupByHandle(key);
			const auto crier = _criers.find(key);
			texts.emplace_back(key, actor ? Describe(actor.get(), crier != _criers.end() ? crier->second.get() : nullptr, a_settings) :
			                                "That actor is gone.");
		}
		std::scoped_lock guard(_lock);
		for (auto& [key, text] : texts) {
			_statuses[key] = std::move(text);
		}
	}
}
