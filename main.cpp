#include "nvse/PluginAPI.h"
#include "nvse/GameAPI.h"
#include "nvse/GameOSDepend.h"
#include "sm64_loader.h"
#include "config.h"
#include "game_state.h"
#include "tick.h"
#include "mario.h"
#include "player_control.h"
#include "control_filter.h"
#include "mario_render.h"
#include "collision_objects.h"
#include "camera.h"
#include "player_hide.h"
#include "audio.h"

IDebugLog		gLog("nvse_sm64.log");
PluginHandle	g_pluginHandle = kPluginHandle_Invalid;

NVSEMessagingInterface* g_messagingInterface{};
NVSEInterface* g_nvseInterface{};

static constexpr UInt32 kPluginVersion = 1;

static const char* MessageName(UInt32 type)
{
	switch (type)
	{
	case NVSEMessagingInterface::kMessage_PostLoad: return "PostLoad";
	case NVSEMessagingInterface::kMessage_ExitGame: return "ExitGame";
	case NVSEMessagingInterface::kMessage_ExitToMainMenu: return "ExitToMainMenu";
	case NVSEMessagingInterface::kMessage_LoadGame: return "LoadGame";
	case NVSEMessagingInterface::kMessage_SaveGame: return "SaveGame";
	case NVSEMessagingInterface::kMessage_PreLoadGame: return "PreLoadGame";
	case NVSEMessagingInterface::kMessage_ExitGame_Console: return "ExitGame_Console";
	case NVSEMessagingInterface::kMessage_PostLoadGame: return "PostLoadGame";
	case NVSEMessagingInterface::kMessage_PostPostLoad: return "PostPostLoad";
	case NVSEMessagingInterface::kMessage_NewGame: return "NewGame";
	case NVSEMessagingInterface::kMessage_DeferredInit: return "DeferredInit";
	default: return nullptr;
	}
}

// Edge-triggered debug keys, only while the game window has focus.
static void PollDebugKeys()
{
	static bool s_toggleDown = false;
	static bool s_dumpDown = false;

	const OSGlobals* os = *g_osGlobals;
	const bool focused = os && GetForegroundWindow() == os->window;
	const auto pressed = [focused](int vk, bool& wasDown)
	{
		const bool down = focused && (GetAsyncKeyState(vk) & 0x8000);
		const bool edge = down && !wasDown;
		wasDown = down;
		return edge;
	};
	if (pressed(g_config.toggleKey, s_toggleDown))
		Tick_SetActive(!Tick_IsActive());
	if (pressed(g_config.dumpStateKey, s_dumpDown))
		Tick_DumpState();
}

void MessageHandler(NVSEMessagingInterface::Message* msg)
{
	static bool s_loggedMainLoop = false;

	if (msg->type == NVSEMessagingInterface::kMessage_MainGameLoop)
	{
		if (!s_loggedMainLoop)
		{
			s_loggedMainLoop = true;
			_MESSAGE("first MainGameLoop message");
		}
		Tick_OnMainGameLoop();
		PollDebugKeys();
		return;
	}

	// Lifecycle messages are logged so M7 can rely on their observed order.
	if (const char* name = MessageName(msg->type))
		_MESSAGE("message: %s", name);

	switch (msg->type)
	{
	case NVSEMessagingInterface::kMessage_PostLoad:
		Config_Load();
		Tick_InitActive(g_config.startActive);
		SM64_Init();
		break;
	case NVSEMessagingInterface::kMessage_PreLoadGame:
	case NVSEMessagingInterface::kMessage_ExitToMainMenu:
		// The old world is going away; the next running tick respawns Mario at the player.
		Mario_Despawn(MessageName(msg->type));
		GameState_SetLoaded(false);
		break;
	case NVSEMessagingInterface::kMessage_PostLoadGame:
		GameState_SetLoaded(msg->data != nullptr);
		if (msg->data)
		{
			Camera_ForgetView();
			Camera_RequestLoadView();
		}
		break;
	case NVSEMessagingInterface::kMessage_NewGame:
		GameState_SetLoaded(true);
		Camera_ForgetView();
		Camera_RequestLoadView();
		break;
	case NVSEMessagingInterface::kMessage_DeferredInit:
		// First point where the console exists; report the PostLoad result there.
		if (SM64_IsLoaded())
			Console_Print("SM64FNV: libsm64 ready");
		else
			Console_Print("SM64FNV: libsm64 NOT loaded: %s", SM64_LastError());
		break;
	case NVSEMessagingInterface::kMessage_ExitGame:
	case NVSEMessagingInterface::kMessage_ExitGame_Console:
		// The renderer may already be going away; the process exit frees the mesh.
		MarioRender_Abandon();
		CollisionObjects_Abandon();
		Mario_Despawn("exit");
		PlayerControl_RemoveSaveHook();
		PlayerControl_RemoveFlagPatches();
		PlayerHide_RemovePatches();
		ControlFilter_Remove();
		// Before sm64_global_terminate: the audio thread calls into libsm64.
		Audio_Shutdown();
		SM64_Shutdown();
		break;
	}
}

bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info)
{
	_MESSAGE("SM64FNV query (plugin version %u)", kPluginVersion);

	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "SM64FNV";
	info->version = kPluginVersion;

	if (nvse->isEditor)
		return false;

	if (nvse->nvseVersion < PACKED_NVSE_VERSION)
	{
		_ERROR("NVSE version too old (got %08X expected at least %08X)", nvse->nvseVersion, PACKED_NVSE_VERSION);
		return false;
	}
	if (nvse->runtimeVersion != RUNTIME_VERSION_1_4_0_525)
	{
		_ERROR("unsupported runtime version %08X (need %08X)", nvse->runtimeVersion, RUNTIME_VERSION_1_4_0_525);
		return false;
	}
	if (nvse->isNogore)
	{
		_ERROR("NoGore is not supported");
		return false;
	}
	return true;
}

bool NVSEPlugin_Load(NVSEInterface* nvse)
{
	_MESSAGE("SM64FNV load (NVSE %08X, runtime %08X)", nvse->nvseVersion, nvse->runtimeVersion);

	g_pluginHandle = nvse->GetPluginHandle();
	g_nvseInterface = nvse;

	g_messagingInterface = static_cast<NVSEMessagingInterface*>(nvse->QueryInterface(kInterface_Messaging));
	if (!g_messagingInterface || !g_messagingInterface->RegisterListener(g_pluginHandle, "NVSE", MessageHandler))
	{
		_ERROR("failed to register NVSE message listener");
		return false;
	}
	_MESSAGE("NVSE message listener registered");

	// Without it the controls stay enabled (PlayerControl_Acquire refuses), so a failure here isn't fatal.
	PlayerControl_InstallSaveHook();
	// Without them Wait is refused and the HUD hidden while Mario is active; not fatal.
	PlayerControl_InstallFlagPatches();
	// Without it the high-detail water reflection shows the hidden body; not fatal.
	PlayerHide_InstallPatches();
	// Without it the hidden player still reacts to weapon/VATS/hotkey controls; not fatal either.
	ControlFilter_Install();
	return true;
}
