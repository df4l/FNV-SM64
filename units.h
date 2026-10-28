#pragma once

#include <cmath>

// FNV (Z-up, game units) <-> SM64 (Y-up, SM64 units). FNV-SM64-RE findings/scale.md.
namespace Units
{
	// SA's MARIO_SCALE (0.0085 m per SM64 unit) x 69.99126 game units per metre.
	constexpr float kFnvPerSm64 = 0.0085f * 69.99126f;
	constexpr float kPi = 3.14159265358979f;

	// sm64 = (x, z, -y) / kFnvPerSm64. The Y flip keeps Mario unmirrored (as in SA).
	inline void FnvToSm64(float fx, float fy, float fz, float out[3])
	{
		out[0] = fx / kFnvPerSm64;
		out[1] = fz / kFnvPerSm64;
		out[2] = -fy / kFnvPerSm64;
	}

	inline void Sm64ToFnv(const float s[3], float& fx, float& fy, float& fz)
	{
		fx = s[0] * kFnvPerSm64;
		fy = -s[2] * kFnvPerSm64;
		fz = s[1] * kFnvPerSm64;
	}

	// Rotation (c[j] = image of local axis j, FNV frame) -> SM64ObjectTransform::eulerRotation (degrees), for local
	// vertices converted with FnvToSm64. libsm64's mtxf_rotate_zxy_and_translate sends local z to
	// (cos p sin y, -sin p, cos p cos y), and its CONVERT_ANGLE negates degrees. Checked offline against that formula.
	inline void RotationToSm64Euler(const float c[3][3], float euler[3])
	{
		auto toSm64 = [](const float* v, float sign, float out[3]) {
			out[0] = sign * v[0];
			out[1] = sign * v[2];
			out[2] = -sign * v[1];
		};
		float x[3], y[3], z[3];
		toSm64(c[0], 1.0f, x);
		toSm64(c[2], 1.0f, y);
		toSm64(c[1], -1.0f, z);
		const float sinPitch = -z[1] > 1.0f ? 1.0f : -z[1] < -1.0f ? -1.0f : -z[1];
		const float pitch = asinf(sinPitch);
		float yaw, roll;
		if (fabsf(sinPitch) < 0.9999f)
		{
			yaw = atan2f(z[0], z[2]);
			roll = atan2f(x[1], y[1]);
		}
		else
		{
			roll = 0.0f;
			yaw = atan2f(-x[2], x[0]);
		}
		euler[0] = -pitch * 180.0f / kPi;
		euler[1] = -yaw * 180.0f / kPi;
		euler[2] = -roll * 180.0f / kPi;
	}

	// FNV heading (rotZ, clockwise from +Y) -> SM64 faceAngle. Inferred in scale.md, sign still to verify in game.
	inline float HeadingToFaceAngle(float heading)
	{
		return kPi - heading;
	}
}
