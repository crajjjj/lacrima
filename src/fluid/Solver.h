#pragma once

#include "fluid/Grid.h"
#include "fluid/Particle.h"
#include "fluid/Skin.h"

#include <optional>
#include <span>
#include <vector>

namespace Lacrima::Fluid
{
	struct Parameters
	{
		std::uint32_t iterations{ 4 };
		float         cohesion{ 4.0f };
		float         viscosity{ 40.0f };
		float         gravity{ 9.81f };
		Vec3          down{ 0.0f, 0.0f, -1.0f };  // unit direction gravity pulls in, in simulation space
		float         stretch{ 3.0f };
		float         friction{ 30.0f };
		float         grip{ 0.6f };
		float         cling{ 2.0f };  // seconds a drop hangs from a surface that faces down before it lets go (a lone drop longer, a heavy bead shorter)
	};

	struct Anchor
	{
		std::size_t   particle{};
		Vec3          point;
		float         rest{};
		std::uint32_t owner{};
	};

	struct Capsule
	{
		Vec3  a;
		Vec3  b;
		float radius{};
		Vec3  velocityA;
		Vec3  velocityB;
	};

	struct Hole
	{
		Vec3  center;
		float radius{};
	};

	struct Colliders
	{
		std::span<const Capsule> capsules;
		std::span<const Hole>    holes;
		const Skin*              skin{ nullptr };
	};

	// Position-based fluid in a frame where the skin stands still: a particle at
	// rest on it falls asleep and costs nothing until a neighbour, an anchor or the
	// caller (Wake) stirs it. Capsules move and carry what touches them.
	class Solver
	{
	public:
		Solver(float a_spacing, Parameters a_parameters);

		static bool Valid(const Parameters& a_parameters);
		void        Configure(const Parameters& a_parameters);

		void  Step(std::span<Particle> a_particles, std::span<Anchor> a_anchors, const Colliders& a_colliders, float a_seconds);
		float Spacing() const { return _spacing; }

	private:
		float Weight(Vec3 a_delta) const;
		Vec3  Gradient(Vec3 a_delta) const;
		void  FindNeighbors(std::span<const Vec3> a_positions, std::span<const Particle> a_particles, float a_radiusSquared, bool a_all);
		std::span<const std::uint32_t> Neighbors(std::size_t a_index) const;
		void  Collide(std::span<const Particle> a_particles, const Colliders& a_colliders, bool a_pull);
		static std::optional<Vec3> Carrier(const Colliders& a_colliders, int a_surface, float a_along);

		float                      _spacing;
		float                      _radiusSquared;
		float                      _searchSquared;
		float                      _restWeight{};
		Parameters                 _parameters;
		std::vector<Vec3>          _predicted;
		std::vector<Vec3>          _velocity;
		std::vector<Vec3>          _correction;
		std::vector<float>         _lambda;
		std::vector<std::uint8_t>  _anchored;
		std::vector<float>         _weights;
		std::vector<int>           _contact;
		std::vector<float>         _along;
		std::vector<Vec3>          _normal;
		std::vector<std::uint32_t> _neighborStart;
		std::vector<std::uint32_t> _neighborList;
		CellGrid                   _grid;
	};
}
