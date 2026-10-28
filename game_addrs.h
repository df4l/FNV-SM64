#pragma once

#include <cstdint>

// Every raw FalloutNV.exe 1.4.0.525 address the plugin uses, with its source.
// GOG and Steam exes match byte for byte at the checked sites (FNV-SM64-RE notes/progress.md, M0.1).
namespace GameAddr
{
	// --- Game state (FNV-SM64-RE findings/main-loop.md §3) ---

	// bool Main::bInMenuMode (Ghidra user name g_bIsMenuMode). findings/addresses.md, high
	constexpr uintptr_t kInMenuMode = 0x011DEA2B;
	// bool fader-to-black visible (user g_bIsInPauseFade). NVSE Hooks_Gameplay.cpp kIsInPauseFadeGlobalAddr; main-loop.md §3, high
	constexpr uintptr_t kFaderToBlackVisible = 0x011DEA2D;
	// bool console open. NVSE GameAPI.cpp / JIP IsConsoleOpen, high
	constexpr uintptr_t kConsoleOpen = 0x011DEA2E;
	// Main* is NVSE g_osGlobals (0x011DEA0C); +0x07 bool bFreezeTime. main-loop.md §3, high
	constexpr uintptr_t kMainFreezeTimeOffset = 0x07;
	// LoadingMenu*, non-null while a loading screen is up (isInLoadingScreen 0x00705EA0). findings/addresses.md, high
	constexpr uintptr_t kLoadingMenuPtr = 0x011DA0C0;

	// --- Player (findings/player.md §1A) ---

	// void __thiscall MobileObject::SetPosition(const NiPoint3*), ret 4: ref pos + char controller (velocity zeroed,
	// fall height reset) + 3D root. findings/addresses.md, high; prologue re-checked in Ghidra 2026-10-02
	constexpr uintptr_t kMobileObjectSetPosition = 0x00931620;
	// void __thiscall PlayerCharacter::SetControlsDisabled(bool disable, UInt8 mask), ret 8: updates +0x680 and the HUD.
	// findings/player.md §3, high; body re-checked in Ghidra 2026-10-02
	constexpr uintptr_t kPlayerSetControlsDisabled = 0x0095F530;
	// Readers of the flags (+0x680) that we redirect at single call sites so our bits don't count (M4.3b). Both
	// sites re-checked in Ghidra 2026-10-03, high.
	// UInt8 __thiscall GetControlFlags() returns +0x680. The Wait/Rest key handler in PlayerCharacter::Update calls it
	// and refuses Wait silently (no message) when any flag is set.
	constexpr uintptr_t kPlayerGetControlFlags = 0x005A03D0;
	constexpr uintptr_t kWaitFlagsCallSite = 0x0094239C;
	// bool __thiscall IsControlDisabled(UInt8 mask), ret 4. After a menu closes, 0x0070C4A0 calls it with mask 1
	// (Movement) and hides the HUD (state 0xC) when it's true.
	constexpr uintptr_t kPlayerIsControlDisabled = 0x005A03F0;
	constexpr uintptr_t kHudMovementCheckCallSite = 0x0070C982;
	// UpdateVisibilityState itself re-tests Movement twice (`push 1; mov ecx,[g_thePlayer]; call`) and turns a
	// requested normal state into 0xC. Ghidra + GOG bytes 2026-10-03, high
	constexpr uintptr_t kHudVisMovementCheckSite1 = 0x00771836;
	constexpr uintptr_t kHudVisMovementCheckSite2 = 0x00771902;
	// PlayerCharacter::Update Activate handler: `push 1; mov ecx,[ebp-18h]; call` sets the refuse flag (activate-
	// nothing sound, 0x00943378) when Movement is disabled. Ghidra + GOG bytes 2026-10-03, high
	constexpr uintptr_t kActivateMovementCheckSite = 0x00942F0C;
	// Actor virtual 0x008A03A0 (in every actor vtable, e.g. PlayerCharacter +0x430): an NPC whose combat target is the
	// player skips its combat update while Movement is disabled (`push 1; mov ecx,[g_thePlayer]; call` at this site),
	// so enemies never hit a Mario player (M10.1b). The Fight bit (8) does the same in 0x008ECF20 / 0x00918120.
	// Ghidra 2026-10-04, high
	constexpr uintptr_t kCombatMovementCheckSite = 0x008A04B2;
	// void __cdecl HUDMainMenu::UpdateVisibilityState(UInt32 state). SetControlsDisabled (0x0095F590) passes 0xC
	// (hidden) when Movement is disabled, else 1 (normal). Ghidra 2026-10-03, high
	constexpr uintptr_t kHudUpdateVisibilityState = 0x00771700;
	constexpr UInt32 kHudVisibilityNormal = 1;

