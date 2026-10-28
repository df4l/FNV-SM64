#include "game_state.h"
#include "game_addrs.h"
#include "nvse/GameOSDepend.h"

namespace
{
	bool s_gameLoaded = false;

	bool ReadBool(uintptr_t addr) { return *reinterpret_cast<const bool*>(addr); }
}

GameState GameState_Read()
{
	GameState s{};
	s.menuMode = ReadBool(GameAddr::kInMenuMode);
	s.faderToBlack = ReadBool(GameAddr::kFaderToBlackVisible);
	s.consoleOpen = ReadBool(GameAddr::kConsoleOpen);
	const OSGlobals* main = *g_osGlobals;
	s.freezeTime = main && ReadBool(reinterpret_cast<uintptr_t>(main) + GameAddr::kMainFreezeTimeOffset);
	s.loadingScreen = *reinterpret_cast<void* const*>(GameAddr::kLoadingMenuPtr) != nullptr;
	s.gameLoaded = s_gameLoaded;
	return s;
}

void GameState_SetLoaded(bool loaded)
{
	if (loaded != s_gameLoaded)
		_MESSAGE("game loaded: %d", loaded);
	s_gameLoaded = loaded;
}
