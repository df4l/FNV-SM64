#pragma once

// Hides the player's 3rd-person body while Mario is spawned ([Subsystems] bHidePlayer), by setting AppCulled on
// the 3rd-person root. Culled nodes still get their transforms updated (NiNode::UpdateDownwardPass), so the
// camera and skeleton keep working. Show1stPerson rewrites the bit on every 1st/3rd switch, so it is re-applied
// every frame.
void PlayerHide_Apply();
// Puts the bit back the way Show1stPerson leaves it (3rd-person root culled iff the 1st-person root is not).
void PlayerHide_Release(const char* reason);
void PlayerHide_DumpState();
// The high-detail water reflection pass un-culls the 3rd-person root mid-frame (after our per-frame apply);
// this redirects that one call so it leaves our hidden root alone. Not fatal if it fails.
void PlayerHide_InstallPatches();
void PlayerHide_RemovePatches();
