#include "mario_input.h"
#include "sm64_loader.h"
#include "game_addrs.h"
#include "units.h"
#include "config.h"
#include "control_filter.h"
#include "nvse/GameAPI.h"
#include "nvse/GameObjects.h"
#include "nvse/GameOSDepend.h"
#include "nvse/Utilities.h"

#include <cmath>

namespace
{
	// ControlCode (FNV-SM64-RE findings/input.md §3, JIP enum; indexes the game's bind tables).
	constexpr UInt32 kForward = 0, kBackward = 1, kLeft = 2, kRight = 3, kAttack = 4, kSneak = 8, kRun = 9, kJump = 0xC;
	constexpr UInt32 kKeyHeld = 0;	// KeyState isHeld
	// Stick length while Run is held, i.e. walking with the game's default Always Run on (SA's walkState factor).
	constexpr float kWalkStick = 0.65f;
	constexpr uint32_t kMaxChangeLogs = 200;

	struct Controls
	{
		bool forward, backward, left, right, attack, sneak, run, jump;
	};

	enum class CamSource { None, Camera, Heading };

	Controls s_controls{};
	CamSource s_camSource = CamSource::None;
	float s_camYawDeg = 0.0f;	// FNV heading of the camera's horizontal forward (clockwise from +Y)
	bool s_padActive = false;
	bool s_usedStick = false;	// the last read took its direction from the gamepad stick
	float s_padX = 0.0f, s_padY = 0.0f;	// left stick after the deadzone, right/forward = +1
	SM64MarioInputs s_last{};
	uint32_t s_changeLogs = 0;
	bool s_loggedFirst = false;
	bool s_readFailed = false;

	bool Held(OSInputGlobals* input, UInt32 code)
	{
		return ControlFilter_ReadUnfiltered(input, code, kKeyHeld) > 0;
	}

