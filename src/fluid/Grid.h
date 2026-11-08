#pragma once

#include "fluid/Particle.h"

#include <cmath>
#include <unordered_map>
#include <vector>

namespace Lacrima::Fluid
{
	class CellGrid
	{
	public:
		struct Key
		{
			int  x, y, z;
			bool operator==(const Key&) const = default;
		};

		void Begin(float a_cell)
		{
			_cell = a_cell;
			for (auto& entry : _cells) {
				entry.second.clear();
			}
		}

		void Add(Vec3 a_point, std::size_t a_index) { _cells[KeyOf(a_point)].push_back(a_index); }

		void End()
		{
			std::erase_if(_cells, [](const auto& a_entry) { return a_entry.second.empty(); });
		}

		Key KeyOf(Vec3 a_point) const
		{
			return { static_cast<int>(std::floor(a_point.x / _cell)), static_cast<int>(std::floor(a_point.y / _cell)),
				static_cast<int>(std::floor(a_point.z / _cell)) };
		}

		const std::vector<std::size_t>* Find(Key a_key) const
		{
			const auto cell = _cells.find(a_key);
			return cell != _cells.end() ? &cell->second : nullptr;
		}

	private:
		struct KeyHash
		{
			std::size_t operator()(Key a_key) const noexcept
			{
				return (static_cast<std::size_t>(a_key.x) * 73856093u) ^ (static_cast<std::size_t>(a_key.y) * 19349663u) ^
				       (static_cast<std::size_t>(a_key.z) * 83492791u);
			}
		};

		float                                                      _cell{ 1.0f };
		std::unordered_map<Key, std::vector<std::size_t>, KeyHash> _cells;
	};
}