	// --- Save (FNV-SM64-RE findings/player.md §3) ---

	// void __thiscall BGSSaveLoadGame::SaveGame(arg), ret 4: writes the whole .fos (contains NVSE's post-save hook
	// 0x00847C45). Ghidra 2026-10-02, high
	constexpr uintptr_t kBGSSaveLoadGameSaveGame = 0x00847850;
	// Its only caller's `call` (E8, ECX = [0x011DDF38]); GOG bytes match Steam (checked 2026-10-02), high
	constexpr uintptr_t kSaveGameCallSite = 0x008505BB;

	// --- Input / camera (FNV-SM64-RE findings/input.md, camera.md) ---

	// OSInputGlobals* is NVSE g_OSInputGlobals (0x011F35CC).
	// int __thiscall OSInputGlobals::GetControlState(ControlCode, KeyState), ret 8: applies the game's key/mouse or
	// controller bindings (gamepad mode reads only the controller ones). input.md §3, high; body + epilogue re-read in
	// Ghidra 2026-10-03 (no side effects except for code 0xE MenuMode, which we never pass)
	constexpr uintptr_t kGetControlState = 0x00A24660;
	// Its prologue push ebp / mov ebp,esp / sub esp,0x14 (55 8B EC 83 EC 14), relocated by the M4.3 entry hook.
	// Ghidra + GOG file bytes compared 2026-10-03, high
	constexpr uintptr_t kGetControlStatePrologueLen = 6;
	// bool g_bGamepadActive: XInput pad 0 connected and the controller not disabled. input.md §2, high
	constexpr uintptr_t kGamepadActive = 0x011F35C8;
	// XINPUT_STATE g_xinputState (pad 0), refreshed by OSInputGlobals::Poll: +8 sThumbLX, +0xA sThumbLY (SHORT,
	// standard XInput layout). input.md §2, high
	constexpr uintptr_t kXInputState = 0x011F35A8;
	constexpr uintptr_t kXInputThumbLX = 0x8, kXInputThumbLY = 0xA;
	// NiCamera* __cdecl Main::WorldRootCamera(): spWorldRoot (0x011DEB7C) -> BSSceneGraph::GetCamera. camera.md §1,
	// high; disassembled 2026-10-03 (no args, plain RET)
	constexpr uintptr_t kWorldRootCamera = 0x00524C90;
	// float 3rd-person zoom distance = g_vanityModePos.y (vanityModePos 0x011E0B58 +4): mouse-wheel zoom, clamped to
	// GMST fChaseCameraMax (0x00945BB4). camera.md §2, high; re-read in Ghidra 2026-10-03
	constexpr uintptr_t kCameraZoomDist = 0x011E0B5C;
	// bool g_bVanityMode: free-orbit camera; set while the POV control is held (gamepad zoom, 0x00942CD4) or by
	// auto-vanity. camera.md, high
	constexpr uintptr_t kVanityMode = 0x011E07B8;
	// bool __thiscall PlayerCharacter::SetFirstPerson(bool bFirst), ret 4: sets bWant3rdPerson (+0x64C); going to 3rd
	// person it may reset the zoom distance, so write that afterwards. camera.md §3, high (JIP ToggleFirstPerson;
	// Ghidra 2026-10-03)
	constexpr uintptr_t kPlayerSetFirstPerson = 0x00950110;

