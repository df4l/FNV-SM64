#include "audio.h"
#include "config.h"
#include "game_addrs.h"
#include "sm64_loader.h"
#include "nvse/GameAPI.h"

#include <atomic>
#include <cmath>
#include <mmsystem.h>
#include <dsound.h>

namespace
{
	constexpr DWORD kSampleRate = 32000;		// libsm64 output rate (SA audio_wasapi.cpp)
	constexpr DWORD kFrameBytes = 4;			// 16-bit stereo
	constexpr DWORD kRingFrames = 16384;		// 0.5 s
	constexpr DWORD kRingBytes = kRingFrames * kFrameBytes;
	// SA asks for 1100 frames per sm64_audio_tick (2 x 528/544 come back) and keeps about two of them queued.
	constexpr uint32_t kDesiredFrames = 1100;
	constexpr uint32_t kTargetQueuedFrames = 2200;
	constexpr uint32_t kStartFrames = 1500;	// SA starts WASAPI once 1500 frames are queued
	// Silence written after the data so an underrun plays zeros, not old audio.
	constexpr DWORD kSilenceAheadBytes = 4096 * kFrameBytes;
	constexpr uint32_t kMaxVolumeLogs = 20;

	SRWLOCK s_libLock = SRWLOCK_INIT;

	IDirectSoundBuffer* s_buffer = nullptr;
	HANDLE s_thread = nullptr;
	bool s_failed = false;		// no device / buffer creation failed: don't retry every frame
	std::atomic<bool> s_play{false};
	std::atomic<bool> s_quit{false};
	std::atomic<bool> s_faulted{false};
	std::atomic<bool> s_faultLogged{false};
	DWORD s_faultCode = 0;
	uintptr_t s_faultAddr = 0;

	// Audio thread state (counters also read by the dump; a torn read only skews the display).
	bool s_playing = false;
	DWORD s_writeOffset = 0;
	std::atomic<uint32_t> s_chunks{0};
	std::atomic<uint32_t> s_underruns{0};
	std::atomic<uint32_t> s_starts{0};
	std::atomic<uint32_t> s_queuedFrames{0};
	std::atomic<int> s_peak{0};	// loudest sample libsm64 produced since the last F10 (0 = silence)

	LONG s_volumeMB = 1;		// 1 = not set yet
	float s_effective = 0.0f;
	uint32_t s_volumeLogs = 0;
	bool s_lastPlay = false;

	const float* GameVolumes()
	{
		return reinterpret_cast<const float*>(GameAddr::kBSAudioManagerVolumes);
	}

	// BSWin32GameSound::SetVolume 0x00AED660 for a sound without category flags (= Effects), no static attenuation.
	LONG EffectsVolumeMB(float& effective)
	{
		const float* v = GameVolumes();
		const float masterMult = *reinterpret_cast<const float*>(GameAddr::kMasterVolumeMult);
		const float vol = g_config.audioVolume < 0.0f ? 0.0f : g_config.audioVolume > 1.0f ? 1.0f : g_config.audioVolume;
		effective = vol * v[GameAddr::kVolumeMaster] * v[GameAddr::kVolumeMasterMult] * masterMult *
			v[GameAddr::kVolumeEffects] * v[GameAddr::kVolumeEffectsMult];
		const LONG mB = (LONG)(2000.0 * log10(effective < 1e-5f ? 1e-5f : effective));
		return mB < DSBVOLUME_MIN ? DSBVOLUME_MIN : mB > -1 ? -1 : mB;
	}

	IDirectSound8* GameDevice()
	{
		const uintptr_t audio = *reinterpret_cast<const uintptr_t*>(GameAddr::kBSWin32AudioPtr);
		if (!audio || !*reinterpret_cast<const bool*>(audio + GameAddr::kBSWin32AudioInitDone))
			return nullptr;
		return *reinterpret_cast<IDirectSound8**>(audio + GameAddr::kBSWin32AudioDevice);
	}

