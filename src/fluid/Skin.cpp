#include "fluid/Skin.h"

#include "fluid/Clusters.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Lacrima::Fluid
{
	namespace
	{
		constexpr std::size_t kMaxCells = std::size_t{ 1 } << 21;
	}

	void Skin::Clear()
	{
		_patches.clear();
		_start.clear();
		_order.clear();
		_nx = _ny = _nz = 0;
	}

	bool Skin::Locate(Vec3 a_point, int& a_x, int& a_y, int& a_z) const
	{
		a_x = static_cast<int>(std::floor((a_point.x - _low.x) / _cell));
		a_y = static_cast<int>(std::floor((a_point.y - _low.y) / _cell));
		a_z = static_cast<int>(std::floor((a_point.z - _low.z) / _cell));
		return a_x >= 0 && a_x < _nx && a_y >= 0 && a_y < _ny && a_z >= 0 && a_z < _nz;
	}

	void Skin::Build(float a_cell)
	{
		_start.clear();
		_order.clear();
		_nx = _ny = _nz = 0;
		if (_patches.empty() || !(a_cell > 0.0f) || !std::isfinite(a_cell)) {
			return;
		}
		Vec3 low = _patches.front().position;
		Vec3 high = low;
		for (const auto& patch : _patches) {
			low = { std::min(low.x, patch.position.x), std::min(low.y, patch.position.y), std::min(low.z, patch.position.z) };
			high = { std::max(high.x, patch.position.x), std::max(high.y, patch.position.y), std::max(high.z, patch.position.z) };
		}
		_low = low;
		_cell = a_cell;
		for (;;) {
			_nx = static_cast<int>(std::floor((high.x - low.x) / _cell)) + 1;
			_ny = static_cast<int>(std::floor((high.y - low.y) / _cell)) + 1;
			_nz = static_cast<int>(std::floor((high.z - low.z) / _cell)) + 1;
			if (static_cast<std::size_t>(_nx) * static_cast<std::size_t>(_ny) * static_cast<std::size_t>(_nz) <= kMaxCells) {
				break;
			}
			_cell *= 2.0f;
		}
		const std::size_t cells = static_cast<std::size_t>(_nx) * static_cast<std::size_t>(_ny) * static_cast<std::size_t>(_nz);
		_start.assign(cells + 1, 0);
		std::vector<std::uint32_t> slots(_patches.size());
		for (std::size_t k = 0; k < _patches.size(); ++k) {
			int x = 0;
			int y = 0;
			int z = 0;
			Locate(_patches[k].position, x, y, z);
			x = std::clamp(x, 0, _nx - 1);
			y = std::clamp(y, 0, _ny - 1);
			z = std::clamp(z, 0, _nz - 1);
			const std::size_t cell = (static_cast<std::size_t>(z) * _ny + static_cast<std::size_t>(y)) * _nx + static_cast<std::size_t>(x);
			slots[k] = static_cast<std::uint32_t>(cell);
			++_start[cell + 1];
		}
		for (std::size_t c = 0; c < cells; ++c) {
			_start[c + 1] += _start[c];
		}
		std::vector<std::uint32_t> fill(_start.begin(), _start.end() - 1);
		_order.resize(_patches.size());
		for (std::size_t k = 0; k < _patches.size(); ++k) {
			_order[fill[slots[k]]++] = static_cast<std::uint32_t>(k);
		}
	}

	std::size_t Skin::Nearest(Vec3 a_point) const
	{
		if (_order.empty()) {
			return kNone;
		}
		const float fx = (a_point.x - _low.x) / _cell;
		const float fy = (a_point.y - _low.y) / _cell;
		const float fz = (a_point.z - _low.z) / _cell;
		if (fx < -1.0f || fy < -1.0f || fz < -1.0f || fx > static_cast<float>(_nx) + 1.0f || fy > static_cast<float>(_ny) + 1.0f ||
			fz > static_cast<float>(_nz) + 1.0f) {
			return kNone;
		}
		const int                cx = static_cast<int>(std::floor(fx));
		const int                cy = static_cast<int>(std::floor(fy));
		const int                cz = static_cast<int>(std::floor(fz));
		const std::array<int, 2> xs{ cx, fx - static_cast<float>(cx) < 0.5f ? cx - 1 : cx + 1 };
		const std::array<int, 2> ys{ cy, fy - static_cast<float>(cy) < 0.5f ? cy - 1 : cy + 1 };
		const std::array<int, 2> zs{ cz, fz - static_cast<float>(cz) < 0.5f ? cz - 1 : cz + 1 };
		float                    best = 0.25f * _cell * _cell;
		std::size_t              found = kNone;
		for (const int z : zs) {
			if (z < 0 || z >= _nz) {
				continue;
			}
			for (const int y : ys) {
				if (y < 0 || y >= _ny) {
					continue;
				}
				for (const int x : xs) {
					if (x < 0 || x >= _nx) {
						continue;
					}
					const std::size_t cell = (static_cast<std::size_t>(z) * _ny + static_cast<std::size_t>(y)) * _nx + static_cast<std::size_t>(x);
					for (std::uint32_t i = _start[cell]; i < _start[cell + 1]; ++i) {
						const std::uint32_t k = _order[i];
						const float         distance = LengthSquared(_patches[k].position - a_point);
						if (distance < best) {
							best = distance;
							found = k;
						}
					}
				}
			}
		}
		return found;
	}
}