	// --- Render (FNV-SM64-RE findings/render.md) ---

	// void* __cdecl NiNew(size): Ni object allocator. render.md §1, high (JIP Ni_Alloc; Ghidra 2026-10-02)
	constexpr uintptr_t kNiNew = 0x00AA13E0;
	// NiNode::NiNode(UInt16 childCapacity), thiscall, object size 0xAC. Main init (0x0086D590) builds
	// LandLOD/ObjectLODRoot with NiNew(0xAC) + this ctor(0). Ghidra 2026-10-02, high
	constexpr uintptr_t kNiNodeCtor = 0x00A5ECB0;
	constexpr uint32_t kNiNodeSize = 0xAC;
	// NiTriShapeData::NiTriShapeData(numVerts, v, n, c, uv, texSets, nbtMethod, numTris, tris), thiscall, size 0x58.
	// Takes ownership of the arrays: the NiGeometryData dtor (0x00A67C70) frees them with MemoryManager::Deallocate,
	// i.e. FormHeap_Free. render.md §1, high; dtor read in Ghidra 2026-10-02
	constexpr uintptr_t kNiTriShapeDataCtor = 0x00A7B630;
	constexpr uint32_t kNiTriShapeDataSize = 0x58;
	// NiTriShape::NiTriShape(NiTriShapeData*), thiscall, size 0xC4. render.md §1, high
	constexpr uintptr_t kNiTriShapeCtor = 0x00A74480;
	constexpr uint32_t kNiTriShapeSize = 0xC4;
	// BSShaderNoLightingProperty::BSShaderNoLightingProperty(), thiscall, size 0x80. render.md §1, high (JIP)
	constexpr uintptr_t kBSShaderNoLightingPropertyCtor = 0x00B6FC90;
	constexpr uint32_t kBSShaderNoLightingPropertySize = 0x80;
	// DList node alloc (12 B: next, prev, data), used for NiAVObject's property list. JIP NiGeometry::AddProperty;
	// Ghidra user name NewNode, high
	constexpr uintptr_t kDListNewNode = 0x0043A010;
	// void __fastcall NiAVObject::UpdateProperties(this). render.md §1, medium-high (JIP AssignGeometryProps)
	constexpr uintptr_t kNiAVObjectUpdateProperties = 0x00A5A040;
	// cdecl (obj, 0, 0, 0): per-geometry shader setup. render.md §1, medium (JIP AssignGeometryProps, shipped code)
	constexpr uintptr_t kBSShaderManagerInitGeometry = 0x00B57BD0;
	// void __thiscall NiNode::UpdateDownwardPass(const NiUpdateData&, UInt32 flags). render.md §1, high
	constexpr uintptr_t kNiNodeUpdateDownwardPass = 0x00A5DD70;
	// NiPixelData::NiPixelData(w, h, const NiPixelFormat*, mipmaps, faces), thiscall, size 0x74; allocates the pixel
	// buffer (+0x50) itself. render.md §1, high; body re-read in Ghidra 2026-10-02
	constexpr uintptr_t kNiPixelDataCtor = 0x00A7C190;
	constexpr uint32_t kNiPixelDataSize = 0x74;
	// static NiPixelFormat NiPixelFormat::RGBA32 (0x44 B). render.md §1, high
	constexpr uintptr_t kNiPixelFormatRGBA32 = 0x011AA2A0;
	// NiSourceTexture* __cdecl NiSourceTexture::Create(NiPixelData*, const char* name, const FormatPrefs*): refs the
	// pixel data; returns null (and deletes itself, dropping the pixel data) if renderer-data preload fails.
	// render.md §1, high; body re-read in Ghidra 2026-10-02
	constexpr uintptr_t kNiSourceTextureCreate = 0x00A5FF10;
	// NiAlphaProperty* __cdecl NiAlphaProperty::Create(): NiNew(0x1C), flags 0xEC, threshold 0. JIP; disassembled in
	// Ghidra 2026-10-02 (medium -> high)
	constexpr uintptr_t kNiAlphaPropertyCreate = 0x00A5CEB0;
	// World ShadowSceneNode* (GetShadowSceneNode(0) 0x00450B80 reads [0x011F91C8 + i*4]). Child of the world
	// SceneGraph and parent of Sky/Weather/LODRoot/ObjectLODRoot (Main init 0x0086D590). Ghidra 2026-10-02, high
	constexpr uintptr_t kWorldShadowSceneNodePtr = 0x011F91C8;

