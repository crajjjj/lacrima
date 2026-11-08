#include "Tears.h"

// The fluid rendering here (clusters wrapped into blobs, linked runs posed as
// ropes, drops squashed against the skin) is adapted from OStrands'
// FluidManager.cpp by LeonVanWinkle (Saint), GPL-3.0. OStrands feeds it from
// climax emitters and openings; Lacrima feeds it from the tear ducts, with one
// small simulation per eye that rides on the face.

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
		constexpr float       kSkinCell = 0.02f;
		constexpr float       kSkinMargin = 0.06f;
		constexpr float       kFloorDepth = 400.0f;
		constexpr float       kRecenter = 120.0f;
		constexpr float       kWellSpeed = 0.02f;
		constexpr float       kFallSpeed = 0.25f;
		constexpr float       kRamp = 1.5f;
		constexpr float       kMarkerRadius = 0.4f;
		constexpr auto        kStatusEvery = 250ms;
		constexpr auto        kStatusWanted = 60s;

		constexpr const char* kBlobPath = "Lacrima\\tear_blob.nif";
		constexpr const char* kRopePath = "Lacrima\\tear_rope.nif";

		struct MeshNames
		{
			RE::BSFixedString blobShape{ "BlobShape" };
			RE::BSFixedString ropeShape{ "RopeShape" };

			static const MeshNames& Get()
			{
				static const MeshNames names;
				return names;
			}
		};

		Vec3         ToVec(const RE::NiPoint3& a_point) { return { a_point.x, a_point.y, a_point.z }; }
		RE::NiPoint3 ToPoint(Vec3 a_vec) { return { a_vec.x, a_vec.y, a_vec.z }; }

		RE::NiPoint3 Normalized(RE::NiPoint3 a_vector, const RE::NiPoint3& a_fallback)
		{
			return a_vector.Unitize() > 1e-5f ? a_vector : a_fallback;
		}

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

		Fluid::Parameters ParametersOf(const Settings& a_settings)
		{
			return { 4, a_settings.cohesion, a_settings.viscosity, 9.81f, a_settings.stretch, a_settings.friction, a_settings.stickiness };
		}

		std::string Clean(std::string a_text)
		{
			std::ranges::replace(a_text, '%', ' ');
			return a_text;
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
		std::scoped_lock guard(_lock);
		if (!_enabled) {
			return;
		}
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
		std::scoped_lock guard(_lock);
		if (_enabled) {
			_pending.push_back({ Request::Kind::kShed, a_actor, 0.0f, 0.0f, a_eye });
		}
	}

	void Tears::Wipe(RE::ActorHandle a_actor)
	{
		if (!a_actor) {
			return;
		}
		std::scoped_lock guard(_lock);
		_pending.push_back({ Request::Kind::kWipe, a_actor });
	}

	void Tears::WipeAll()
	{
		std::scoped_lock guard(_lock);
		_pending.push_back({ Request::Kind::kWipeAll });
	}

	void Tears::CryNearby(float a_radius, float a_intensity, float a_seconds)
	{
		std::scoped_lock guard(_lock);
		if (_enabled) {
			Request request{ Request::Kind::kNearby };
			request.intensity = std::isfinite(a_intensity) ? std::clamp(a_intensity, 0.0f, 1.0f) : 0.6f;
			request.seconds = std::isfinite(a_seconds) ? a_seconds : 0.0f;
			request.radius = std::isfinite(a_radius) ? std::max(a_radius, 0.0f) : 0.0f;
			_pending.push_back(request);
		}
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
		_spareBlobs.clear();
		_spareRopes.clear();
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

	Tears::Crier* Tears::CrierFor(RE::ActorHandle a_actor, const Settings& a_settings, bool a_create)
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
		case Request::Kind::kNearby:
			{
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* lists = RE::ProcessLists::GetSingleton();
				if (!player || !lists || a_request.intensity <= 0.0f) {
					break;
				}
				const auto  center = player->GetPosition();
				std::size_t started = 0;
				for (const auto& handle : lists->highActorHandles) {
					const auto actor = handle.get();
					if (!actor || actor->IsDead() || !actor->Get3D(false) || actor->GetPosition().GetDistance(center) > a_request.radius) {
						continue;
					}
					if (auto* crier = CrierFor(handle, a_settings, true)) {
						StartCrying(*crier, a_request.intensity, a_request.seconds, a_settings);
						++started;
					}
				}
				logger::info("{} nearby actor(s) started crying", started);
			}
			break;
		}
	}

	bool Tears::Idle(const Crier& a_crier) const
	{
		return !a_crier.crying && !a_crier.watched && std::ranges::all_of(a_crier.cheeks, [](const Cheek& a_cheek) {
			return a_cheek.particles.empty() && !a_cheek.welling && a_cheek.queued == 0;
		});
	}

	// -------------------------------------------------------------- frame update

	void Tears::Update(float a_delta, bool a_paused)
	{
		const Settings settings = Settings::Snapshot();
		std::vector<Request> requests;
		{
			std::scoped_lock guard(_lock);
			requests.swap(_pending);
			_enabled = settings.enabled;
		}
		if (!settings.enabled) {
			if (!_criers.empty()) {
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
				CrierFor(watch, settings, true);
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
		_clock += a_delta;
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
			const float goal = crier.crying ? crier.target : 0.0f;
			const float step = a_delta / kRamp;
			crier.intensity = crier.intensity < goal ? std::min(goal, crier.intensity + step) : std::max(goal, crier.intensity - step);

			const auto view = Face::Of(actor.get(), settings.ductPosition, settings.ductBelow);
			for (std::size_t e = 0; e < Face::kEyes; ++e) {
				auto&             cheek = crier.cheeks[e];
				const Face::Duct* duct = view && view->ducts[e].valid ? &view->ducts[e] : nullptr;
				if (duct && !Place(cheek, actor.get(), *duct, settings)) {
					continue;
				}
				if (duct) {
					Emit(crier, cheek, *duct, settings, a_delta);
				}
				try {
					Simulate(cheek, view, settings, a_delta);
					Expire(cheek, settings, a_delta);
				} catch (const std::exception& error) {
					logger::error("{}: tears on one cheek cleared: {}", NameOf(actor.get()), error.what());
					Clear(cheek);
				}
				const bool marker = crier.watched && duct;
				if (cheek.particles.empty() && !marker) {
					if (cheek.root) {
						Release(cheek);
					}
					continue;
				}
				if (Attach(cheek, actor.get())) {
					Draw(cheek, settings, marker ? duct : nullptr);
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
					const auto actor = crier->actor.get();
					logger::info("{}: intensity {:.2f}, left cheek {} particles / {} ropes / {} drops, right cheek {} / {} / {}", NameOf(actor.get()),
						crier->intensity, crier->cheeks[0].particles.size(), crier->cheeks[0].shownRopes, crier->cheeks[0].shownBlobs,
						crier->cheeks[1].particles.size(), crier->cheeks[1].shownRopes, crier->cheeks[1].shownBlobs);
				}
			}
		}
	}

	void Tears::Publish()
	{
		std::scoped_lock guard(_lock);
		std::erase_if(_states, [&](const auto& a_entry) { return !_criers.contains(a_entry.first); });
		for (const auto& [key, crier] : _criers) {
			auto& state = _states[key];
			state.crying = crier->crying;
			state.intensity = crier->crying ? crier->target : 0.0f;
			state.timeLeft = crier->crying ? (crier->forever ? -1.0f : std::max(crier->timeLeft, 0.0f)) : 0.0f;
		}
	}

	// -------------------------------------------------------------------- ducts

	bool Tears::Place(Cheek& a_cheek, RE::Actor* a_actor, const Face::Duct& a_duct, const Settings& a_settings)
	{
		const float spacing = kBaseSpacing * a_settings.size;
		if (!a_cheek.solver || a_cheek.spacing != spacing) {
			try {
				a_cheek.solver = std::make_unique<Fluid::Solver>(spacing, ParametersOf(a_settings));
				a_cheek.spacing = spacing;
			} catch (const std::exception& error) {
				logger::error("tear solver: {}", error.what());
				return false;
			}
		}
		auto*              cell = a_actor->GetParentCell();
		const RE::NiPoint3 wanted = a_duct.point - RE::NiPoint3{ 0.0f, 0.0f, kFloorDepth };
		if (!a_cheek.placed) {
			a_cheek.placed = true;
			a_cheek.origin = wanted;
			a_cheek.cell = cell;
			return true;
		}
		if (cell != a_cheek.cell) {
			const bool interior = (cell && cell->IsInteriorCell()) || (a_cheek.cell && a_cheek.cell->IsInteriorCell());
			a_cheek.cell = cell;
			if (interior) {
				Clear(a_cheek);
				a_cheek.origin = wanted;
				return true;
			}
		}
		const RE::NiPoint3 offset = wanted - a_cheek.origin;
		if (offset.Length() > kRecenter) {
			const Vec3 shift = ToVec(offset / kUnitsPerMeter);
			for (auto& particle : a_cheek.particles) {
				particle.position = particle.position - shift;
			}
			for (auto& anchor : a_cheek.anchors) {
				anchor.point = anchor.point - shift;
			}
			a_cheek.origin = wanted;
		}
		return true;
	}

	void Tears::StartTear(Cheek& a_cheek, float a_strength, const Settings& a_settings)
	{
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const float                           size = static_cast<float>(a_settings.drop) * (0.5f + 0.5f * a_strength) * (0.75f + 0.5f * unit(_rng));
		a_cheek.run = ++_nextRun;
		a_cheek.left = std::max(2, static_cast<int>(std::lround(size)));
		a_cheek.interval = a_settings.well / static_cast<float>(a_cheek.left);
		a_cheek.slot = 0.0f;
		a_cheek.hold = a_settings.hold * (0.6f + 0.8f * unit(_rng));
		a_cheek.welling = true;
	}

	void Tears::Emit(Crier& a_crier, Cheek& a_cheek, const Face::Duct& a_duct, const Settings& a_settings, float a_delta)
	{
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		if (!a_cheek.welling) {
			if (a_cheek.queued > 0) {
				--a_cheek.queued;
				StartTear(a_cheek, std::max(a_crier.intensity, 0.6f), a_settings);
			} else if (a_crier.crying && a_cheek.sheds && a_crier.intensity > 0.01f) {
				a_cheek.wait -= a_delta;
				if (a_cheek.wait <= 0.0f) {
					StartTear(a_cheek, a_crier.intensity, a_settings);
				}
			}
			if (!a_cheek.welling) {
				return;
			}
		}

		const float        spacing = a_cheek.spacing;
		const float        lift = 0.6f * spacing * kUnitsPerMeter;
		const RE::NiPoint3 side = Normalized(a_duct.normal.Cross(a_duct.down), { 1.0f, 0.0f, 0.0f });
		const RE::NiPoint3 seat = a_duct.point + a_duct.normal * lift;
		const Vec3         hold = ToLocal(a_cheek, seat);
		auto&              particles = a_cheek.particles;
		std::optional<std::size_t> newest;
		a_cheek.slot -= a_delta;
		while (a_cheek.left > 0 && a_cheek.slot <= 0.0f) {
			a_cheek.slot += a_cheek.interval;
			--a_cheek.left;
			if (particles.size() >= static_cast<std::size_t>(a_settings.maxParticles)) {
				continue;
			}
			const RE::NiPoint3 jitter = (side * (unit(_rng) - 0.5f) + a_duct.down * (unit(_rng) - 0.5f)) * (0.6f * spacing * kUnitsPerMeter);
			Fluid::Particle    particle;
			particle.life = a_settings.lifetime * (0.85f + 0.3f * unit(_rng));
			particle.position = ToLocal(a_cheek, seat + jitter);
			particle.velocity = ToVec(a_duct.down * kWellSpeed);
			particle.run = a_cheek.run;
			if (!particles.empty() && particles.back().run == a_cheek.run) {
				const float gap = Fluid::Length(particle.position - particles.back().position);
				if (gap < 2.0f * spacing) {
					particles.back().link = std::max(kLinkSpacing * spacing, gap);
				}
			}
			newest = particles.size();
			particles.push_back(particle);
		}
		const auto held = std::ranges::find_if(a_cheek.anchors, [&](const Fluid::Anchor& a) { return a.owner == a_cheek.id; });
		if (newest) {
			const float rest = std::max(Fluid::Length(particles[*newest].position - hold), 0.5f * spacing);
			if (held != a_cheek.anchors.end()) {
				*held = { *newest, hold, rest, a_cheek.id };
			} else {
				a_cheek.anchors.push_back({ *newest, hold, rest, a_cheek.id });
			}
		} else if (held != a_cheek.anchors.end()) {
			held->point = hold;
		}
		if (a_cheek.left > 0) {
			return;
		}
		a_cheek.hold -= a_delta;
		if (a_cheek.hold > 0.0f) {
			return;
		}
		std::erase_if(a_cheek.anchors, [&](const Fluid::Anchor& a) { return a.owner == a_cheek.id; });
		a_cheek.welling = false;
		const float rate = a_settings.rate * std::max(a_crier.intensity, 0.05f);
		a_cheek.wait = 60.0f / rate * (0.5f + unit(_rng));
	}

	// ---------------------------------------------------------------- simulation

	Vec3 Tears::ToLocal(const Cheek& a_cheek, const RE::NiPoint3& a_point) const
	{
		return ToVec((a_point - a_cheek.origin) / kUnitsPerMeter);
	}

	RE::NiPoint3 Tears::ToWorld(const Cheek& a_cheek, Vec3 a_point) const
	{
		return a_cheek.origin + ToPoint(a_point) * kUnitsPerMeter;
	}

	void Tears::Simulate(Cheek& a_cheek, const std::optional<Face::View>& a_view, const Settings& a_settings, float a_delta)
	{
		if (a_cheek.particles.empty() || !a_cheek.solver) {
			a_cheek.accumulator = 0.0f;
			return;
		}
		a_cheek.accumulator = std::min(a_cheek.accumulator + a_delta, kMaxSteps * kStep);
		auto& patches = a_cheek.skin.Patches();
		patches.clear();
		if (a_view) {
			Vec3 low = a_cheek.particles.front().position;
			Vec3 high = low;
			for (const auto& particle : a_cheek.particles) {
				low = { std::min(low.x, particle.position.x), std::min(low.y, particle.position.y), std::min(low.z, particle.position.z) };
				high = { std::max(high.x, particle.position.x), std::max(high.y, particle.position.y), std::max(high.z, particle.position.z) };
			}
			low = low - Vec3{ kSkinMargin, kSkinMargin, kSkinMargin };
			high = high + Vec3{ kSkinMargin, kSkinMargin, kSkinMargin };
			for (const auto& patch : a_view->patches) {
				const Vec3 position = ToLocal(a_cheek, patch.position);
				if (position.x < low.x || position.y < low.y || position.z < low.z || position.x > high.x || position.y > high.y ||
					position.z > high.z) {
					continue;
				}
				patches.push_back({ position, ToVec(patch.normal), ToVec(patch.velocity / kUnitsPerMeter) });
			}
		}
		a_cheek.skin.Build(kSkinCell);
		a_cheek.solver->Configure(ParametersOf(a_settings));
		const Fluid::Colliders colliders{ {}, {}, &a_cheek.skin };
		int                    steps = 0;
		while (a_cheek.accumulator >= kStep && steps < kMaxSteps) {
			a_cheek.solver->Step(a_cheek.particles, a_cheek.anchors, colliders, a_cheek.ground, kStep);
			a_cheek.accumulator -= kStep;
			++steps;
		}
		if (steps == kMaxSteps) {
			a_cheek.accumulator = 0.0f;
		}
		std::erase_if(a_cheek.anchors, [](const Fluid::Anchor& a_anchor) { return a_anchor.rest <= 0.0f; });
	}

	void Tears::Expire(Cheek& a_cheek, const Settings& a_settings, float a_delta)
	{
		auto& particles = a_cheek.particles;
		bool  expired = false;
		for (auto& particle : particles) {
			particle.life -= a_delta;
			if (!particle.touching && particle.velocity.z < -kFallSpeed && particle.life > a_settings.fall) {
				particle.life = a_settings.fall;
			}
			expired = expired || particle.life <= 0.0f;
		}
		if (!expired) {
			return;
		}
		std::vector<std::size_t> remap(particles.size(), Fluid::kNone);
		std::size_t              write = 0;
		bool                     previousKept = false;
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

	void Tears::Clear(Cheek& a_cheek)
	{
		a_cheek.particles.clear();
		a_cheek.anchors.clear();
		a_cheek.welling = false;
		a_cheek.left = 0;
		a_cheek.queued = 0;
		a_cheek.accumulator = 0.0f;
	}

	// ----------------------------------------------------------------- rendering

	bool Tears::Attach(Cheek& a_cheek, RE::Actor* a_actor)
	{
		auto* host = a_actor ? a_actor->Get3D(false) : nullptr;
		if (!host || !host->parent) {
			return false;
		}
		RE::NiNode* parent = host->parent;
		if (!a_cheek.root) {
			a_cheek.root.reset(RE::NiNode::Create(0));
			if (!a_cheek.root) {
				return false;
			}
			a_cheek.root->name = "LacrimaTears";
			a_cheek.root->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		}
		if (a_cheek.root->parent != parent) {
			if (a_cheek.root->parent) {
				a_cheek.root->parent->DetachChild2(a_cheek.root.get());
			}
			parent->AttachChild(a_cheek.root.get(), true);
		}
		a_cheek.root->local = parent->world.Invert();
		a_cheek.root->SetAppCulled(host->GetAppCulled());
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
		}
		a_cheek.blobs.clear();
		a_cheek.ropes.clear();
		a_cheek.root.reset();
		a_cheek.opacity = a_cheek.shine = a_cheek.gloss = -1.0f;
		Clear(a_cheek);
		a_cheek.placed = false;
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
		}
		return meshes;
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
		return true;
	}

	bool Tears::CloneRope(Rope& a_rope)
	{
		auto& meshes = MeshesFor();
		auto* clone = meshes.failed ? nullptr : meshes.rope->Clone();
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
		if (auto* shape = node->GetObjectByName(MeshNames::Get().ropeShape)) {
			shape->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		}
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
			if (shader->GetMaterialType() == RE::BSShaderMaterial::Type::kLighting) {
				if (auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(shader->GetBaseMaterial())) {
					material->specularColorScale = a_settings.shine;
					material->specularPower = a_settings.gloss;
				}
			}
		}
	}

	void Tears::Reshade(Cheek& a_cheek, const Settings& a_settings)
	{
		if (a_cheek.opacity == a_settings.opacity && a_cheek.shine == a_settings.shine && a_cheek.gloss == a_settings.gloss) {
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
		} else if (_cloneBudget <= 0 || (--_cloneBudget, !CloneRope(rope))) {
			return nullptr;
		}
		rope.root->local = RE::NiTransform{};
		a_cheek.root->AttachChild(rope.root.get(), true);
		a_cheek.opacity = -1.0f;
		rope.used = true;
		a_cheek.ropes.push_back(std::move(rope));
		return &a_cheek.ropes.back();
	}

	void Tears::PoseBlob(Cheek& a_cheek, Blob& a_blob)
	{
		for (std::size_t i = 0; i < a_blob.bones.size(); ++i) {
			RE::NiTransform world;
			world.translate = ToWorld(a_cheek, a_blob.surface[i].position);
			world.rotate = ArcRotation(_directions[i], a_blob.surface[i].normal);
			a_blob.bones[i]->local = world;
		}
		a_blob.root->SetAppCulled(false);
	}

	void Tears::PoseDrop(Cheek& a_cheek, Blob& a_blob, Vec3 a_center, float a_radius, Vec3 a_velocity, std::optional<Vec3> a_contact)
	{
		const float flying = a_contact ? 0.0f : std::max(Fluid::Length(a_velocity) - kStreakStart, 0.0f);
		const float stretch = 1.0f + std::min(flying * kStreak, kMaxStretch - 1.0f);
		const Vec3  along = Fluid::Normalize(a_velocity, { 0.0f, 0.0f, 1.0f });
		const float squash = a_contact ? kSquash : 1.0f;
		const Vec3  normal = a_contact ? *a_contact : Vec3{ 0.0f, 0.0f, 1.0f };
		const Vec3  center = a_contact ? a_center - normal * (a_radius * (1.0f - squash) * 0.7f) : a_center;
		for (std::size_t i = 0; i < a_blob.bones.size(); ++i) {
			const Vec3      direction = _directions[i];
			const Vec3      offset = Stretch(Stretch(direction, along, stretch), normal, squash);
			const Vec3      facing = Fluid::Normalize(Stretch(Stretch(direction, along, 1.0f / stretch), normal, 1.0f / squash), direction);
			RE::NiTransform world;
			world.translate = ToWorld(a_cheek, center + offset * a_radius);
			world.rotate = ArcRotation(direction, facing);
			a_blob.bones[i]->local = world;
		}
		a_blob.live = false;
		a_blob.root->SetAppCulled(false);
	}

	void Tears::PoseRope(Cheek& a_cheek, Rope& a_rope, std::size_t a_begin, std::size_t a_end, const Fluid::Anchor* a_anchor, float a_width,
		float a_fade)
	{
		const float spacing = a_cheek.spacing;
		const float volume = spacing * spacing * spacing;
		const float fade = std::max(a_fade, 1e-3f);
		auto&       points = _ropePoints;
		points.assign(a_cheek.particles.begin() + static_cast<std::ptrdiff_t>(a_begin), a_cheek.particles.begin() + static_cast<std::ptrdiff_t>(a_end));
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
			positions[b] = ToWorld(a_cheek, point);
			radii[b] = radius * kUnitsPerMeter;
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

	void Tears::Draw(Cheek& a_cheek, const Settings& a_settings, const Face::Duct* a_marker)
	{
		auto& meshes = MeshesFor();
		if (meshes.failed) {
			return;
		}
		const float spacing = a_cheek.spacing;
		const float volume = spacing * spacing * spacing;
		const float kernel = 2.0f * spacing;
		auto&       particles = a_cheek.particles;
		a_cheek.field.Rebuild(particles, kernel, a_settings.fade);
		const auto&              field = a_cheek.field;
		std::vector<std::size_t> pooled;
		for (std::size_t i = 0; i < particles.size(); ++i) {
			if (!particles[i].asleep) {
				const float count = field.Count(particles[i].position);
				particles[i].bulk = particles[i].bulk ? count >= kBulkLeave : count >= kBulkEnter;
			}
			if (particles[i].bulk) {
				pooled.push_back(i);
			}
		}
		const auto               clusters = Fluid::FindClusters(particles, pooled, 2.0f * spacing, a_cheek.clusterGrid);
		std::vector<std::size_t> owner(particles.size(), Fluid::kNone);
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
		std::vector<std::vector<std::size_t>> hints(clusters.size());
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
		std::vector<std::size_t> claimed;
		struct Droplet
		{
			Vec3                center;
			float               radius;
			Vec3                velocity;
			std::optional<Vec3> contact;
		};
		std::vector<Droplet> droplets;
		const auto           contactOf = [&](std::span<const std::size_t> a_members) -> std::optional<Vec3> {
            const auto& skin = a_cheek.skin.Patches();
            for (const auto m : a_members) {
                const auto& particle = particles[m];
                if (!particle.touching || particle.surface < 0) {
                    continue;
                }
                const auto index = static_cast<std::size_t>(particle.surface);
                if (index < skin.size()) {
                    return skin[index].normal;
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
			std::vector<std::size_t> seeds;
			for (const auto b : hints[c]) {
				seeds.push_back(a_cheek.blobs[b].seed);
			}
			const bool settled = std::ranges::all_of(cluster.members, [&](std::size_t a_member) {
				return particles[a_member].asleep && particles[a_member].life > a_settings.fade;
			});
			for (const auto& piece : Fluid::SplitCluster(particles, cluster, 4.0f * spacing, seeds)) {
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
					continue;
				}
				const auto previous = warm ? std::span<const Fluid::SurfacePoint>(blob->surface) : std::span<const Fluid::SurfacePoint>{};
				if (!Fluid::WrapSurface(field, particles[piece.seed].position, piece.extent + kernel, kThreshold, _directions, previous, _scratch)) {
					PoseDrop(a_cheek, *blob, piece.center, Fluid::VolumeRadius(piece.count, volume));
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
				PoseBlob(a_cheek, *blob);
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
		for (const auto& run : Fluid::FindRuns(particles)) {
			const auto  held = std::ranges::find_if(a_cheek.anchors, [&](const Fluid::Anchor& a) { return a.particle + 1 == run.end; });
			const auto* anchor = held != a_cheek.anchors.end() ? &*held : nullptr;
			if (run.Size() >= 2 || anchor) {
				if (Rope* rope = AcquireRope(a_cheek)) {
					PoseRope(a_cheek, *rope, run.begin, run.end, anchor, a_settings.trail, a_settings.fade);
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
		for (const auto& anchor : a_cheek.anchors) {
			if (anchor.particle < particles.size() && particles[anchor.particle].bulk) {
				if (Rope* rope = AcquireRope(a_cheek)) {
					PoseRope(a_cheek, *rope, anchor.particle, anchor.particle + 1, &anchor, a_settings.trail, a_settings.fade);
					++ropes;
				}
			}
		}
		for (const auto& droplet : droplets) {
			if (droplet.radius <= 0.0f) {
				continue;
			}
			if (Blob* blob = AcquireBlob(a_cheek)) {
				PoseDrop(a_cheek, *blob, droplet.center, droplet.radius, droplet.velocity, droplet.contact);
			}
		}
		if (a_marker) {
			if (Blob* blob = AcquireBlob(a_cheek)) {
				PoseDrop(a_cheek, *blob, ToLocal(a_cheek, a_marker->point), kMarkerRadius / kUnitsPerMeter);
			}
		}

		std::size_t blobs = 0;
		for (auto& blob : a_cheek.blobs) {
			if (blob.used) {
				++blobs;
			} else {
				blob.live = false;
				blob.root->SetAppCulled(true);
			}
		}
		for (auto& rope : a_cheek.ropes) {
			if (!rope.used) {
				rope.root->SetAppCulled(true);
			}
		}
		Reshade(a_cheek, a_settings);
		RE::NiUpdateData update{};
		a_cheek.root->Update(update);
		a_cheek.shownBlobs = blobs;
		a_cheek.shownRopes = ropes;
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
				text += std::format("{} cheek: {} particles, {} trails, {} drops{}. ", kNames[e], cheek.particles.size(), cheek.shownRopes,
					cheek.shownBlobs, cheek.sheds ? "" : ", not shedding");
			}
		}
		if (a_actor && a_actor->Get3D(false)) {
			text += "Head: " + Clean(Face::Describe(a_actor, a_settings.ductPosition, a_settings.ductBelow));
		} else {
			text += "No 3D loaded.";
		}
		if (_meshes.failed) {
			text += " The tear meshes did not load; see Lacrima.log.";
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
