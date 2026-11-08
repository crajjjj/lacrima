#pragma once

#include "fluid/Particle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Lacrima::Fluid
{
	// A uniform grid over a point set, rebuilt each time it is used. The points are
	// sorted by cell key with x in the low bits, so the three x-adjacent cells of one
	// row are one contiguous key range and the 27 cells around a point are nine range
	// scans: no hashing, no per-cell allocation, and a rebuild is one sort of a few
	// hundred entries.
	class CellGrid
	{
	public:
		void Begin(float a_cell)
		{
			_cell = std::isfinite(a_cell) && a_cell > 0.0f ? a_cell : 1.0f;
			_entries.clear();
		}

		void Add(Vec3 a_point, std::size_t a_index)
		{
			const auto cell = CellOf(a_point);
			_entries.push_back({ Key(cell.x, cell.y, cell.z), static_cast<std::uint32_t>(a_index) });
		}

		void End()
		{
			std::sort(_entries.begin(), _entries.end(), [](const Entry& a_lhs, const Entry& a_rhs) { return a_lhs.key < a_rhs.key; });
		}

		// Visits the index of every point in the 27 cells around a_point's cell, which
		// covers every point within one cell size of a_point.
		template <class F>
		void ForEachNear(Vec3 a_point, F&& a_visit) const
		{
			const auto cell = CellOf(a_point);
			for (int dz = -1; dz <= 1; ++dz) {
				for (int dy = -1; dy <= 1; ++dy) {
					const std::uint64_t low = Key(cell.x - 1, cell.y + dy, cell.z + dz);
					const std::uint64_t high = Key(cell.x + 1, cell.y + dy, cell.z + dz);
					auto                it = std::lower_bound(_entries.begin(), _entries.end(), low,
						               [](const Entry& a_entry, std::uint64_t a_key) { return a_entry.key < a_key; });
					for (; it != _entries.end() && it->key <= high; ++it) {
						a_visit(static_cast<std::size_t>(it->index));
					}
				}
			}
		}

	private:
		struct Entry
		{
			std::uint64_t key;
			std::uint32_t index;
		};

		struct Cell
		{
			int x, y, z;
		};

		static constexpr int kBits = 21;
		static constexpr int kOffset = 1 << (kBits - 1);
		static constexpr int kLast = (1 << kBits) - 1;

		Cell CellOf(Vec3 a_point) const
		{
			const auto axis = [&](float a_value) {
				const float cells = a_value / _cell;
				const int   index = std::isfinite(cells) ? static_cast<int>(std::floor(std::clamp(cells, -1.0e6f, 1.0e6f))) : 0;
				return std::clamp(index + kOffset, 1, kLast - 1);
			};
			return { axis(a_point.x), axis(a_point.y), axis(a_point.z) };
		}

		static std::uint64_t Key(int a_x, int a_y, int a_z)
		{
			return (static_cast<std::uint64_t>(a_z) << (2 * kBits)) | (static_cast<std::uint64_t>(a_y) << kBits) |
			       static_cast<std::uint64_t>(a_x);
		}

		float              _cell{ 1.0f };
		std::vector<Entry> _entries;
	};
}