	// Vtable offsets (JIP netimmerse.h; NiNode vtbl 0x109B5AC)
	constexpr uint32_t kVtblDeleteThis = 0x04;			// NiRefObject, called when the refcount drops to 0
	constexpr uint32_t kVtblNiNodeAddObject = 0xDC;		// (NiAVObject* child, bool firstFree)
	constexpr uint32_t kVtblNiNodeRemoveObject = 0xE8;	// (NiAVObject* child), drops the node's reference

	// Field offsets (JIP netimmerse.h; render.md §2)
	constexpr uint32_t kRefCount = 0x04;			// NiRefObject
	constexpr uint32_t kAVParent = 0x18;			// NiAVObject* m_parent
	// NiAVObject u32 m_flags, bit 0 AppCulled (player.md §4, JIP + Xbox SetAppCulled, high). A culled node's
	// children still get their transforms (UpdateDownwardPass 0x00A5DD70 only passes the flag down, Ghidra 2026-10-02)
	constexpr uint32_t kAVFlags = 0x30;
	constexpr UInt32 kAVFlagAppCulled = 0x1;
	// void __thiscall NiAVObject::SetAppCulled(bool), ret 4 (Ghidra name, high)
	constexpr uintptr_t kNiAVObjectSetAppCulled = 0x00450F90;
	// TESWater reflection render 0x004EB540 (only with bForceHighDetailReflections:Water, on in Ultra): after the
	// reflection pass, if flag +0x9C (set by the setup 0x004EAA00 when it ran in 1st person, cleared only by
	// 0x004E1650) is set and the player is in 3rd person (+0x64A), `call SetAppCulled(0)` on Get3D() = the
	// 3rd-person root, which shows the hidden body for the rest of the frame. Ghidra + GOG bytes 2026-10-03, high
	constexpr uintptr_t kReflectionUncullCallSite = 0x004EB75A;
	constexpr uint32_t kAVPropertyList = 0x24;		// DList<NiProperty>: head 0x24, tail 0x28, count 0x2C
	constexpr uint32_t kAVLocalTranslate = 0x58;	// local NiTransform @ 0x34: rotate 0x34, translate 0x58, scale 0x64
	// World NiMatrix3 rotate (row-major); a camera looks along column 0. camera.md §1, high (JIP, NiCamera::GetWorldDirection)
	constexpr uint32_t kAVWorldRotate = 0x68;
	constexpr uint32_t kGeomData = 0xB8;			// NiGeometry::geometryData
	// NiGeometryData u16: low 12 bits = dirty mask, 0x7000 = consistency (0 mutable, 0x4000 static). Xbox
	// GetConsistency/MarkAsChanged; the renderer repacks a non-static buffer whose dirty mask is non-zero, and
	// 0xFFF is what its forced repack uses (Xbox PackGeometryBuffer, read 2026-10-02)
	constexpr uint32_t kGDDirtyFlags = 0x0E;
	constexpr uint16_t kGDDirtyAll = 0x0FFF;
	constexpr uint16_t kGDConsistencyMask = 0x7000;
	constexpr uint32_t kGDBound = 0x10;				// NiBound: centre xyz + radius, model space
	constexpr uint32_t kGDVertices = 0x20;			// NiPoint3*
	constexpr uint32_t kGDNormals = 0x24;			// NiPoint3*
	constexpr uint32_t kGDColors = 0x28;			// NiColorA*
	constexpr uint32_t kGDUVs = 0x2C;				// NiPoint2*, texture set 0
	constexpr uint32_t kPixelDataPixels = 0x50;		// NiPixelData: u8* pixels
	constexpr uint32_t kPixelDataStride = 0x64;		// NiPixelData: bytes per pixel
	// BSShaderNoLightingProperty +0x60 NiTexture* (JIP). Its render-pass selector (vtbl +0xA0, 0x00B6F5C0) picks the
	// textured passes when this is set, and its InitGeometry (0x00B700D0) sets flags2 0x20 (vertex colours) when the
	// data has colours. Ghidra 2026-10-02, high
	constexpr uint32_t kNoLightingTexture = 0x60;
	constexpr uint32_t kAlphaFlags = 0x18;			// NiAlphaProperty u16 flags (JIP AlphaFlag)
	constexpr uint32_t kAlphaThreshold = 0x1A;		// NiAlphaProperty u8 test reference

