#include "camera.h"
#include "config.h"
#include "game_addrs.h"
#include "nvse/GameAPI.h"
#include "nvse/GameObjects.h"
#include "nvse/GameSettings.h"

namespace
{
	// GMSTs capping the 3rd-person zoom distance (g_vanityModePos.y, 0x011E0B5C). Ghidra 2026-10-03, high:
	// - fChaseCameraMax: wheel/stick zoom clamp outside vanity mode (0x00945BB4) and the camera distance cap in
	//   SetThirdPersonCameraPosition (0x0094A3C5). Exe default 120.
	// - fVanityModeWheelMax: the same clamps in vanity mode (0x00945BD1, 0x0094A3E2), which is where gamepad zoom
	//   happens (POV held, camera.md "Gamepad zoom"); also the vanity zoom-blend ratio (0x0094A147). The game data
	//   sets 250 (exe default 600).
	struct RaisedSetting
	{
		const char* name;
		Setting* setting;
		float original;	// game value before we raised it
		float ours;		// value we wrote; 0 = not raised
	};
	RaisedSetting s_settings[] = {
		{ "fChaseCameraMax", nullptr, 0.0f, 0.0f },
		{ "fVanityModeWheelMax", nullptr, 0.0f, 0.0f },
	};

	Setting* FindSetting(const char* name)
	{
		GameSettingCollection* gmsts = GameSettingCollection::GetSingleton();
		Setting* setting = nullptr;
		if (!gmsts || !gmsts->GetGameSetting(name, &setting) || !setting || setting->GetType() != Setting::kSetting_Float)
			return nullptr;
		return setting;
	}
}

void Camera_Acquire()
{
	if (g_config.cameraMaxZoom <= 0.0f)
		return;
	for (RaisedSetting& s : s_settings)
	{
		if (s.ours != 0.0f)
			continue;
		s.setting = FindSetting(s.name);
		if (!s.setting)
		{
			_WARNING("camera: GMST %s not found or not a float, unchanged", s.name);
			continue;
		}
		s.original = s.setting->data.f;
		if (g_config.cameraMaxZoom <= s.original)
		{
			_MESSAGE("camera: %s is already %.0f (>= fMaxZoomDist %.0f), unchanged", s.name, s.original,
				g_config.cameraMaxZoom);
			continue;
		}
		s.ours = g_config.cameraMaxZoom;
		s.setting->data.f = s.ours;
		_MESSAGE("camera: %s %.0f -> %.0f", s.name, s.original, s.ours);
	}
}

void Camera_Release(const char* reason)
{
	for (RaisedSetting& s : s_settings)
	{
		if (s.ours == 0.0f)
			continue;
		// Leave a value someone else wrote meanwhile (script SetGameSetting) alone.
		if (s.setting->data.f == s.ours)
		{
			s.setting->data.f = s.original;
			_MESSAGE("camera: %s restored to %.0f (%s)", s.name, s.original, reason);
		}
		else
			_MESSAGE("camera: %s changed by someone else to %.0f, not restored (%s)", s.name, s.setting->data.f, reason);
		s.ours = 0.0f;
	}
}

void Camera_DumpState()
{
	const float zoom = *(const float*)GameAddr::kCameraZoomDist;
	const bool vanity = *(const bool*)GameAddr::kVanityMode;
	char line[160];
	int len = sprintf_s(line, "camera zoom %.0f, vanity %d", zoom, vanity);
	for (const RaisedSetting& s : s_settings)
	{
		Setting* setting = FindSetting(s.name);
		len += sprintf_s(line + len, sizeof(line) - len, ", %s %.0f (%s)", s.name, setting ? setting->data.f : -1.0f,
			s.ours != 0.0f ? "raised" : "vanilla");
	}
	Console_Print("SM64FNV: %s", line);
	_MESSAGE("state dump %s", line);
}

namespace
{
	bool s_viewPending = false;	// a load/new game happened; the next spawn sets the view
}

void Camera_RequestLoadView()
{
	s_viewPending = g_config.thirdPersonOnLoad;
}

void Camera_ApplyLoadView()
{
	if (!s_viewPending)
		return;
	s_viewPending = false;
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	if (!player)
		return;
	if (*(const bool*)GameAddr::kVanityMode)
	{
		_MESSAGE("camera: load view skipped, vanity mode active");
		return;
	}
	const bool wasThird = player->bThirdPerson;
	ThisStdCall<bool>(GameAddr::kPlayerSetFirstPerson, player, (UInt32)0);
	// After SetFirstPerson, which may reset the distance. Outside vanity mode the clamp is fChaseCameraMax
	// (raised by Camera_Acquire, or the game value with fMaxZoomDist=0).
	Setting* max = FindSetting("fChaseCameraMax");
	float& zoom = *(float*)GameAddr::kCameraZoomDist;
	const float before = zoom;
	if (max)
		zoom = max->data.f;
	_MESSAGE("camera: load view: 3rd person (was %s), zoom %.0f -> %.0f", wasThird ? "3rd" : "1st", before, zoom);
}

// M6.1: 3rd person while Mario is spawned.
namespace
{
	constexpr uint32_t kMaxLoggedForces = 20;
	bool s_restoreFirst = false;	// the player was in 1st person when Mario took over; sticky across door respawns
	uint32_t s_forces = 0;

	void SetThirdPerson(PlayerCharacter* player)
	{
		ThisStdCall<bool>(GameAddr::kPlayerSetFirstPerson, player, (UInt32)0);
		// After SetFirstPerson, which may reset the distance; scrolled all the way in it is ~0, i.e. inside Mario.
		float& zoom = *(float*)GameAddr::kCameraZoomDist;
		if (zoom < g_config.cameraMinZoom)
			zoom = g_config.cameraMinZoom;
	}
}

void Camera_NoteSpawnView()
{
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	if (g_config.forceThirdPerson && player && !player->bThirdPerson && !s_restoreFirst)
	{
		s_restoreFirst = true;
		_MESSAGE("camera: player was in 1st person at spawn; restored when Mario is toggled off");
	}
}

void Camera_KeepThirdPerson()
{
	if (!g_config.forceThirdPerson)
		return;
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	// Vanity mode is the gamepad zoom (POV held) or the idle orbit: leave it to the game.
	if (!player || player->bThirdPerson || *(const bool*)GameAddr::kVanityMode)
		return;
	SetThirdPerson(player);
	if (++s_forces <= kMaxLoggedForces)
		_MESSAGE("camera: view went to 1st person, back to 3rd (zoom %.0f)%s", *(const float*)GameAddr::kCameraZoomDist,
			s_forces == kMaxLoggedForces ? " (no more of these lines)" : "");
}

void Camera_RestoreView(const char* reason)
{
	if (!s_restoreFirst)
		return;
	s_restoreFirst = false;
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	if (!player || !player->bThirdPerson)
		return;
	ThisStdCall<bool>(GameAddr::kPlayerSetFirstPerson, player, (UInt32)1);
	_MESSAGE("camera: back to 1st person (%s)", reason);
}

void Camera_ForgetView()
{
	s_restoreFirst = false;
}

void Camera_DumpView()
{
	PlayerCharacter* player = PlayerCharacter::GetSingleton();
	char line[160];
	sprintf_s(line, "view %s, force 3rd person %s, forced %u times, back to 1st on toggle off: %s",
		player ? (player->bThirdPerson ? "3rd" : "1st") : "?", g_config.forceThirdPerson ? "on" : "off (ini)", s_forces,
		s_restoreFirst ? "yes" : "no");
	Console_Print("SM64FNV: %s", line);
	_MESSAGE("state dump %s", line);
}
