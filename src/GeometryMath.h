#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include <RE/N/NiPoint3.h>

namespace Lacrima::GeometryMath
{
	struct Frame
	{
		RE::NiPoint3 x{ 1.0f, 0.0f, 0.0f };
		RE::NiPoint3 y{ 0.0f, 1.0f, 0.0f };
		RE::NiPoint3 z{ 0.0f, 0.0f, 1.0f };
	};

	inline Frame TransportFrame(const Frame& a_previous, RE::NiPoint3 a_direction)
	{
		if (a_direction.Unitize() <= 1e-5f) {
			return a_previous;
		}

		const auto& y = a_direction;
		const float cosine = std::clamp(a_previous.y.Dot(y), -1.0f, 1.0f);
		const auto axis = a_previous.y.Cross(y);
		auto x = a_previous.x;
		if (cosine > -0.9999f) {
			x += axis.Cross(x) + axis.Cross(axis.Cross(x)) / (1.0f + cosine);
		}
		x -= y * x.Dot(y);
		if (x.Unitize() <= 1e-5f) {
			x = y.Cross(a_previous.z);
			x.Unitize();
		}
		return { x, y, x.Cross(y) };
	}

	template <std::size_t N>
	void UpdateFrames(std::array<Frame, N>& a_frames, const std::array<RE::NiPoint3, N>& a_positions, int a_breakAt)
	{
		static_assert(N >= 2);
		const bool broken = a_breakAt >= 0 && a_breakAt < static_cast<int>(N) - 1;
		for (std::size_t i = 0; i < N; ++i) {
			auto before = i > 0 ? i - 1 : i;
			auto after = std::min(i + 1, N - 1);
			if (broken && i == static_cast<std::size_t>(a_breakAt)) {
				after = i;
			}
			if (broken && i == static_cast<std::size_t>(a_breakAt + 1)) {
				before = i;
			}
		
			const auto& reference = i == 0 || (broken && i == static_cast<std::size_t>(a_breakAt + 1)) ?
			                            a_frames[i] : a_frames[i - 1];
			a_frames[i] = TransportFrame(reference, a_positions[after] - a_positions[before]);
		}
	}

	inline float DampingPerStep(float a_retention, float a_step)
	{
		return a_step > 0.0f ? std::pow(std::clamp(a_retention, 0.0f, 1.0f), a_step * 120.0f) : 1.0f;
	}

	inline float DisplacementScale(float a_step, float a_previousStep)
	{
		return a_previousStep > 0.0f ? a_step / a_previousStep : 1.0f;
	}
}