	// Scene sun light, read for Mario's tint (D11). ShadowSceneNode +0xE0 LightingData* pSunLight, LightingData +0xF8
	// NiLight* (JIP netimmerse.h; JIP's minimap swaps this light). Xbox: ShadowSceneNode::SetSunLight is fed
	// Sky->Sun's light at Main::InitSky. high (JIP), effect type checked at runtime
	constexpr uint32_t kSSNSunLight = 0xE0;
	constexpr uint32_t kLightingDataLight = 0xF8;
	constexpr uint32_t kEffectType = 0x9D;			// NiDynamicEffect u8, 3 = directional
	constexpr uint8_t kEffectDirectional = 3;
	constexpr uint32_t kLightAmbient = 0xC8;		// NiLight NiColor ambientColor
	constexpr uint32_t kLightDiffuse = 0xD4;		// NiLight NiColor diffuseColor

	// --- Collision (FNV-SM64-RE findings/collision.md) ---

	// TES* singleton (JIP TES::GetSingleton); TES +0x08 GridCellArray*: +0x0C gridSize, +0x10 TESObjectCELL** (gridSize^2).
	// collision.md §1, medium-high (JIP GameData.h)
	constexpr uintptr_t kTES = 0x011DEA10;
	constexpr uint32_t kTESGridCells = 0x08;
	constexpr uint32_t kGridSize = 0x0C;
	constexpr uint32_t kGridCellArray = 0x10;
	// TESObjectCELL +0x4C TESObjectLAND*; LAND +0x28 LoadedLandData* (0xA4 B): +0x00 NiNode** block nodes (4),
	// +0x04 NiPoint3** vertex arrays (4 x 17x17, 128 apart), +0x98/+0x9C cell X/Y, +0xA0 base height added to vertex z.
	// Terrain collision is a heightfield built from those vertices per block (Xbox CellMopp::Create), not
	// +0x94 spLandRB (null on PC, M5.1 test). collision.md §5, high (JIP GameForms.h + Xbox GetVertex)
	constexpr uint32_t kCellLand = 0x4C;
	constexpr uint32_t kLandLoadedData = 0x28;
	constexpr uint32_t kLandDataMeshes = 0x00;
	constexpr uint32_t kLandDataVertices = 0x04;
	constexpr uint32_t kLandDataCellX = 0x98;
	constexpr uint32_t kLandDataCellY = 0x9C;
	constexpr uint32_t kLandDataBaseHeight = 0xA0;
	// NiNode m_children NiTArray @ 0x9C: data 0xA0, numObjs 0xA8 (JIP netimmerse.h / Ni_types.h, high)
	constexpr uint32_t kNodeChildren = 0xA0;
	constexpr uint32_t kNodeChildCount = 0xA8;
	// NiGeometryData u16 numVertices (JIP netimmerse.h, high)
	constexpr uint32_t kGDVertexCount = 0x08;

