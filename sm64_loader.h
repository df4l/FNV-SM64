#pragma once

#include <cstdint>
#include <string>
#include <windows.h>
#include "libsm64.h"

// libsm64.dll (SA fork) entry points, resolved with GetProcAddress.
struct SM64Api
{
	decltype(&sm64_register_debug_print_function) register_debug_print_function;
	decltype(&sm64_global_init) global_init;
	decltype(&sm64_global_terminate) global_terminate;
	decltype(&sm64_static_surfaces_load) static_surfaces_load;
	decltype(&sm64_mario_create) mario_create;
	decltype(&sm64_mario_tick) mario_tick;
	decltype(&sm64_mario_delete) mario_delete;
	decltype(&sm64_set_mario_faceangle) set_mario_faceangle;
	decltype(&sm64_set_mario_action) set_mario_action;
	decltype(&sm64_set_mario_health) set_mario_health;
	decltype(&sm64_mario_kill) mario_kill;
	decltype(&sm64_set_mario_water_level) set_mario_water_level;
	decltype(&sm64_surface_find_floor) surface_find_floor;
	decltype(&sm64_surface_object_create) surface_object_create;
	decltype(&sm64_surface_object_move) surface_object_move;
	decltype(&sm64_surface_object_delete) surface_object_delete;
	decltype(&sm64_object_create) object_create;
	decltype(&sm64_object_move) object_move;
	decltype(&sm64_object_delete) object_delete;
	decltype(&sm64_mario_attack) mario_attack;
	decltype(&sm64_set_mario_forward_velocity) set_mario_forward_velocity;
	decltype(&sm64_audio_init) audio_init;
	decltype(&sm64_audio_tick) audio_tick;
	decltype(&sm64_set_sound_volume) set_sound_volume;
};

extern SM64Api g_sm64;

// <Plugins>\sm64\ (next to nvse_sm64.dll), with trailing backslash. Holds libsm64.dll, the ROM and sm64.ini.
std::wstring SM64_DataDir();

// Loads libsm64.dll and the ROM from <Plugins>\sm64\, then runs sm64_global_init (and sm64_audio_init, [Audio] bAudio).
// Each stage is logged; returns false (and leaves libsm64 unusable) on any failure.
bool SM64_Init();
void SM64_Shutdown();
bool SM64_IsLoaded();
// sm64_audio_init succeeded ([Audio] bAudio=1 and SM64_Init got that far).
bool SM64_AudioReady();
const char* SM64_LastError();

// SEH filter for guarded libsm64 calls: remembers the exception code/address and handles it.
int SM64_RecordFault(EXCEPTION_POINTERS* info);
// Logs the last recorded fault, with its offset inside libsm64.dll when it lies there.
void SM64_LogFault(const char* where);

// Mario's texture atlas from sm64_global_init: SM64_TEXTURE_WIDTH x SM64_TEXTURE_HEIGHT, RGBA8.
const uint8_t* SM64_Texture();
