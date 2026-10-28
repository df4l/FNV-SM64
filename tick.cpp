#include "tick.h"
#include "game_state.h"
#include "mario.h"
#include "player_control.h"
#include "control_filter.h"
#include "player_hide.h"
#include "camera.h"
#include "audio.h"
#include "nvse/GameAPI.h"

namespace
{
	constexpr double kTickSeconds = 1.0 / 30.0;
	// Caps catch-up after hitches/loads at 3 ticks (CLAUDE.md §4 "clamp large dt").
	constexpr double kMaxFrameSeconds = 0.1;
	constexpr uint32_t kLogEveryTicks = 300;

	LARGE_INTEGER s_freq{};
	LARGE_INTEGER s_last{};
	double s_accumulator = 0.0;
	uint32_t s_ticks = 0;
	uint32_t s_framesSinceLog = 0;
	double s_secondsSinceLog = 0.0;
	bool s_active = true;
	bool s_running = false;
	bool s_started = false;

	void LogState(const char* prefix, const GameState& s)
	{
		_MESSAGE("%s (active=%d menu=%d fader=%d console=%d freeze=%d loading=%d loaded=%d) ticks=%u",
			prefix, s_active, s.menuMode, s.faderToBlack, s.consoleOpen, s.freezeTime, s.loadingScreen, s.gameLoaded, s_ticks);
	}
}

void Tick_OnMainGameLoop()
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (!s_started)
	{
		QueryPerformanceFrequency(&s_freq);
		s_last = now;
		s_started = true;
	}
	// Real (unscaled) time: SM64 runs at its own speed regardless of the game's time multiplier (D6).
	double dt = double(now.QuadPart - s_last.QuadPart) / double(s_freq.QuadPart);
	s_last = now;

	const GameState state = GameState_Read();
	const bool running = s_active && state.SimulationRunning();
	if (running != s_running)
	{
		s_running = running;
		// Time spent paused never reaches the accumulator. Its sub-tick fraction is kept so the interpolated
		// position doesn't snap back on resume.
		dt = 0.0;
		LogState(running ? "simulation running" : "simulation paused", state);
	}
	// Also while paused: closing a menu can switch the view back to 3rd person and show the body.
	if (Mario_IsSpawned())
		PlayerHide_Apply();
	if (!running)
	{
		Audio_Update(false);
		return;
	}
	if (Mario_IsSpawned())
		Camera_KeepThirdPerson();

	if (dt > kMaxFrameSeconds)
	{
		_MESSAGE("frame dt %.3f s clamped to %.3f s", dt, kMaxFrameSeconds);
		dt = kMaxFrameSeconds;
	}

	s_accumulator += dt;
	s_framesSinceLog++;
	s_secondsSinceLog += dt;
	while (s_accumulator >= kTickSeconds)
	{
		s_accumulator -= kTickSeconds;
		Mario_Tick();
		if (++s_ticks % kLogEveryTicks == 0)
		{
			_MESSAGE("tick %u (%.1f fps over the last %u ticks)", s_ticks, s_framesSinceLog / s_secondsSinceLog, kLogEveryTicks);
			s_framesSinceLog = 0;
			s_secondsSinceLog = 0.0;
		}
	}
	// How far this frame is between the last two ticks (SA: ticks / (1.f/30)).
	const float alpha = float(s_accumulator / kTickSeconds);
	Mario_SyncPlayer(alpha);
	Mario_UpdateRender(alpha);
	Audio_Update(Mario_IsSpawned());
}

void Tick_DumpState()
{
	const GameState s = GameState_Read();
	Console_Print("SM64FNV: ticks=%u active=%d running=%d menu=%d fader=%d console=%d freeze=%d loading=%d loaded=%d",
		s_ticks, s_active, s_active && s.SimulationRunning(), s.menuMode, s.faderToBlack, s.consoleOpen, s.freezeTime, s.loadingScreen, s.gameLoaded);
	LogState("state dump", s);
	PlayerControl_DumpState();
	ControlFilter_DumpState();
	PlayerHide_DumpState();
	Mario_DumpState();
	Audio_DumpState();
}

void Tick_InitActive(bool active)
{
	s_active = active;
	_MESSAGE("master toggle at start: SM64 %s", active ? "ON" : "OFF");
}

void Tick_SetActive(bool active)
{
	if (active == s_active)
		return;
	s_active = active;
	_MESSAGE("master toggle: SM64 %s", active ? "ON" : "OFF");
	Console_Print("SM64FNV: Mario %s", active ? "ON" : "OFF");
	// ON spawns lazily on the next running tick, at the player's current position.
	if (active)
		Mario_ResetFailure();
	else
		Mario_Despawn("master toggle off");
		Camera_RestoreView("master toggle off");
}

bool Tick_IsActive()
{
	return s_active;
}