	// Static refs' Havok collision (M5.2). collision.md §1-§4.
	// TESObjectCELL +0xAC tList<TESObjectREFR> objectList (NVSE GameForms.h, high). Ref +0x64 renderState -> +0x14 root
	// NiNode (Get3D 0x0043FCD0, collision.md §1, high).
	constexpr uint32_t kCellObjectList = 0xAC;
	constexpr uint32_t kRefRenderState = 0x64;
	constexpr uint32_t kRenderStateRoot = 0x14;
	// NiObject vtbl +0x0C GetNiNode(): returns this for NiNodes, else null (JIP netimmerse.h, high)
	constexpr uint32_t kVtblGetNiNode = 0x0C;
	// NiAVObject +0x1C bhkNiCollisionObject*; its +0x10 bhkWorldObject*; bhkRefObject +0x08 hkReferencedObject*
	// (JIP havok.h + Xbox PDB, collision.md §2, high)
	constexpr uint32_t kAVCollisionObject = 0x1C;
	constexpr uint32_t kCollisionWorldObject = 0x10;
	constexpr uint32_t kBhkRefObject = 0x08;
	// NiAVObject +0x20 NiBound* world bound (centre NiPoint3, radius), used to skip far refs (JIP netimmerse.h, high)
	constexpr uint32_t kAVWorldBound = 0x20;
	// hkpRigidBody: +0x08 hkpWorld* (null when not in a world), +0x10 shape, +0xE8 motion type (4 keyframed, 5 fixed),
	// +0xF0 hkMotionState = hkTransform (3 rotation columns, translation +0x30, Havok units). JIP havok.h, high
	constexpr uint32_t kHkWorldObjectWorld = 0x08;
	constexpr uint32_t kHkRigidBodyShape = 0x10;
	constexpr uint32_t kHkRigidBodyMotionType = 0xE8;
	constexpr uint32_t kHkRigidBodyTransform = 0xF0;
	constexpr uint8_t kHkMotionKeyframed = 4;
	constexpr uint8_t kHkMotionFixed = 5;
	// hkpWorldObject +0x2C u8 collision layer (CollisionLayerTypes), +0x2D filter flags (0x40 = no collision).
	// JIP havok.h, medium-high (only used to skip triggers/water/non-collidable bodies; histogram logged)
	constexpr uint32_t kHkWorldObjectLayer = 0x2C;
	constexpr uint32_t kHkWorldObjectFilterFlags = 0x2D;
	constexpr uint8_t kHkFilterNoCollision = 0x40;
	// Vtables from RTTI (findings/addresses.md vtable row, high)
	constexpr uintptr_t kVtblBhkRigidBody = 0x010301B4;
	constexpr uintptr_t kVtblBhkRigidBodyT = 0x010C879C;
	constexpr uintptr_t kVtblHkpRigidBody = 0x010C7888;
	constexpr uintptr_t kVtblHkpMoppBvTreeShape = 0x010D0DAC;		// child shape @ +0x34
	// hkScaledMoppBvTreeShape (Bethesda, RTTI .?AVhkScaledMoppBvTreeShape@@): same vtable as hkpMoppBvTreeShape except the
	// dtor (0x00CC9D80, calls the base dtor), so the same child @ +0x34; most FNV statics use it. Ghidra 2026-10-03, high
	constexpr uintptr_t kVtblHkScaledMoppBvTreeShape = 0x010CA330;
	constexpr uintptr_t kVtblHkpListShape = 0x010CC3D8;				// hkArray<ChildInfo 0x10> @ +0x18, size @ +0x1C
	constexpr uintptr_t kVtblHkpBoxShape = 0x010C70B4;				// radius @ +0x10, half extents @ +0x20
	// hkpConvexVerticesShape: +0x40 vertex blocks (0x30 B: x[4], y[4], z[4]; vertex i = block i>>2, lane i&3), +0x44 block
	// count, +0x54 plane equations (16 B: n, d; outside where n.p + d > 0), +0x58 plane count. getSupportingVertex
	// 0x00D58EE0, convertVertexIdsToVertices 0x00D59060, castRay 0x00D592A0 (builds the planes from +0x50 if none). Ghidra
	// 2026-10-03, high (matches JIP layout)
	constexpr uintptr_t kVtblHkpConvexVerticesShape = 0x010D0FC4;
	constexpr uint32_t kConvexVerticesBlocks = 0x40;
	constexpr uint32_t kConvexVerticesBlockCount = 0x44;
	constexpr uint32_t kConvexVerticesPlanes = 0x54;
	constexpr uint32_t kConvexVerticesPlaneCount = 0x58;
	// hkpTransformShape: child @ +0x14, hkTransform @ +0x30. hkpConvexTransformShape: child @ +0x18, hkTransform @ +0x20.
	// Both read in their getAabb (0x00CF59E0 / 0x00D5D800) and castRay (0x00CF5B20 / 0x00D5D940), Ghidra 2026-10-03, high
	constexpr uintptr_t kVtblHkpTransformShape = 0x010CC41C;
	constexpr uintptr_t kVtblHkpConvexTransformShape = 0x010D102C;
	// hkPackedNiTriStripsShape (primary; hkpShapeContainer sub-object at +0x10): data @ +0x84, scale hkVector4 @ +0x90
	// (used if x != 1), as on Xbox. getChildShape 0x00CA3C40 is slot 5 of the container vtable, so its +0x74/+0x80 are
	// relative to primary+0x10 (M5.2 retest: reading +0x74 from the primary gave garbage). Data: +0x08 triangle count,
	// +0x0C vertex count, +0x10 u16 sub-part count, +0x14 triangles (8 B: u16 v0, v1, v2, welding), +0x18 float xyz
	// vertices, +0x24 sub-parts (12 B: filter, numVerts, material). collision.md §4, Ghidra 2026-10-03, high
	constexpr uint32_t kPackedShapeData = 0x84;
	constexpr uint32_t kPackedShapeScale = 0x90;
	constexpr uintptr_t kVtblHkPackedNiTriStripsShape = 0x010C755C;
	constexpr uintptr_t kVtblHkPackedNiTriStripsContainer = 0x010C753C;
	// hkpShape +0x08 bhkShape* wrapper; bhkShape +0x10 MATERIAL_TYPE (primitive shapes). collision.md §2, high
	constexpr uint32_t kHkShapeWrapper = 0x08;
	constexpr uint32_t kBhkShapeMaterial = 0x10;
	// float fHk2BSScale = 6.99913 (Havok -> game units), set at startup. findings/addresses.md, high
	constexpr uintptr_t kHk2BSScale = 0x011C5880;

