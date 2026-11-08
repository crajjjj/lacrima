#pragma once

#include "fluid/Ground.h"
#include "fluid/Particle.h"
#include "fluid/Skin.h"

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Lacrima::Fluid
{
	struct Parameters
	{
		std::uint32_t iterations{ 4 };
		float         cohesion{ 4.0f };
		float         viscosity{ 40.0f };
		float         gravity{ 9.81f };
		float         stretch{ 3.0f };
		float         friction{ 30.0f };
		float         grip{ 0.6f };
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

	class Solver
	{
	public:
		Solver(float a_spacing, Parameters a_parameters);

		static bool Valid(const Parameters& a_parameters);
		void        Configure(const Parameters& a_parameters);

		void  Step(std::span<Particle> a_particles, std::span<Anchor> a_anchors, const Colliders& a_colliders, const Ground& a_ground, float a_seconds);
		float Spacing() const { return _spacing; }

	private:
		struct Cell
		{
			int  x, y, z;
			bool operator==(const Cell&) const = default;
		};

		struct CellHash
		{
			std::size_t operator()(Cell a_cell) const noexcept;
		};

		Cell  CellOf(Vec3 a_point) const;
		float Weight(Vec3 a_delta) const;
		Vec3  Gradient(Vec3 a_delta) const;
		void  FindNeighbors(std::span<const Particle> a_particles, bool a_all);
		void  Collide(std::span<const Particle> a_particles, const Colliders& a_colliders, bool a_pull);
		static std::optional<Vec3> Carrier(const Colliders& a_colliders, int a_surface, float a_along, Vec3 a_position);

		float                                                _spacing;
		float                                                _radiusSquared;
		float                                                _restWeight{};
		Parameters                                           _parameters;
		std::vector<Vec3>                                    _predicted;
		std::vector<Vec3>                                    _velocity;
		std::vector<Vec3>                                    _correction;
		std::vector<float>                                   _lambda;
		std::vector<bool>                                    _anchored;
		std::vector<float>                                   _floor;
		std::vector<float>                                   _weights;
		std::vector<int>                                     _contact;
		std::vector<float>                                   _along;
		std::vector<Vec3>                                    _normal;
		std::vector<std::vector<std::size_t>>                _neighbors;
		std::unordered_map<Cell, std::vector<std::size_t>, CellHash> _grid;
	};
}
