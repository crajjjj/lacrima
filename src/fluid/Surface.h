#pragma once

#include "fluid/Grid.h"
#include "fluid/Particle.h"

#include <span>
#include <vector>

namespace Lacrima::Fluid
{
	struct SurfacePoint
	{
		Vec3  position;
		Vec3  normal;
		float distance{};
	};

	class DensityField
	{
	public:
		void  Rebuild(std::span<const Particle> a_particles, float a_radius, float a_fade);
		float Value(Vec3 a_point) const;
		float Count(Vec3 a_point) const;
		float BulkCount(Vec3 a_point) const;
		Vec3  Normal(Vec3 a_point, Vec3 a_fallback) const;
		float Radius() const { return _radius; }
		float Weight(const Particle& a_particle) const;

	private:
		template <class F>
		void ForNearby(Vec3 a_point, F&& a_visit) const;

		std::span<const Particle> _particles;
		float                     _radius{ 1.0f };
		float                     _radiusSquared{ 1.0f };
		float                     _fade{ 1.0f };
		CellGrid                  _grid;
	};

	bool WrapSurface(const DensityField& a_field, Vec3 a_center, float a_reach, float a_threshold, std::span<const Vec3> a_directions,
		std::span<const SurfacePoint> a_hints, std::span<SurfacePoint> a_out);
}
