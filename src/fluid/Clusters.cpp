#include "fluid/Clusters.h"

#include <algorithm>
#include <numbers>
#include <numeric>
#include <unordered_map>

namespace Lacrima::Fluid
{
	namespace
	{
		std::size_t Root(std::vector<std::size_t>& a_parent, std::size_t a_index)
		{
			while (a_parent[a_index] != a_index) {
				a_parent[a_index] = a_parent[a_parent[a_index]];
				a_index = a_parent[a_index];
			}
			return a_index;
		}
	}

	std::vector<Cluster> FindClusters(std::span<const Particle> a_particles, std::span<const std::size_t> a_subset, float a_link,
		CellGrid& a_grid)
	{
		std::vector<Cluster> result;
		if (a_subset.empty() || !(a_link > 0.0f)) {
			return result;
		}
		a_grid.Begin(a_link);
		for (std::size_t k = 0; k < a_subset.size(); ++k) {
			a_grid.Add(a_particles[a_subset[k]].position, k);
		}
		a_grid.End();
		std::vector<std::size_t> parent(a_subset.size());
		std::iota(parent.begin(), parent.end(), std::size_t{ 0 });
		const float limit = a_link * a_link;
		for (std::size_t k = 0; k < a_subset.size(); ++k) {
			const Vec3 position = a_particles[a_subset[k]].position;
			const auto origin = a_grid.KeyOf(position);
			for (int dx = -1; dx <= 1; ++dx) {
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dz = -1; dz <= 1; ++dz) {
						const auto* cell = a_grid.Find({ origin.x + dx, origin.y + dy, origin.z + dz });
						if (!cell) {
							continue;
						}
						for (const auto other : *cell) {
							if (other > k && LengthSquared(a_particles[a_subset[other]].position - position) <= limit) {
								parent[Root(parent, other)] = Root(parent, k);
							}
						}
					}
				}
			}
		}

		std::unordered_map<std::size_t, std::size_t> index;
		for (std::size_t k = 0; k < a_subset.size(); ++k) {
			const auto [slot, added] = index.try_emplace(Root(parent, k), result.size());
			if (added) {
				result.emplace_back();
			}
			auto& cluster = result[slot->second];
			cluster.center += a_particles[a_subset[k]].position;
			cluster.members.push_back(a_subset[k]);
		}
		for (auto& cluster : result) {
			cluster.center = cluster.center * (1.0f / static_cast<float>(cluster.members.size()));
			float nearest = std::numeric_limits<float>::max();
			for (const auto member : cluster.members) {
				const float distance = Length(a_particles[member].position - cluster.center);
				cluster.extent = std::max(cluster.extent, distance);
				if (distance < nearest) {
					nearest = distance;
					cluster.seed = member;
				}
			}
		}
		return result;
	}

	std::vector<Piece> SplitCluster(std::span<const Particle> a_particles, const Cluster& a_cluster, float a_radius,
		std::span<const std::size_t> a_hints)
	{
		std::vector<Piece> pieces;
		const auto         position = [&](std::size_t a_index) { return a_particles[a_index].position; };
		const auto         covered = [&](Vec3 a_point, float a_reach) {
			return std::ranges::any_of(pieces, [&](const Piece& a_piece) { return Length(position(a_piece.seed) - a_point) <= a_reach; });
		};
		for (std::size_t h = 0; h < a_hints.size(); ++h) {
			if (!covered(position(a_hints[h]), a_radius * 0.6f)) {
				pieces.push_back({ .seed = a_hints[h], .hint = h });
			}
		}
		if (pieces.empty() && a_cluster.extent <= a_radius) {
			pieces.push_back({ .seed = a_cluster.seed });
		}
		for (const auto member : a_cluster.members) {
			if (!covered(position(member), a_radius)) {
				pieces.push_back({ .seed = member });
			}
		}

		std::vector<std::size_t> owner(a_cluster.members.size());
		for (std::size_t m = 0; m < a_cluster.members.size(); ++m) {
			const Vec3 point = position(a_cluster.members[m]);
			float      best = std::numeric_limits<float>::max();
			for (std::size_t p = 0; p < pieces.size(); ++p) {
				const float distance = LengthSquared(position(pieces[p].seed) - point);
				if (distance < best) {
					best = distance;
					owner[m] = p;
				}
			}
			pieces[owner[m]].center += point;
			++pieces[owner[m]].count;
		}
		std::vector<float>       nearest(pieces.size(), std::numeric_limits<float>::max());
		std::vector<std::size_t> middle(pieces.size());
		for (auto& piece : pieces) {
			piece.center = piece.center * (1.0f / static_cast<float>(std::max<std::size_t>(piece.count, 1)));
		}
		for (std::size_t m = 0; m < a_cluster.members.size(); ++m) {
			const float distance = Length(position(a_cluster.members[m]) - pieces[owner[m]].center);
			if (distance < nearest[owner[m]]) {
				nearest[owner[m]] = distance;
				middle[owner[m]] = a_cluster.members[m];
			}
		}
		for (std::size_t p = 0; p < pieces.size(); ++p) {
			if (Length(position(pieces[p].seed) - pieces[p].center) > a_radius * 0.5f) {
				pieces[p].seed = middle[p];
			}
		}
		for (std::size_t m = 0; m < a_cluster.members.size(); ++m) {
			auto& piece = pieces[owner[m]];
			piece.extent = std::max(piece.extent, Length(position(a_cluster.members[m]) - position(piece.seed)));
		}
		return pieces;
	}

	std::vector<Run> FindRuns(std::span<const Particle> a_particles)
	{
		std::vector<Run> runs;
		for (std::size_t i = 0; i < a_particles.size(); ++i) {
			const auto& particle = a_particles[i];
			if (particle.bulk || particle.run == 0) {
				continue;
			}
			if (!runs.empty() && runs.back().end == i && a_particles[i - 1].run == particle.run && a_particles[i - 1].link > 0.0f) {
				runs.back().end = i + 1;
			} else {
				runs.push_back({ i, i + 1 });
			}
		}
		return runs;
	}

	float VolumeRadius(std::size_t a_count, float a_volume)
	{
		const double volume = static_cast<double>(a_volume) * static_cast<double>(a_count);
		return static_cast<float>(std::cbrt(3.0 * volume / (4.0 * std::numbers::pi)));
	}
}
