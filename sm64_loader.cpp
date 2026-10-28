#include "sm64_loader.h"
#include "config.h"

#include <bcrypt.h>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

SM64Api g_sm64{};

namespace
{
	// SA checks the first 4 bytes of the ROM's SHA-1 (sm64-san-andreas main.cpp); full: 9bef1128717f958171a4afac3ed78ee2bb4e86ce
	constexpr char kRomSha1Prefix[] = "9bef1128";

	HMODULE s_lib = nullptr;
	bool s_loaded = false;
	bool s_audioReady = false;
	std::string s_lastError;
	std::vector<uint8_t> s_texture;
	DWORD s_faultCode = 0;
	uintptr_t s_faultAddr = 0;

	bool Fail(const char* fmt, ...)
	{
		char buf[512];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		s_lastError = buf;
		_ERROR("sm64 init failed: %s", buf);
		return false;
	}

	// <Plugins>\sm64\ (the folder next to nvse_sm64.dll), with trailing backslash.
	std::wstring DataDir()
	{
		HMODULE self = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&DataDir), &self);
		wchar_t path[MAX_PATH];
		DWORD len = GetModuleFileNameW(self, path, MAX_PATH);
		std::wstring dir(path, len);
		dir.resize(dir.find_last_of(L'\\') + 1);
		return dir + L"sm64\\";
	}

	std::string Narrow(const std::wstring& s)
	{
		std::string out(WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr), '\0');
		WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), (int)out.size(), nullptr, nullptr);
		out.pop_back();
		return out;
	}

	bool Sha1Hex(const std::vector<uint8_t>& data, char (&hex)[41])
	{
		uint8_t digest[20];
		if (!BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), digest, sizeof(digest))))
			return false;
		for (int i = 0; i < 20; i++)
			sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
		return true;
	}

	void DebugPrint(const char* msg)
	{
		_MESSAGE("[libsm64] %s", msg);
	}

	// Kept free of C++ objects so __try is allowed: a fault inside libsm64 is logged instead of taking the game down.
	bool GlobalInitGuarded(const uint8_t* rom, uint8_t* texture)
	{
		__try
		{
			g_sm64.global_init(rom, texture);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool AudioInitGuarded(const uint8_t* rom)
	{
		__try
		{
			g_sm64.audio_init(rom);
			// SA's level (sm64-san-andreas main.cpp): scales SM64's own sound-effect volume inside the mix.
			g_sm64.set_sound_volume(0.5f);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	template <typename T>
	bool Resolve(T& fn, const char* name)
	{
		fn = reinterpret_cast<T>(GetProcAddress(s_lib, name));
		if (!fn)
			return Fail("libsm64.dll has no export %s", name);
		return true;
	}
}

std::wstring SM64_DataDir()
{
	return DataDir();
}

bool SM64_Init()
{
	if (s_loaded)
		return true;

	const std::wstring dir = SM64_DataDir();
	_MESSAGE("sm64 data dir: %s", Narrow(dir).c_str());

	// Stage 1: libsm64.dll
	const std::wstring libPath = dir + L"libsm64.dll";
	s_lib = LoadLibraryW(libPath.c_str());
	if (!s_lib)
		return Fail("LoadLibrary(%s) error %lu", Narrow(libPath).c_str(), GetLastError());
	_MESSAGE("libsm64.dll loaded at %p", s_lib);

	if (!Resolve(g_sm64.register_debug_print_function, "sm64_register_debug_print_function") ||
		!Resolve(g_sm64.global_init, "sm64_global_init") ||
		!Resolve(g_sm64.global_terminate, "sm64_global_terminate") ||
		!Resolve(g_sm64.static_surfaces_load, "sm64_static_surfaces_load") ||
		!Resolve(g_sm64.mario_create, "sm64_mario_create") ||
		!Resolve(g_sm64.mario_tick, "sm64_mario_tick") ||
		!Resolve(g_sm64.mario_delete, "sm64_mario_delete") ||
		!Resolve(g_sm64.set_mario_faceangle, "sm64_set_mario_faceangle") ||
		!Resolve(g_sm64.set_mario_action, "sm64_set_mario_action") ||
		!Resolve(g_sm64.set_mario_health, "sm64_set_mario_health") ||
		!Resolve(g_sm64.mario_kill, "sm64_mario_kill") ||
		!Resolve(g_sm64.set_mario_water_level, "sm64_set_mario_water_level") ||
		!Resolve(g_sm64.surface_find_floor, "sm64_surface_find_floor") ||
		!Resolve(g_sm64.surface_object_create, "sm64_surface_object_create") ||
		!Resolve(g_sm64.surface_object_move, "sm64_surface_object_move") ||
		!Resolve(g_sm64.surface_object_delete, "sm64_surface_object_delete") ||
		!Resolve(g_sm64.object_create, "sm64_object_create") ||
		!Resolve(g_sm64.object_move, "sm64_object_move") ||
		!Resolve(g_sm64.object_delete, "sm64_object_delete") ||
		!Resolve(g_sm64.mario_attack, "sm64_mario_attack") ||
		!Resolve(g_sm64.set_mario_forward_velocity, "sm64_set_mario_forward_velocity") ||
		!Resolve(g_sm64.audio_init, "sm64_audio_init") ||
		!Resolve(g_sm64.audio_tick, "sm64_audio_tick") ||
		!Resolve(g_sm64.set_sound_volume, "sm64_set_sound_volume"))
		return false;
	_MESSAGE("libsm64 exports resolved");

	// Stage 2: ROM (user-supplied, never copied anywhere)
	const std::wstring romPath = dir + L"sm64.us.z64";
	std::ifstream file(romPath, std::ios::binary | std::ios::ate);
	if (!file)
		return Fail("ROM not found: %s", Narrow(romPath).c_str());
	std::vector<uint8_t> rom((size_t)file.tellg());
	file.seekg(0);
	file.read(reinterpret_cast<char*>(rom.data()), rom.size());
	if (!file)
		return Fail("ROM read error: %s", Narrow(romPath).c_str());
	_MESSAGE("ROM read: %u bytes", (UInt32)rom.size());

	char sha1[41];
	if (!Sha1Hex(rom, sha1))
		return Fail("SHA-1 computation failed");
	if (strncmp(sha1, kRomSha1Prefix, 8) != 0)
		return Fail("ROM SHA-1 %s does not match the SM64 US ROM (expected %s...)", sha1, kRomSha1Prefix);
	_MESSAGE("ROM SHA-1 OK: %s", sha1);

	// Stage 3: sm64_global_init
	g_sm64.register_debug_print_function(DebugPrint);
	s_texture.assign(4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT, 0);
	if (!GlobalInitGuarded(rom.data(), s_texture.data()))
	{
		SM64_LogFault("sm64_global_init");
		return Fail("sm64_global_init raised an exception");
	}

	UInt32 opaque = 0;
	for (size_t i = 3; i < s_texture.size(); i += 4)
		opaque += s_texture[i] != 0;
	_MESSAGE("sm64_global_init OK: texture %dx%d RGBA, %u/%u pixels with alpha", SM64_TEXTURE_WIDTH, SM64_TEXTURE_HEIGHT,
		opaque, (UInt32)(s_texture.size() / 4));

	s_loaded = true;

	// Stage 4: sound banks (libsm64 keeps its own copy of the ROM). A failure only leaves Mario silent.
	if (!g_config.audio)
		_MESSAGE("sm64_audio_init skipped ([Audio] bAudio=0)");
	else if (!AudioInitGuarded(rom.data()))
		SM64_LogFault("sm64_audio_init");
	else
	{
		s_audioReady = true;
		_MESSAGE("sm64_audio_init OK");
	}
	return true;
}

void SM64_Shutdown()
{
	if (s_loaded)
	{
		s_audioReady = false;
		g_sm64.global_terminate();
		_MESSAGE("sm64_global_terminate done");
		s_loaded = false;
	}
	// libsm64.dll stays mapped until process exit; unloading it buys nothing at shutdown.
}

bool SM64_IsLoaded()
{
	return s_loaded;
}

bool SM64_AudioReady()
{
	return s_audioReady;
}

const char* SM64_LastError()
{
	return s_lastError.c_str();
}

const uint8_t* SM64_Texture()
{
	return s_loaded ? s_texture.data() : nullptr;
}

int SM64_RecordFault(EXCEPTION_POINTERS* info)
{
	s_faultCode = info->ExceptionRecord->ExceptionCode;
	s_faultAddr = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
	return EXCEPTION_EXECUTE_HANDLER;
}

void SM64_LogFault(const char* where)
{
	const uintptr_t base = reinterpret_cast<uintptr_t>(s_lib);
	if (s_lib && s_faultAddr >= base && s_faultAddr - base < 0x1000000)
		_ERROR("%s: exception %08X at %08X (libsm64.dll+%X)", where, s_faultCode, s_faultAddr, s_faultAddr - base);
	else
		_ERROR("%s: exception %08X at %08X (outside libsm64.dll code; bad pointer or call target)", where, s_faultCode, s_faultAddr);
}