	// --- Audio (FNV-SM64-RE findings/audio.md) ---

	// BSWin32Audio* singleton (JIP BSWin32Audio::Get). +0x05 bool initialised, +0x38 IDirectSound8* (DirectSoundCreate8
	// writes the interface pointer there in BSWin32Audio::Init 0x00AEA230). findings/addresses.md, high
	constexpr uintptr_t kBSWin32AudioPtr = 0x011F6D98;
	constexpr uint32_t kBSWin32AudioInitDone = 0x05;
	constexpr uint32_t kBSWin32AudioDevice = 0x38;
	// BSAudioManager instance: float volumes[12] at +0x140. [0..5] Master/Foot/Voice/Effects/Music/Radio sliders,
	// [6..11] the matching multipliers. Read by GetEffectiveVolume 0x00ADAC30 (JIP + Ghidra), high
	constexpr uintptr_t kBSAudioManagerVolumes = 0x011F6EF0 + 0x140;
	constexpr uint32_t kVolumeMaster = 0, kVolumeEffects = 3, kVolumeMasterMult = 6, kVolumeEffectsMult = 9;
	// float fMasterVolumeMult:Audio (default 1.3), static init 0x00FB7650. findings/addresses.md, high
	constexpr uintptr_t kMasterVolumeMult = 0x011F6E78;

