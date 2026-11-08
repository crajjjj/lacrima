#pragma once

#include "fluid/Grid.h"
#include "fluid/Particle.h"

#include <limits>
#include <span>
#include <vector>

namespace Lacrima::Fluid
{
	constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

	struct Cluster
	{
		Vec3                     center;
		std::size_t              seed{};
		float                    extent{};
		std::vector<std::size_t> members;
	};

	struct Piece
	{
		std::size_t seed{};
		std::size_t hint{ kNone };
		Vec3        center;
		float       extent{};
		std::size_t count{};
	};

	struct Run
	{
		std::size_t begin{};
		std::size_t end{};

		std::size_t Size() const { return end - begin; }
	};

	std::vector<Cluster> FindClusters(std::span<const Particle> a_particles, std::span<const std::size_t> a_subset, float a_link,
		CellGrid& a_grid);
	std::vector<Piece>   SplitCluster(std::span<const Particle> a_particles, const Cluster& a_cluster, float a_radius,
		  std::span<const std::size_t> a_hints);
	std::vector<Run>     FindRuns(std::span<const Particle> a_particles);
	float                VolumeRadius(std::size_t a_count, float a_volume);
}
