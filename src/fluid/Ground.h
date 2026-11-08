#pragma once

#include "fluid/Particle.h"

#include <optional>
#include <unordered_map>

namespace Lacrima::Fluid
{
	class Ground
	{
	public:
		explicit Ground(float a_cell);

		float Cell() const { return _cell; }
		bool  Known(int a_x, int a_y) const;
		void  Set(int a_x, int a_y, float a_height);
		int   Node(float a_value) const;
		float Height(float a_x, float a_y) const;
		Vec3  Gradient(float a_x, float a_y) const;

	private:
		static std::uint64_t Key(int a_x, int a_y);
		std::optional<float> At(int a_x, int a_y) const;

		float                                   _cell;
		std::unordered_map<std::uint64_t, float> _heights;
	};
}
