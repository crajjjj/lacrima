#include "fluid/Surface.h"

#include <algorithm>

namespace Lacrima::Fluid
{
	namespace
	{
		constexpr int kBisections = 5;
	}

	void DensityField::Rebuild(std::span<const Particle> a_particles, float a_radius, float a_fade)
	{
		_particles = a_particles;
		_radius = a_radius;
		_radiusSquared = a_radius * a_radius;
		_fade = std::max(a_fade, 1e-3f);
		_grid.Begin(a_radius);
		for (std::size_t i = 0; i < a_particles.size(); ++i) {
			_grid.Add(a_particles[i].position, i);
		}
		_grid.End();
	}

	float DensityField::Weight(const Particle& a_particle) const
	{
		return std::clamp(a_particle.life / _fade, 0.0f, 1.0f);
	}

	template <class F>
	void DensityField::ForNearby(Vec3 a_point, F&& a_visit) const
	{
		_grid.ForEachNear(a_point, [&](std::size_t a_index) {
			const auto  delta = a_point - _particles[a_index].position;
			const float distance = LengthSquared(delta);
			if (distance < _radiusSquared) {
				a_visit(_particles[a_index], delta, 1.0f - distance / _radiusSquared);
			}
		});
	}

	float DensityField::Value(Vec3 a_point) const
	{
		float value = 0.0f;
		ForNearby(a_point, [&](const Particle& a_particle, Vec3, float a_falloff) {
			value += Weight(a_particle) * a_falloff * a_falloff * a_falloff;
		});
		return value;
	}

	float DensityField::Count(Vec3 a_point) const
	{
		float value = 0.0f;
		ForNearby(a_point, [&](const Particle&, Vec3, float a_falloff) { value += a_falloff * a_falloff * a_falloff; });
		return value;
	}

	float DensityField::BulkCount(Vec3 a_point) const
	{
		float value = 0.0f;
		ForNearby(a_point, [&](const Particle& a_particle, Vec3, float a_falloff) {
			if (a_particle.bulk) {
				value += a_falloff * a_falloff * a_falloff;
			}
		});
		return value;
	}

	Vec3 DensityField::Normal(Vec3 a_point, Vec3 a_fallback) const
	{
		Vec3 sum{};
		ForNearby(a_point, [&](const Particle& a_particle, Vec3 a_delta, float a_falloff) {
			sum += a_delta * (Weight(a_particle) * a_falloff * a_falloff);
		});
		return Normalize(sum, a_fallback);
	}

	bool WrapSurface(const DensityField& a_field, Vec3 a_center, float a_reach, float a_threshold, std::span<const Vec3> a_directions,
		std::span<const SurfacePoint> a_hints, std::span<SurfacePoint> a_out)
	{
		if (a_out.size() < a_directions.size() || !(a_reach > 0.0f) || a_field.Value(a_center) < a_threshold) {
			return false;
		}
		const bool  warm = a_hints.size() == a_directions.size();
		const float step = a_field.Radius() * 0.25f;
		const auto  inside = [&](Vec3 a_direction, float a_distance) {
			return a_field.Value(a_center + a_direction * a_distance) >= a_threshold;
		};
		for (std::size_t i = 0; i < a_directions.size(); ++i) {
			const Vec3  direction = a_directions[i];
			const float start = warm ? std::clamp(a_hints[i].distance, 0.0f, a_reach) : 0.0f;
			float       low = start;
			float       high = start;
			bool        capped = false;
			if (start <= 0.0f || inside(direction, start)) {
				high = low + step;
				while (high < a_reach && inside(direction, high)) {
					low = high;
					high += step;
				}
				if (high >= a_reach) {
					high = a_reach;
					capped = inside(direction, a_reach);
				}
			} else {
				low = std::max(start - step, 0.0f);
				while (low > 0.0f && !inside(direction, low)) {
					high = low;
					low = std::max(low - step, 0.0f);
				}
			}
			float distance = high;
			if (!capped) {
				for (int b = 0; b < kBisections; ++b) {
					const float middle = 0.5f * (low + high);
					if (inside(direction, middle)) {
						low = middle;
					} else {
						high = middle;
					}
				}
				distance = 0.5f * (low + high);
			}
			const Vec3 point = a_center + direction * distance;
			a_out[i] = { point, a_field.Normal(point, direction), distance };
		}
		return true;
	}
}
