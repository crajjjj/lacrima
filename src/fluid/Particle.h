#pragma once

#include <cmath>
#include <cstdint>

namespace Lacrima::Fluid
{
	struct Vec3
	{
		float x{};
		float y{};
		float z{};
	};

	inline Vec3  operator+(Vec3 a_lhs, Vec3 a_rhs) { return { a_lhs.x + a_rhs.x, a_lhs.y + a_rhs.y, a_lhs.z + a_rhs.z }; }
	inline Vec3  operator-(Vec3 a_lhs, Vec3 a_rhs) { return { a_lhs.x - a_rhs.x, a_lhs.y - a_rhs.y, a_lhs.z - a_rhs.z }; }
	inline Vec3  operator-(Vec3 a_value) { return { -a_value.x, -a_value.y, -a_value.z }; }
	inline Vec3  operator*(Vec3 a_lhs, float a_scale) { return { a_lhs.x * a_scale, a_lhs.y * a_scale, a_lhs.z * a_scale }; }
	inline Vec3& operator+=(Vec3& a_lhs, Vec3 a_rhs) { return a_lhs = a_lhs + a_rhs; }
	inline float Dot(Vec3 a_lhs, Vec3 a_rhs) { return a_lhs.x * a_rhs.x + a_lhs.y * a_rhs.y + a_lhs.z * a_rhs.z; }
	inline float LengthSquared(Vec3 a_value) { return Dot(a_value, a_value); }
	inline float Length(Vec3 a_value) { return std::sqrt(Dot(a_value, a_value)); }
	inline bool  IsFinite(Vec3 a_value) { return std::isfinite(a_value.x) && std::isfinite(a_value.y) && std::isfinite(a_value.z); }

	inline Vec3 Cross(Vec3 a_lhs, Vec3 a_rhs)
	{
		return { a_lhs.y * a_rhs.z - a_lhs.z * a_rhs.y, a_lhs.z * a_rhs.x - a_lhs.x * a_rhs.z, a_lhs.x * a_rhs.y - a_lhs.y * a_rhs.x };
	}

	inline Vec3 Normalize(Vec3 a_value, Vec3 a_fallback)
	{
		const float length = Length(a_value);
		return length > 1e-9f ? a_value * (1.0f / length) : a_fallback;
	}

	inline Vec3 LimitLength(Vec3 a_value, float a_maximum)
	{
		const float length = Length(a_value);
		return length > a_maximum ? a_value * (a_maximum / length) : a_value;
	}

	struct Particle
	{
		Vec3          position;
		Vec3          velocity;
		Vec3          carried;
		float         life{};
		float         link{};
		float         idle{};
		float         along{};
		float         airborne{};  // seconds since the particle last touched a surface
		float         travel{};    // metres moved along a surface since it last stopped
		float         hang{};      // seconds spent clinging to a surface that faces down (under a chin)
		std::int32_t  surface{ -1 };
		std::uint32_t run{};
		std::uint32_t order{};  // a drop left behind by a running tear: its place in that tear's trail (0 = not left behind)
		bool          bulk{};
		bool          asleep{};
		bool          touching{};
	};
}
