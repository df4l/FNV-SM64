#pragma once

// Mario's sound (M9): libsm64's 32 kHz stereo mix streamed into a DirectSound buffer on the game's own device, with
// the volume the game gives an Effects sound (Master and Effects sliders). FNV-SM64-RE findings/audio.md.

// Every frame on the game thread. play = the simulation runs and Mario is spawned; otherwise the stream is stopped.
// Creates the buffer and the audio thread on first use.
void Audio_Update(bool play);
// Held around sm64_mario_create/delete so the audio thread never mixes while Mario's objects change.
void Audio_LockLib();
void Audio_UnlockLib();
// ExitGame: stops the thread and frees the buffer (before sm64_global_terminate).
void Audio_Shutdown();
void Audio_DumpState();
