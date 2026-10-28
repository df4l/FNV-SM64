#pragma once

// M4 follow-up: the vanilla 3rd-person camera (D9 option A) is kept, but its zoom-out limit is raised while Mario is
// spawned, since the human-sized default puts the camera right on top of him. [Camera] fMaxZoomDist.
void Camera_Acquire();
void Camera_Release(const char* reason);
void Camera_DumpState();
// [Camera] bThirdPersonOnLoad: a save load or new game arms it; the next spawn (after Camera_Acquire) switches to
// 3rd person and sets the zoom to fChaseCameraMax. Later respawns (F8, doors) keep the player's view.
void Camera_RequestLoadView();
void Camera_ApplyLoadView();
// M6.1 [Camera] bForceThirdPerson: Camera_NoteSpawnView (spawn, before the load view) remembers a 1st-person view;
// Camera_KeepThirdPerson (every running frame while spawned) switches 1st person back to 3rd, zoom >= fMinZoomDist;
// Camera_RestoreView (master toggle off, libsm64 fault) returns to 1st person if that's where the player was.
// A load or new game forgets it (Camera_ForgetView): the load view decides then.
void Camera_NoteSpawnView();
void Camera_KeepThirdPerson();
void Camera_RestoreView(const char* reason);
void Camera_ForgetView();
void Camera_DumpView();
