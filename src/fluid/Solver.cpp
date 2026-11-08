#include "fluid/Solver.h"

#include "fluid/Clusters.h"

#include <algorithm>
#include <stdexcept>

namespace Lacrima::Fluid
{
	namespace
	{
		constexpr float kMaxSpeed = 6.0f;
		constexpr float kDomain = 1000.0f;
		constexpr float kFloorFriction = 8.0f;
		constexpr float kLinkStiffness = 0.6f;
		constexpr float kLinkBreak = 12.0f;
		constexpr float kLinkSnap = 6.0f;
		constexpr float kSleepSpeed = 0.02f;
		constexpr float kWakeSpeed = 0.08f;
		constexpr float kSleepTime = 0.4f;
		constexpr float kSkinGravity = 0.25f;
		constexpr float kGroundSlide = 0.2f;
		constexpr float kStickReach = 2.0f;
		constexpr float kHanging = -0.5f;
		constexpr float kSeparation = 0.1f;
		constexpr float kAnchorBreak = 12.0f;
		constexpr float kAnchorFlow = 15.0f;
		constexpr float kCreep = 0.035f;
		constexpr std::size_t kMaxLoad = 64;
		constexpr float kCohesionFloor = 1.0f;
	}

	std::size_t Solver::CellHash::operator()(Cell a_cell) const noexcept
	{
		return (static_cast<std::uint32_t>(a_cell.x) * 73856093u) ^ (static_cast<std::uint32_t>(a_cell.y) * 19349663u) ^
		       (static_cast<std::uint32_t>(a_cell.z) * 83492791u);
	}

	bool Solver::Valid(const Parameters& a_parameters)
	{
		const auto within = [](float a_value, float a_min, float a_max) { return std::isfinite(a_value) && a_value >= a_min && a_value <= a_max; };
		return a_parameters.iterations > 0 && a_parameters.iterations <= 16 && within(a_parameters.cohesion, 0.0f, 20.0f) &&
		       within(a_parameters.viscosity, 0.0f, 100.0f) && within(a_parameters.gravity, 0.0f, 30.0f) &&
		       within(a_parameters.stretch, 0.0f, 30.0f) && within(a_parameters.friction, 0.0f, 200.0f) && within(a_parameters.grip, 0.0f, 10.0f);
	}

	void Solver::Configure(const Parameters& a_parameters)
	{
		if (Valid(a_parameters)) {
			_parameters = a_parameters;
		}
	}