	// --- Gameplay (FNV-SM64-RE findings/gameplay.md) ---

	// ActorValue HEALTH. gameplay.md §2, high
	constexpr UInt32 kAVHealth = 0x10;
	// Actor vt+0x3AC void DamageModActorValue(int av, float delta, Actor* attacker), delta < 0 = damage; HEALTH damage
	// goes on to HandleHealthDamage, which kills below 1. PlayerCharacter vtable 0x0108AA3C + 0x3AC = 0x0093B7A0, the
	// player override (Ghidra 2026-10-04). gameplay.md §2, high
	constexpr uint32_t kVtblActorDamageModActorValue = 0x3AC;
	// UInt8 Actor::inWater (JIP name; NVSE unk14C). gameplay.md §6, high
	constexpr uint32_t kActorInWater = 0x14C;
	// float RenderState::waterLevel, game units, -FLT_MAX = no water (TESObjectREFR::renderState +0x64, NVSE field).
	// Read by 0x0057B0A0; its NI2HK copy is bhkCharacterController+0x53C (set at 0x0057B1D0). JIP layout, Ghidra
	// 2026-10-04, gameplay.md §6, high
	constexpr uint32_t kRenderStateWaterLevel = 0x08;
	// ProcessLists singleton (JIP ProcessManager::Get; PC Actor::Kill passes it to RemoveAliveActor). gameplay.md §4, high
	constexpr uintptr_t kProcessLists = 0x011E0E80;
	// +0x80 high-process actors list (JIP highActors, Xbox AliveActorList, same offset), embedded head node
	// {Actor* data; node* next}. gameplay.md §4, high
	constexpr uint32_t kProcessListsHighActors = 0x80;
	// Actor vt+0x338 bool DoDamage(float health, float fatigue, Actor* attacker) = 0x0089D6F0: invulnerability check,
	// attacker/target combat aggro, DamageModActorValue(HEALTH) + DamageObject; returns IsDead (JIP
	// DamageHealthAndFatigue). gameplay.md §3, high
	constexpr uint32_t kVtblActorDoDamage = 0x338;
	// BaseProcess vt+0x418 void KnockExplosion(Actor* target, float x, float y, float z, float force) = 0x0091FEE0
	// (MiddleHighProcess; JIP PushActorAway). High process only (processLevel 0), as Cmd_PushActorAway 0x005D6B60
	// checks. gameplay.md §3, high (argument order re-read in Cmd_PushActorAway's disassembly, Ghidra 2026-10-04)
	constexpr uint32_t kVtblProcessKnockExplosion = 0x418;
	// float __cdecl AdjustPushForce(ActorValueOwner* target, int force): Cmd_PushActorAway's force scaling (user
	// name; JIP Actor::AdjustPushForce inlines it). Ghidra 2026-10-04, high
	constexpr uintptr_t kAdjustPushForce = 0x00646580;
	// float __cdecl CalcWeaponDamage(ActorValueOwner* attacker, TESObjectWEAP* weapon, float condition, float mult,
	// int, bool, ContChangesEntry* entry, bool hasAmmo) (no name; JIP ContChangesEntry::CalculateWeaponDamage pushes
	// these 8 args). With weapon null: 1 x Unarmed skill scale + UnarmedDamage AV (0x38), as decompiled. Ghidra
	// 2026-10-04, findings/addresses.md, medium-high (JIP call + body; null-weapon use is ours)
	constexpr uintptr_t kCalcWeaponDamage = 0x00644CE0;
	// ActorValue DamageResistance / DamageThreshold (NVSE GameAPI.h eActorVal_DamageResistance 18, _Damagethreshold 76)
	constexpr UInt32 kAVDamageResist = 18, kAVDamageThreshold = 76;
}
