#pragma once

#include "fluid/Particle.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Lacrima::Fluid
{
	// A point on a surface the fluid collides with. Surfaces are static in
	// simulation space (the tears live in the head's own frame), so a patch carries
	// no velocity.
	struct Patch
	{
		Vec3 position;
		Vec3 normal;
	};

	class Skin
	{
	public:
		void Build(float a_cell);
		void Clear();

		std::vector<Patch>&       Patches() { return _patches; }
		const std::vector<Patch>& Patches() const { return _patches; }
		std::size_t               Nearest(Vec3 a_point) const;

	private:
		bool Locate(Vec3 a_point, int& a_x, int& a_y, int& a_z) const;

		float                      _cell{ 0.05f };
		Vec3                       _low;
		int                        _nx{ 0 };
		int                        _ny{ 0 };
		int                        _nz{ 0 };
		std::vector<Patch>         _patches;
		std::vector<std::uint32_t> _start;
		std::vector<std::uint32_t> _order;
	};
}
