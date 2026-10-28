#pragma once

// Snapshot of the engine's pause/menu/loading flags (findings/main-loop.md §3).
struct GameState
{
	bool menuMode;
	bool faderToBlack;
	bool consoleOpen;
	bool freezeTime;
	bool loadingScreen;
	bool gameLoaded;	// tracked from NVSE load/new-game/exit messages

	// Main::OnIdle's own "simulation running" test, plus loading screen and loaded-game checks.
	bool SimulationRunning() const
	{
		return gameLoaded && !loadingScreen && (!menuMode || faderToBlack) && !consoleOpen && !freezeTime;
	}
};

GameState GameState_Read();
void GameState_SetLoaded(bool loaded);