	Solver::Solver(float a_spacing, Parameters a_parameters) :
		_spacing(a_spacing), _radiusSquared(4.0f * a_spacing * a_spacing), _parameters(a_parameters)
	{
		if (!std::isfinite(a_spacing) || a_spacing < 1e-4f || a_spacing > 0.1f || !Valid(a_parameters)) {
			throw std::invalid_argument("invalid fluid parameters");
		}
		for (int z = -2; z <= 2; ++z) {
			for (int y = -2; y <= 2; ++y) {
				for (int x = -2; x <= 2; ++x) {
					_restWeight += Weight(Vec3{ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) } * _spacing);
				}
			}
		}
	}

	Solver::Cell Solver::CellOf(Vec3 a_point) const
	{
		const float size = 2.0f * _spacing;
		return { static_cast<int>(std::floor(a_point.x / size)), static_cast<int>(std::floor(a_point.y / size)),
			static_cast<int>(std::floor(a_point.z / size)) };
	}

	float Solver::Weight(Vec3 a_delta) const
	{
		const float q = std::max(0.0f, 1.0f - LengthSquared(a_delta) / _radiusSquared);
		return q * q * q;
	}

	Vec3 Solver::Gradient(Vec3 a_delta) const
	{
		const float q = std::max(0.0f, 1.0f - LengthSquared(a_delta) / _radiusSquared);
		return a_delta * (-6.0f * q * q / (_radiusSquared * _restWeight));
	}

	void Solver::FindNeighbors(std::span<const Particle> a_particles, bool a_all)
	{
		for (auto& [cell, members] : _grid) {
			members.clear();
		}
		for (std::size_t i = 0; i < _predicted.size(); ++i) {
			_grid[CellOf(_predicted[i])].push_back(i);
		}
		_neighbors.resize(_predicted.size());
		for (std::size_t i = 0; i < _predicted.size(); ++i) {
			auto& neighbors = _neighbors[i];
			neighbors.clear();
			if (!a_all && a_particles[i].asleep) {
				continue;
			}
			const auto cell = CellOf(_predicted[i]);
			for (int z = -1; z <= 1; ++z) {
				for (int y = -1; y <= 1; ++y) {
					for (int x = -1; x <= 1; ++x) {
						const auto found = _grid.find({ cell.x + x, cell.y + y, cell.z + z });
						if (found == _grid.end()) {
							continue;
						}
						for (const auto j : found->second) {
							if (i != j && LengthSquared(_predicted[i] - _predicted[j]) < _radiusSquared) {
								neighbors.push_back(j);
							}
						}
					}
				}
			}
		}
		if (_grid.size() > 4 * _predicted.size() + 64) {
			std::erase_if(_grid, [](const auto& a_entry) { return a_entry.second.empty(); });
		}
	}

	void Solver::Collide(std::span<const Particle> a_particles, const Colliders& a_colliders, bool a_pull)
	{
		const float margin = 0.5f * _spacing;
		const float reach = margin + kStickReach * _spacing;
		for (std::size_t i = 0; i < _predicted.size(); ++i) {
			if (a_particles[i].asleep) {
				continue;
			}
			Vec3& point = _predicted[i];
			if (std::ranges::any_of(a_colliders.holes, [&](const Hole& a_hole) { return LengthSquared(point - a_hole.center) < a_hole.radius * a_hole.radius; })) {
				continue;
			}
			for (std::size_t k = 0; k < a_colliders.capsules.size(); ++k) {
				const auto& capsule = a_colliders.capsules[k];
				const float extent = capsule.radius + reach;
				if (point.x < std::min(capsule.a.x, capsule.b.x) - extent || point.x > std::max(capsule.a.x, capsule.b.x) + extent ||
					point.y < std::min(capsule.a.y, capsule.b.y) - extent || point.y > std::max(capsule.a.y, capsule.b.y) + extent ||
					point.z < std::min(capsule.a.z, capsule.b.z) - extent || point.z > std::max(capsule.a.z, capsule.b.z) + extent) {
					continue;
				}
				const Vec3  span = capsule.b - capsule.a;
				const float length = LengthSquared(span);
				const float t = length > 1e-12f ? std::clamp(Dot(point - capsule.a, span) / length, 0.0f, 1.0f) : 0.0f;
				const Vec3  closest = capsule.a + span * t;
				const Vec3  offset = point - closest;
				const float distance = Length(offset);
				const float limit = capsule.radius + margin;
				const Vec3  normal = distance > 1e-7f ? offset * (1.0f / distance) : Vec3{ 0.0f, 0.0f, 1.0f };
				if (distance < limit) {
					point = closest + normal * limit;
				} else if (a_pull && a_particles[i].touching && distance < capsule.radius + reach && normal.z > kHanging) {
					point = closest + normal * limit;
				} else {
					continue;
				}
				_contact[i] = static_cast<int>(k);
				_along[i] = t;
				_normal[i] = normal;
			}
			if (!a_colliders.skin) {
				continue;
			}
			const auto& patches = a_colliders.skin->Patches();
			for (int pass = 0; pass < 2; ++pass) {
				const std::size_t k = a_colliders.skin->Nearest(point);
				if (k == kNone) {
					break;
				}
				const auto& patch = patches[k];
				const float depth = Dot(point - patch.position, patch.normal);
				const bool  pulled = a_pull && a_particles[i].touching && depth < reach && patch.normal.z > kHanging;
				if (depth >= margin && !pulled) {
					break;
				}
				point = point + patch.normal * (margin - depth);
				_contact[i] = static_cast<int>(a_colliders.capsules.size() + k);
				_along[i] = 0.0f;
				_normal[i] = patch.normal;
			}
		}
	}

	std::optional<Vec3> Solver::Carrier(const Colliders& a_colliders, int a_surface, float a_along, Vec3 a_position)
	{
		if (a_surface < 0) {
			return std::nullopt;
		}
		const auto index = static_cast<std::size_t>(a_surface);
		if (index < a_colliders.capsules.size()) {
			const auto& capsule = a_colliders.capsules[index];
			return capsule.velocityA + (capsule.velocityB - capsule.velocityA) * a_along;
		}
		if (!a_colliders.skin) {
			return std::nullopt;
		}
		const std::size_t nearest = a_colliders.skin->Nearest(a_position);
		if (nearest == kNone) {
			return std::nullopt;
		}
		return a_colliders.skin->Patches()[nearest].velocity;
	}

	void Solver::Step(std::span<Particle> a_particles, std::span<Anchor> a_anchors, const Colliders& a_colliders, const Ground& a_ground, float a_seconds)
	{
		if (!std::isfinite(a_seconds) || a_seconds <= 0.0f || a_seconds > 0.05f) {
			throw std::invalid_argument("invalid fluid step");
		}
		for (const auto& particle : a_particles) {
			if (!IsFinite(particle.position) || !IsFinite(particle.velocity) ||
				std::max({ std::abs(particle.position.x), std::abs(particle.position.y), std::abs(particle.position.z) }) > kDomain) {
				throw std::invalid_argument("a fluid particle left the simulation domain");
			}
		}
		const std::size_t count = a_particles.size();
		if (count == 0) {
			return;
		}
		_predicted.resize(count);
		_velocity.resize(count);
		_correction.resize(count);
		_lambda.resize(count);
		_contact.assign(count, -1);
		_along.resize(count);
		_normal.resize(count);
		const float lift = _spacing * 0.5f;
		_floor.resize(count);
		_anchored.assign(count, false);
		for (const auto& anchor : a_anchors) {
			if (anchor.rest > 0.0f && anchor.particle < count) {
				_anchored[anchor.particle] = true;
			}
		}
		for (std::size_t i = 0; i < count; ++i) {
			_predicted[i] = a_particles[i].position;
		}
		FindNeighbors(a_particles, true);
		for (std::size_t i = 0; i < count; ++i) {
			auto& particle = a_particles[i];
			if (!particle.asleep) {
				continue;
			}
			const bool stirred = _anchored[i] || std::ranges::any_of(_neighbors[i], [&](std::size_t j) {
				return !a_particles[j].asleep && LengthSquared(a_particles[j].velocity) > kWakeSpeed * kWakeSpeed;
			});
			if (stirred) {
				particle.asleep = false;
				particle.idle = 0.0f;
			}
		}
		_weights.assign(count, 0.0f);
		for (std::size_t i = 0; i < count; ++i) {
			for (const auto j : _neighbors[i]) {
				_weights[i] += Weight(a_particles[j].position - a_particles[i].position);
			}
		}
		for (std::size_t i = 0; i < count; ++i) {
			if (a_particles[i].asleep) {
				continue;
			}
			Vec3 cohesion{};
			for (const auto j : _neighbors[i]) {
				const auto  delta = a_particles[j].position - a_particles[i].position;
				const float shared = std::max(0.5f * (_weights[i] + _weights[j]), kCohesionFloor);
				cohesion += delta * (Weight(delta) / shared);
			}
			cohesion = cohesion * (_parameters.cohesion / _spacing);
			const auto& particle = a_particles[i];
			Vec3        moving = particle.velocity;
			if (particle.touching) {
				if (const auto carrier = Carrier(a_colliders, particle.surface, particle.along, particle.position)) {
					moving = moving - particle.carried + *carrier;
				}
			}
			const float gravity = particle.touching ? _parameters.gravity * kSkinGravity : _parameters.gravity;
			Vec3        slide{};
			if (particle.position.z <= a_ground.Height(particle.position.x, particle.position.y) + lift * 1.05f) {
				const Vec3 slope = a_ground.Gradient(particle.position.x, particle.position.y);
				slide = slope * (-_parameters.gravity * kGroundSlide / (1.0f + LengthSquared(slope)));
			}
			const auto velocity = moving + (cohesion + slide + Vec3{ 0.0f, 0.0f, -gravity }) * a_seconds;
			_predicted[i] = a_particles[i].position + LimitLength(velocity, kMaxSpeed) * a_seconds;
			_floor[i] = a_ground.Height(_predicted[i].x, _predicted[i].y) + lift;
			_predicted[i].z = std::max(_predicted[i].z, _floor[i]);
		}
		for (std::size_t i = 0; i < count; ++i) {
			if (a_particles[i].asleep) {
				_floor[i] = a_ground.Height(_predicted[i].x, _predicted[i].y) + lift;
			}
		}
		Collide(a_particles, a_colliders, true);

		for (std::uint32_t iteration = 0; iteration < _parameters.iterations; ++iteration) {
			FindNeighbors(a_particles, false);
			for (std::size_t i = 0; i < count; ++i) {
				if (a_particles[i].asleep) {
					_lambda[i] = 0.0f;
					continue;
				}
				float density = 1.0f;
				float denominator = 0.01f / (_spacing * _spacing);
				Vec3  gradients{};
				for (const auto j : _neighbors[i]) {
					const auto delta = _predicted[i] - _predicted[j];
					density += Weight(delta);
					const auto gradient = Gradient(delta);
					gradients += gradient;
					denominator += Dot(gradient, gradient);
				}
				denominator += Dot(gradients, gradients);
				_lambda[i] = -std::max(0.0f, density / _restWeight - 1.0f) / denominator;
			}
			for (std::size_t i = 0; i < count; ++i) {
				Vec3 correction{};
				if (a_particles[i].asleep) {
					_correction[i] = correction;
					continue;
				}
				for (const auto j : _neighbors[i]) {
					correction += Gradient(_predicted[i] - _predicted[j]) * (_lambda[i] + _lambda[j]);
				}
				_correction[i] = LimitLength(correction, 0.25f * _spacing);
			}
			for (std::size_t i = 0; i < count; ++i) {
				_predicted[i] += _correction[i];
			}
			for (std::size_t i = 0; i + 1 < count; ++i) {
				const float rest = a_particles[i].link;
				if (rest <= 0.0f) {
					continue;
				}
				const bool  first = !a_particles[i].asleep;
				const bool  second = !a_particles[i + 1].asleep;
				const Vec3  delta = _predicted[i + 1] - _predicted[i];
				const float length = Length(delta);
				if (length > 1e-6f && (first || second)) {
					const float share = first && second ? 0.5f : 1.0f;
					const Vec3  correction = LimitLength(delta * ((length - rest) / length * share * kLinkStiffness), 0.25f * _spacing);
					if (first) {
						_predicted[i] += correction;
					}
					if (second) {
						_predicted[i + 1] = _predicted[i + 1] - correction;
					}
				}
			}
			for (const auto& anchor : a_anchors) {
				if (anchor.rest <= 0.0f || anchor.particle >= count) {
					continue;
				}
				auto&       position = _predicted[anchor.particle];
				const Vec3  delta = position - anchor.point;
				const float length = Length(delta);
				if (length > anchor.rest) {
					position = position - LimitLength(delta * ((length - anchor.rest) / length * kLinkStiffness), 0.25f * _spacing);
				}
			}
			for (std::size_t i = 0; i < count; ++i) {
				_predicted[i].z = std::max(_predicted[i].z, _floor[i]);
			}
		}

		const float flow = std::min(1.0f, _parameters.stretch * a_seconds);
		for (std::size_t i = 0; i + 1 < count; ++i) {
			float& rest = a_particles[i].link;
			if (rest <= 0.0f) {
				continue;
			}
			const float length = Length(_predicted[i + 1] - _predicted[i]);
			if (length > rest) {
				rest += (length - rest) * flow;
			}
			if (rest > kLinkBreak * _spacing || length > kLinkSnap * rest) {
				rest = 0.0f;
			}
		}
		Collide(a_particles, a_colliders, false);
		for (std::size_t i = 0; i < count; ++i) {
			_predicted[i].z = std::max(_predicted[i].z, _floor[i]);
		}

		const float follow = std::min(1.0f, kAnchorFlow * a_seconds);
		for (auto& anchor : a_anchors) {
			if (anchor.rest <= 0.0f || anchor.particle >= count) {
				anchor.rest = 0.0f;
				continue;
			}
			const Vec3  delta = _predicted[anchor.particle] - anchor.point;
			const float length = Length(delta);
			if (length > anchor.rest) {
				anchor.rest += (length - anchor.rest) * follow;
			}
			if (delta.z < 0.0f) {
				std::size_t load = 1;
				for (std::size_t k = anchor.particle; k > 0 && load < kMaxLoad && a_particles[k - 1].link > 0.0f; --k) {
					++load;
				}
				anchor.rest += kCreep * std::sqrt(static_cast<float>(load)) * a_seconds;
			}
			if (anchor.rest > kAnchorBreak * _spacing) {
				anchor.rest = 0.0f;
			}
		}

		for (std::size_t i = 0; i < count; ++i) {
			_velocity[i] = a_particles[i].asleep ? Vec3{} : LimitLength((_predicted[i] - a_particles[i].position) * (1.0f / a_seconds), kMaxSpeed);
		}
		FindNeighbors(a_particles, false);
		for (std::size_t i = 0; i < count; ++i) {
			_weights[i] = 0.0f;
			for (const auto j : _neighbors[i]) {
				_weights[i] += Weight(_predicted[i] - _predicted[j]);
			}
		}
		const float viscosity = 1.0f - std::exp(-_parameters.viscosity * a_seconds);
		const float friction = std::exp(-kFloorFriction * a_seconds);
		for (std::size_t i = 0; i < count; ++i) {
			auto& particle = a_particles[i];
			if (particle.asleep) {
				particle.velocity = {};
				continue;
			}
			Vec3 smoothing{};
			for (const auto j : _neighbors[i]) {
				const float shared = 1.0f + 0.5f * (_weights[i] + (a_particles[j].asleep ? _weights[i] : _weights[j]));
				smoothing += (_velocity[j] - _velocity[i]) * (Weight(_predicted[i] - _predicted[j]) / shared);
			}
			auto velocity = _velocity[i] + smoothing * viscosity;
			particle.touching = _contact[i] >= 0;
			if (particle.touching) {
				const Vec3  surface = Carrier(a_colliders, _contact[i], _along[i], _predicted[i]).value_or(Vec3{});
				const Vec3  relative = velocity - surface;
				const float normal = Dot(relative, _normal[i]);
				Vec3        sliding = relative - _normal[i] * normal;
				const float slip = Length(sliding);
				const float grip = _parameters.grip * a_seconds;
				sliding = slip > grip ? sliding * ((slip - grip) / slip * std::exp(-_parameters.friction * a_seconds)) : Vec3{};
				velocity = surface + sliding + _normal[i] * std::clamp(normal, 0.0f, kSeparation);
				particle.carried = surface;
				particle.surface = _contact[i];
				particle.along = _along[i];
			} else {
				particle.surface = -1;
			}
			if (_predicted[i].z <= _floor[i] + _spacing * 0.01f) {
				velocity.z = std::max(velocity.z, 0.0f);
				velocity.x *= friction;
				velocity.y *= friction;
			}
			particle.position = _predicted[i];
			particle.velocity = velocity;
			if (_anchored[i] || particle.touching || LengthSquared(velocity) > kSleepSpeed * kSleepSpeed) {
				particle.idle = 0.0f;
			} else if ((particle.idle += a_seconds) > kSleepTime) {
				particle.asleep = true;
				particle.velocity = {};
			}
		}
	}
}