	// No C++ objects here, so __try is allowed: a fault leaves Mario without input instead of crashing.
	bool ReadControlsGuarded(Controls* out)
	{
		__try
		{
			OSInputGlobals* input = *g_OSInputGlobals;
			if (!input)
				return false;
			out->forward = Held(input, kForward);
			out->backward = Held(input, kBackward);
			out->left = Held(input, kLeft);
			out->right = Held(input, kRight);
			out->attack = Held(input, kAttack);
			out->sneak = Held(input, kSneak);
			out->run = Held(input, kRun);
			out->jump = Held(input, kJump);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// Raw left stick from the game's XInput state (filled by Poll, so one frame old here, like the controls).
	bool ReadStickGuarded(SInt16* lx, SInt16* ly)
	{
		__try
		{
			const UInt8* state = (const UInt8*)GameAddr::kXInputState;
			*lx = *(const SInt16*)(state + GameAddr::kXInputThumbLX);
			*ly = *(const SInt16*)(state + GameAddr::kXInputThumbLY);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// Radial deadzone, then rescaled so the stick still reaches 1 at full tilt (SM64 applies its own small one).
	void ReadPadStick()
	{
		s_padX = s_padY = 0.0f;
		s_padActive = *(const UInt8*)GameAddr::kGamepadActive != 0;
		SInt16 lx = 0, ly = 0;
		if (!s_padActive || !ReadStickGuarded(&lx, &ly))
			return;
		const float x = lx / 32767.0f, y = ly / 32767.0f;
		const float len = sqrtf(x * x + y * y);
		const float dz = g_config.stickDeadzone;
		if (len <= dz || dz >= 1.0f)
			return;
		const float scale = (len > 1.0f ? 1.0f : (len - dz) / (1.0f - dz)) / len;
		s_padX = x * scale;
		s_padY = y * scale;
	}

	// World camera forward = world rotate column 0 (camera looks along local +X), row-major NiMatrix3 at +0x68.
	bool ReadCameraForwardGuarded(float* fx, float* fy)
	{
		__try
		{
			const UInt8* camera = CdeclCall<const UInt8*>(GameAddr::kWorldRootCamera);
			if (!camera)
				return false;
			const float* rotate = (const float*)(camera + GameAddr::kAVWorldRotate);
			*fx = rotate[0];
			*fy = rotate[3];
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// Horizontal forward in FNV (x, y), unit length. Falls back to the player's heading (the 3rd-person camera's yaw
	// before vanity offsets, camera.md §2) if the camera is missing or looks straight up/down.
	CamSource CameraForward(float& fx, float& fy)
	{
		if (ReadCameraForwardGuarded(&fx, &fy))
		{
			const float len = sqrtf(fx * fx + fy * fy);
			if (len > 0.01f)
			{
				fx /= len;
				fy /= len;
				return CamSource::Camera;
			}
		}
		if (const PlayerCharacter* player = PlayerCharacter::GetSingleton())
		{
			fx = sinf(player->rotZ);
			fy = cosf(player->rotZ);
			return CamSource::Heading;
		}
		return CamSource::None;
	}

	const char* CamSourceName(CamSource s)
	{
		switch (s)
		{
		case CamSource::Camera: return "camera";
		case CamSource::Heading: return "player heading";
		default: return "none";
		}
	}

	float HeadingDeg(float fx, float fy)
	{
		float deg = atan2f(fx, fy) * (180.0f / Units::kPi);
		return deg < 0.0f ? deg + 360.0f : deg;
	}

	bool SameInputs(const SM64MarioInputs& a, const SM64MarioInputs& b)
	{
		return a.stickX == b.stickX && a.stickY == b.stickY && a.buttonA == b.buttonA && a.buttonB == b.buttonB &&
			a.buttonZ == b.buttonZ;
	}

	void LogInputs(const char* prefix, const SM64MarioInputs& in)
	{
		_MESSAGE("%s stick (%.2f, %.2f) from %s A=%d B=%d Z=%d run=%d, camera yaw %.0f deg from %s, camLook (%.2f, %.2f), gamepad %d",
			prefix, in.stickX, in.stickY, s_usedStick ? "pad" : "keys", in.buttonA, in.buttonB, in.buttonZ, s_controls.run,
			s_camYawDeg, CamSourceName(s_camSource), in.camLookX, in.camLookZ, s_padActive);
	}
}

void MarioInput_Read(SM64MarioInputs& out)
{
	out = {};
	if (!ReadControlsGuarded(&s_controls))
	{
		s_controls = {};
		if (!s_readFailed)
		{
			s_readFailed = true;
			_ERROR("input: GetControlState faulted or no OSInputGlobals; Mario gets no input");
		}
	}

	float fx = 0.0f, fy = 1.0f;
	s_camSource = CameraForward(fx, fy);
	s_camYawDeg = HeadingDeg(fx, fy);
	// SM64 camera yaw points from the focus back to the camera: camLook = -forward, in SM64 axes (x, -y).
	out.camLookX = -fx;
	out.camLookZ = fy;

	// libsm64 negates stickX (gController.stickX = -64 * stickX): -1 = right, +1 on stickY = forward (as SA).
	// A tilted gamepad stick wins over the digital controls; its tilt already sets the speed, so Run doesn't apply.
	ReadPadStick();
	s_usedStick = s_padX != 0.0f || s_padY != 0.0f;
	float x, y;
	if (s_usedStick)
	{
		x = s_padX;
		y = s_padY;
	}
	else
	{
		x = float(s_controls.right) - float(s_controls.left);
		y = float(s_controls.forward) - float(s_controls.backward);
		const float len = sqrtf(x * x + y * y);
		if (len > 1.0f)
		{
			x /= len;
			y /= len;
		}
		if (s_controls.run)
		{
			x *= kWalkStick;
			y *= kWalkStick;
		}
	}
	out.stickX = -x;
	out.stickY = y;
	out.buttonA = s_controls.jump;
	out.buttonB = s_controls.attack;
	out.buttonZ = s_controls.sneak;

	if (!s_loggedFirst)
	{
		s_loggedFirst = true;
		LogInputs("input: first read,", out);
	}
	else if (!SameInputs(out, s_last) && s_changeLogs < kMaxChangeLogs)
	{
		if (++s_changeLogs == kMaxChangeLogs)
			_MESSAGE("input: change log limit reached, further changes not logged");
		LogInputs("input:", out);
	}
	s_last = out;
}

void MarioInput_Reset()
{
	s_last = {};
	s_loggedFirst = false;
}

void MarioInput_DumpState()
{
	const PlayerCharacter* player = PlayerCharacter::GetSingleton();
	const float headingDeg = player ? HeadingDeg(sinf(player->rotZ), cosf(player->rotZ)) : 0.0f;
	Console_Print("SM64FNV: input stick (%.2f, %.2f) from %s, gamepad %d, A=%d B=%d Z=%d; camera yaw %.0f (%s), player heading %.0f",
		s_last.stickX, s_last.stickY, s_usedStick ? "pad" : "keys", s_padActive, s_last.buttonA, s_last.buttonB, s_last.buttonZ, s_camYawDeg,
		CamSourceName(s_camSource), headingDeg);
	_MESSAGE("state dump input: player heading %.0f deg", headingDeg);
	LogInputs("state dump input:", s_last);
}
