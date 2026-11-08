#include "fluid/Ground.h"

#include <algorithm>
#include <array>
#include <limits>

namespace Lacrima::Fluid
{
	namespace
	{
		constexpr float kCliff = 0.03f;
	}

	Ground::Ground(float a_cell) :
		_cell(a_cell)
	{}

	std::uint64_t Ground::Key(int a_x, int a_y)
	{
		return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a_x)) << 32) | static_cast<std::uint32_t>(a_y);
	}

	bool Ground::Known(int a_x, int a_y) const
	{
		return _heights.contains(Key(a_x, a_y));
	}

	void Ground::Set(int a_x, int a_y, float a_height)
	{
		_heights[Key(a_x, a_y)] = a_height;
	}

	int Ground::Node(float a_value) const
	{
		return static_cast<int>(std::floor(a_value / _cell));
	}

	std::optional<float> Ground::At(int a_x, int a_y) const
	{
		const auto it = _heights.find(Key(a_x, a_y));
		return it != _heights.end() ? std::optional<float>(it->second) : std::nullopt;
	}

	float Ground::Height(float a_x, float a_y) const
	{
		const float u = a_x / _cell;
		const float v = a_y / _cell;
		const int   x = static_cast<int>(std::floor(u));
		const int   y = static_cast<int>(std::floor(v));
		const float fu = u - static_cast<float>(x);
		const float fv = v - static_cast<float>(y);
		const std::array corners{ At(x, y), At(x + 1, y), At(x, y + 1), At(x + 1, y + 1) };
		float       lowest = std::numeric_limits<float>::max();
		float       highest = std::numeric_limits<float>::lowest();
		bool        complete = true;
		for (const auto& corner : corners) {
			if (corner) {
				lowest = std::min(lowest, *corner);
				highest = std::max(highest, *corner);
			} else {
				complete = false;
			}
		}
		if (highest == std::numeric_limits<float>::lowest()) {
			return 0.0f;
		}
		if (!complete) {
			return highest;
		}
		if (highest - lowest > kCliff) {
			const int nearest = (fu < 0.5f ? 0 : 1) + (fv < 0.5f ? 0 : 2);
			return *corners[static_cast<std::size_t>(nearest)];
		}
		const float bottom = *corners[0] + (*corners[1] - *corners[0]) * fu;
		const float top = *corners[2] + (*corners[3] - *corners[2]) * fu;
		return bottom + (top - bottom) * fv;
	}

	Vec3 Ground::Gradient(float a_x, float a_y) const
	{
		const float u = a_x / _cell;
		const float v = a_y / _cell;
		const int   x = static_cast<int>(std::floor(u));
		const int   y = static_cast<int>(std::floor(v));
		const auto  h00 = At(x, y);
		const auto  h10 = At(x + 1, y);
		const auto  h01 = At(x, y + 1);
		const auto  h11 = At(x + 1, y + 1);
		if (!h00 || !h10 || !h01 || !h11) {
			return {};
		}
		const float highest = std::max({ *h00, *h10, *h01, *h11 });
		const float lowest = std::min({ *h00, *h10, *h01, *h11 });
		if (highest - lowest > kCliff) {
			return {};
		}
		const float fu = u - static_cast<float>(x);
		const float fv = v - static_cast<float>(y);
		return { ((*h10 - *h00) * (1.0f - fv) + (*h11 - *h01) * fv) / _cell, ((*h01 - *h00) * (1.0f - fu) + (*h11 - *h10) * fu) / _cell, 0.0f };
	}
}