	int RecordFault(EXCEPTION_POINTERS* info)
	{
		s_faultCode = info->ExceptionRecord->ExceptionCode;
		s_faultAddr = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// Returns frames per half (0 on fault); the call writes 2 halves.
	uint32_t AudioTickGuarded(uint32_t queued, int16_t* out)
	{
		__try
		{
			return g_sm64.audio_tick(queued, kDesiredFrames, out);
		}
		__except (RecordFault(GetExceptionInformation()))
		{
			return 0;
		}
	}

	// Copies bytes at s_writeOffset and zeroes kSilenceAheadBytes after them (two Lock regions when it wraps).
	bool WriteRing(const void* data, DWORD bytes)
	{
		void* p1;
		void* p2;
		DWORD n1, n2;
		HRESULT hr = s_buffer->Lock(s_writeOffset, bytes + kSilenceAheadBytes, &p1, &n1, &p2, &n2, 0);
		if (hr == DSERR_BUFFERLOST && SUCCEEDED(s_buffer->Restore()))
			hr = s_buffer->Lock(s_writeOffset, bytes + kSilenceAheadBytes, &p1, &n1, &p2, &n2, 0);
		if (FAILED(hr))
			return false;
		const uint8_t* src = static_cast<const uint8_t*>(data);
		const DWORD c1 = n1 < bytes ? n1 : bytes;
		memcpy(p1, src, c1);
		memset(static_cast<uint8_t*>(p1) + c1, 0, n1 - c1);
		if (p2)
		{
			const DWORD c2 = bytes - c1;
			memcpy(p2, src + c1, c2);
			memset(static_cast<uint8_t*>(p2) + c2, 0, n2 - c2);
		}
		s_buffer->Unlock(p1, n1, p2, n2);
		s_writeOffset = (s_writeOffset + bytes) % kRingBytes;
		return true;
	}

	void Pump()
	{
		DWORD play, write;
		if (FAILED(s_buffer->GetCurrentPosition(&play, &write)))
			return;
		// The queue is measured from the real cursors every time: Stop() moves the play cursor up to the write cursor
		// (~30 ms on Windows 10+), so a running count drifts and ends up behind the play cursor (M9.1: silent after the
		// first pause). Data must start at or after the write cursor; if not, the mixer already took the zeros after
		// our data (ran dry, or the cursors jumped): continue from the write cursor.
		DWORD queuedBytes = (s_writeOffset + kRingBytes - play) % kRingBytes;
		const DWORD committedBytes = (write + kRingBytes - play) % kRingBytes;
		if (queuedBytes < committedBytes)
		{
			if (s_playing)
				s_underruns++;
			s_writeOffset = write;
			queuedBytes = committedBytes;
		}

		int16_t chunk[544 * 2 * 2];
		uint32_t queued = queuedBytes / kFrameBytes;
		while (queued < kTargetQueuedFrames)
		{
			const uint32_t half = AudioTickGuarded(queued, chunk);
			if (!half)
			{
				if (s_faultCode)
					s_faulted = true;
				return;
			}
			int peak = s_peak;
			for (uint32_t i = 0; i < half * 4; i++)
				peak = abs(chunk[i]) > peak ? abs(chunk[i]) : peak;
			s_peak = peak;
			if (!WriteRing(chunk, half * 2 * kFrameBytes))
				return;
			queued += half * 2;
			s_chunks++;
		}
		s_queuedFrames = queued;
		if (!s_playing && queued >= kStartFrames && SUCCEEDED(s_buffer->Play(0, 0, DSBPLAY_LOOPING)))
		{
			s_playing = true;
			s_starts++;
		}
	}

	DWORD WINAPI AudioThread(void*)
	{
		while (!s_quit)
		{
			if (s_play && !s_faulted)
			{
				AcquireSRWLockExclusive(&s_libLock);
				Pump();
				ReleaseSRWLockExclusive(&s_libLock);
			}
			else if (s_playing)
			{
				// Paused/despawned: what is queued after the write cursor stays in the ring and plays on resume.
				s_buffer->Stop();
				s_playing = false;
			}
			Sleep(5);
		}
		if (s_playing)
			s_buffer->Stop();
		return 0;
	}

	bool CreateStream()
	{
		IDirectSound8* device = GameDevice();
		if (!device)
		{
			_MESSAGE("audio: the game's DirectSound device is missing (audio disabled in the game?), Mario stays silent");
			Console_Print("SM64FNV: audio off, the game has no sound device");
			return false;
		}
		WAVEFORMATEX format{};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = 2;
		format.nSamplesPerSec = kSampleRate;
		format.wBitsPerSample = 16;
		format.nBlockAlign = kFrameBytes;
		format.nAvgBytesPerSec = kSampleRate * kFrameBytes;
		// 2D, no GLOBALFOCUS: like the game's own buffers it goes quiet while the game window is in the background.
		DSBUFFERDESC desc{};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2;
		desc.dwBufferBytes = kRingBytes;
		desc.lpwfxFormat = &format;
		const HRESULT hr = device->CreateSoundBuffer(&desc, &s_buffer, nullptr);
		if (FAILED(hr))
		{
			_ERROR("audio: CreateSoundBuffer on the game's device %p failed (%08X), Mario stays silent", device, hr);
			s_buffer = nullptr;
			return false;
		}
		s_thread = CreateThread(nullptr, 0, AudioThread, nullptr, 0, nullptr);
		if (!s_thread)
		{
			_ERROR("audio: CreateThread failed (%lu)", GetLastError());
			s_buffer->Release();
			s_buffer = nullptr;
			return false;
		}
		SetThreadPriority(s_thread, THREAD_PRIORITY_ABOVE_NORMAL);
		_MESSAGE("audio: stream created on the game's DirectSound device %p: 32 kHz 16-bit stereo, %u-frame ring, thread started",
			device, kRingFrames);
		return true;
	}

	void UpdateVolume()
	{
		float effective;
		const LONG mB = EffectsVolumeMB(effective);
		if (mB == s_volumeMB)
			return;
		s_buffer->SetVolume(mB);
		s_volumeMB = mB;
		s_effective = effective;
		if (s_volumeLogs < kMaxVolumeLogs)
		{
			s_volumeLogs++;
			const float* v = GameVolumes();
			_MESSAGE("audio: volume %ld mB (%.3f = trim %.2f x master %.2f x mult %.2f x fMasterVolumeMult %.2f x effects %.2f x mult %.2f)",
				mB, effective, g_config.audioVolume, v[GameAddr::kVolumeMaster], v[GameAddr::kVolumeMasterMult],
				*reinterpret_cast<const float*>(GameAddr::kMasterVolumeMult), v[GameAddr::kVolumeEffects],
				v[GameAddr::kVolumeEffectsMult]);
		}
	}
}

void Audio_Update(bool play)
{
	if (!SM64_AudioReady() || s_failed)
		return;
	if (s_faulted && !s_faultLogged)
	{
		s_faultLogged = true;
		_ERROR("audio: sm64_audio_tick raised exception %08X at %08X, Mario's sound is off until restart", s_faultCode, s_faultAddr);
		Console_Print("SM64FNV: audio fault, sound off (see nvse_sm64.log)");
	}
	if (play != s_lastPlay)
	{
		s_lastPlay = play;
		_MESSAGE("audio: %s", play ? "playing" : "stopped");
	}
	if (!s_buffer)
	{
		if (!play)
			return;
		if (!CreateStream())
		{
			s_failed = true;
			return;
		}
	}
	UpdateVolume();
	s_play = play;
}

void Audio_LockLib()
{
	AcquireSRWLockExclusive(&s_libLock);
}

void Audio_UnlockLib()
{
	ReleaseSRWLockExclusive(&s_libLock);
}

void Audio_Shutdown()
{
	if (!s_buffer)
		return;
	s_play = false;
	s_quit = true;
	if (WaitForSingleObject(s_thread, 1000) != WAIT_OBJECT_0)
	{
		// Still inside libsm64 or DirectSound: leave both alone, the process is exiting.
		_ERROR("audio: thread did not stop within 1 s, buffer left to the process exit");
		return;
	}
	CloseHandle(s_thread);
	s_thread = nullptr;
	s_buffer->Release();
	s_buffer = nullptr;
	_MESSAGE("audio: stopped and released (%u chunks, %u underruns, %u starts)", s_chunks.load(), s_underruns.load(), s_starts.load());
}

void Audio_DumpState()
{
	if (!g_config.audio)
	{
		Console_Print("SM64FNV: audio off (ini)");
		return;
	}
	if (!SM64_AudioReady() || s_failed || !s_buffer)
	{
		Console_Print("SM64FNV: audio %s", !SM64_AudioReady() ? "not initialised (see log)" : s_failed ? "failed (see log)" : "not started yet");
		return;
	}
	const float* v = GameVolumes();
	const int peak = s_peak.exchange(0);
	DWORD status = 0, play = 0, write = 0;
	s_buffer->GetStatus(&status);
	s_buffer->GetCurrentPosition(&play, &write);
	const DWORD ahead = ((s_writeOffset + kRingBytes - play) % kRingBytes) / kFrameBytes;
	Console_Print("SM64FNV: audio %s%s, volume %ld mB (master %.0f%%, effects %.0f%%), queued %u frames, peak %d since last F10, "
		"%u chunks, %u underruns", s_play ? "playing" : "stopped", s_faulted ? " (FAULTED)" : "", s_volumeMB,
		v[GameAddr::kVolumeMaster] * 100.0f, v[GameAddr::kVolumeEffects] * 100.0f, ahead, peak, s_chunks.load(), s_underruns.load());
	_MESSAGE("state dump audio: play=%d faulted=%d status 0x%X volume %ld mB (effective %.3f), queued %u frames (cursors play %u write %u, "
		"ours %u), peak %d, %u chunks, %u underruns, %u starts", s_play.load(), s_faulted.load(), status, s_volumeMB, s_effective,
		ahead, play / kFrameBytes, write / kFrameBytes, s_writeOffset / kFrameBytes, peak, s_chunks.load(), s_underruns.load(),
		s_starts.load());
}
