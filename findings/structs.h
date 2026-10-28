// structs.h: VALIDATED layouts for FNV PC 1.4.0.525 (documentation only, not mod code).
// Each field: offset, how verified, confidence. Only offsets actually checked in Ghidra are listed.
#pragma once
#include <cstdint>

// BSTimer (instance appTimer @ 0x011F6394; NVSE calls this struct "TimeGlobal").
// Xbox: class BSTimer, instance appTimer 0x840B796C.
struct BSTimer {
    uint8_t  disableCounter;     // +0x00  (NVSE)                                      conf: NVSE only
    uint8_t  pad01[3];
    float    fpsClamp;           // +0x04  (NVSE)                                      conf: NVSE only
    float    fpsClampRemainder;  // +0x08  (NVSE)                                      conf: NVSE only
    float    secondsPassed;      // +0x0C  BSTimer::GetSeconds 0x84D030 returns this    conf: HIGH
    float    lastSecondsPassed;  // +0x10  (NVSE)                                      conf: NVSE only
    uint32_t msPassed;           // +0x14  (NVSE)                                      conf: NVSE only
    // ... (NVSE: unk18, isChangeTimeMultSlowly @+0x1C)
};

// Main (Xbox class name; user's Ghidra name "TESMain"; JIP "OSGlobals"). sizeof == 0xA4
// (FormHeap::Allocate(0xA4) in the main loop function 0x86A850). Instance pointer: 0x011DEA0C (NVSE/JIP).
struct Main {
    uint8_t  bOneMore;           // +0x00  used in main loop (user field name)          conf: HIGH
    uint8_t  bQuitGame;          // +0x01  (JIP)                                        conf: JIP only
    uint8_t  bExitToMainMenu;    // +0x02  (JIP)                                        conf: JIP only
    uint8_t  bIsWindowActive;    // +0x03  (JIP)                                        conf: JIP only
    uint8_t  byte04, byte05;     // +0x04
    uint8_t  bFlyCamera;         // +0x06  tested in OnIdle_PollControls/UpdatePlayer at Xbox bFlyCamera spots  conf: HIGH
    uint8_t  bFreezeTime;        // +0x07  OnIdle guards (user field name) == Xbox this->bFreezeTime            conf: HIGH
    void*    hWnd;               // +0x08  (JIP; user field hWnd)                       conf: MEDIUM
    // ... up to 0xA0: NiCamera* (JIP)
};

// VATS singleton pVATS @ 0x011F2250 (NVSE "VATSCameraData").
struct VATS_partial {
    uint8_t  unk00[8];
    int32_t  mode;               // +0x08  == 4 => VATS_PLAYBACK (OnIdle_UpdateTimer, GetPlayerUpdateMult)  conf: HIGH
};

// NiAVObject (PC). Validated: m_flags +0x30 (JIP + Xbox PDB m_uFlags +0x30); AppCulled = bit 0x1 (Xbox SetAppCulled -> SetBit(1)).
// DIVERGENCE: PC transforms at +0x34 (local: rot 0x24 + translate @+0x58 + scale) / +0x68 (world) [JIP]; Xbox +0x40/+0x80.
struct NiAVObject_partial {
    uint8_t  base[0x18];          // NiObjectNET
    void*    m_parent;            // +0x18
    void*    m_collisionObject;   // +0x1C
    void*    m_worldBound;        // +0x20
    uint8_t  m_propertyList[0xC]; // +0x24
    uint32_t m_flags;             // +0x30  bit0 = AppCulled (hidden)   conf: HIGH
    float    localRot[9];         // +0x34  (JIP)                      conf: MEDIUM
    float    localTranslate[3];   // +0x58  (JIP SetPos writes here)   conf: MEDIUM
    float    localScale;          // +0x64
    // world transform +0x68 (JIP)
};

// PlayerCharacter (partial, PC)
//   +0x1EC  QueuedTargetLoc* pQueuedTargetLoc   (HandlePositionPlayerRequest 0x93BEA0)   conf: HIGH
//   +0x648  uint8 saved b3rdPerson (temp 3rd person), +0x649 uint8 temp-3rd flag      conf: MEDIUM
//   +0x64A  bool b3rdPerson (current view; Xbox +0x65A)        (Get3D 0x950B60, UpdateCamera)  conf: HIGH
//   +0x64B  bool bActually3rdPerson (Xbox +0x65B)                                              conf: MEDIUM
//   +0x64C  bool bWant3rdPerson (Xbox +0x65C; NVSE 'bThirdPerson')  (SetFirstPerson 0x950110) conf: HIGH
//   +0x680  uint8 disabledControlFlags             (SetControlsDisabled 0x95F530)          conf: HIGH
//   +0x694  NiNode* playerNode (1st person)        (NVSE)                                  conf: NVSE

// Actor (partial, PC)
//   +0x108  uint32 lifeState  (1,2 = dead/dying; 6 = essential down)  Actor::GetLifeState 0x4F8960 / IsDead 0x8844F0   conf: HIGH
//   +0x1AC  uint32 sitSleepState                                       Actor::GetSitSleepState 0x8D8800                 conf: HIGH
// hkpCharacterProxy (Havok, PC)
//   +0x10   hkVector4 m_velocity (Havok units/s)          setLinearVelocity 0xCAC960   conf: HIGH
//   +0x58   float m_keepDistance, +0x5C float m_keepContactTolerance (setPosition 0xCAC920)  conf: HIGH
// hkpShapePhantom: +0x08 m_world, +0xB0 motion state, +0xE0 position (setPosition 0xC9E990)  conf: HIGH
// bhkCharacterController: +0x08 hkpCharacterProxy* (wrapped), +0x570 RotCenter (hkVector4), +0x594 shape phantom wrapper  conf: MEDIUM (from SetPosition_hkVector4 0xC6E390)

// OSInputGlobals (PC, singleton *(OSInputGlobals**)0x11F35CC). Offsets from JIP, the ones marked HIGH re-checked in Poll 0xA23010 / GetControlState 0xA24660.
//   +0x0000 uint8 controllerDisabled                    conf: HIGH
//   +0x0008 IDirectInput8*                              conf: JIP
//   +0x000C IDirectInputDevice8* joysticks[8]           conf: HIGH (Poll loop)
//   +0x002C IDirectInputDevice8* keyboard, +0x0030 mouse  conf: HIGH
//   +0x0034 joystick states, stride 0xA0 (DIJOYSTATE 0x50 cur + 0x50 prev)  conf: HIGH
//   +0x18F4 int numJoysticks                             conf: HIGH (GetButtonState)
//   +0x18F8 uint8 currKeyStates[256], +0x19F8 lastKeyStates[256]   conf: HIGH
//   +0x1B24 DIMOUSESTATE cur (0x14), +0x1B38 prev        conf: HIGH
//   +0x1B88 uint8 vibrationEnabled                       conf: MEDIUM (FUN_00A257C0)
//   +0x1B94 uint8 keyBinds[0x1C], +0x1BB0 mouseBinds, +0x1BCC joystickBinds, +0x1BE8 controllerBinds  conf: HIGH
// Globals: XINPUT_STATE g_xinputState @0x11F35A8 (sThumbLX @0x11F35B0, LY @0x11F35B2, RX @0x11F35B4, RY @0x11F35B6), prev @0x11F35B8, bool g_bGamepadActive @0x11F35C8

// NiAVObject world transform (PC): rotate @ +0x68 (NiMatrix3 row-major 3x3), translate @ +0x8C, scale @ +0x98.  conf: HIGH (+0x68 via NiCamera::GetWorldDirection 0x45BBA0), MEDIUM for +0x8C/+0x98 (layout inference + JIP)
// Camera forward = column 0 of the world rotate; up = column 1.

// ---- Collision (P5) ----
// NiAVObject+0x1C m_collisionObject -> bhkNiCollisionObject{ +0x0C u16 flags, +0x10 bhkWorldObject* }      conf: HIGH (Xbox PDB + JIP)
// bhkRefObject+0x08 hkReferencedObject* (hkpRigidBody / hkpShape)                                         conf: HIGH
// hkpWorldObject: +0x08 hkpWorld*, +0x0C userData, +0x10 hkpCollidable{ +0x0 shape*, +0x4 key, +0x8 hkMotionState* }   conf: HIGH
// hkMotionState: hkTransform{ rotation 3x hkVector4 (+0x00), translation (+0x30) }  (Havok units)          conf: MEDIUM-HIGH (JIP)
// hkpShape: +0x08 userData (bhkShape*), +0x0C hkpShapeType                                                 conf: HIGH
// bhkShape: +0x10 MATERIAL_TYPE                                                                            conf: HIGH
// hkpMoppBvTreeShape: +0x34 child hkpShape*                                                                conf: HIGH (PC getContainer)
// hkpListShape: +0x18 ChildInfo* (16 B, shape @0), +0x1C count                                             conf: HIGH
// hkpBoxShape: +0x10 radius, +0x20 halfExtents hkVector4                                                   conf: HIGH
// hkpCapsuleShape: +0x10 radius, +0x20 p1, +0x30 p2                                                        conf: HIGH
// hkPackedNiTriStripsShape (PC!): +0x10 container vtbl, +0x74 hkPackedNiTriStripsData*, +0x80 hkVector4 scale  conf: HIGH (getChildShape 0xCA3C40). Xbox: +0x84/+0x90
struct hkPackedNiTriStripsData_PC {   // conf: HIGH
    void*    vtbl;            // +0x00 (NiObject)
    uint32_t refCount;        // +0x04
    uint32_t numTriangles;    // +0x08 uiKeyCount
    uint32_t numVertices;     // +0x0C
    uint16_t numSubParts;     // +0x10
    uint16_t pad12;
    struct Tri { uint16_t v[3]; uint16_t welding; } *triangles;   // +0x14
    float  (*vertices)[3];    // +0x18 NiPoint3, Havok units (PC uses these; Xbox uses half-floats @+0x20)
    uint32_t unk1C;
    uint16_t* compressedVerts;// +0x20 (unused by PC getChildShape)
    struct SubPart { uint32_t filter; uint32_t numVerts; uint32_t material; } *subParts;  // +0x24
};
// TESObjectLAND+0x28 LoadedLandData* (0xA4): +0x00 NiNode** quads, +0x04 NiPoint3** verts (4x289), +0x50 hkpMoppCode*, +0x94 bhkRigidBody* spLandRB, +0x98/+0x9C cellX/Y, +0xA0 baseHeight   conf: HIGH (Xbox PDB == JIP)

// ---- Rendering (P6) ----
// NiGeometryData: +0x08 u16 numVerts, +0x0C u16 dataFlags, +0x0E u16 dirtyFlags, +0x10 NiBound, +0x20 verts, +0x24 normals, +0x28 colors, +0x2C uvs   conf: HIGH (JIP + Xbox MarkAsChanged)
// NiTriBasedGeomData: +0x40 u16 numTriangles, +0x42 u16 activeTriangles; NiTriShapeData(0x58): +0x44 triListLen, +0x48 NiTriangle*                conf: HIGH
// NiTriShapeDynamicData (0x5C): +0x58 u16 activeVertices, +0x5A u16 activeTriangles                                                              conf: HIGH (ctor 0xA816A0)
// NiGeometry (0xC4): +0x9C alpha, +0xA0 culling, +0xA4 material, +0xA8 shader, +0xAC stencil, +0xB0 texturing, +0xB8 geometryData                  conf: MEDIUM-HIGH (JIP)
// BSShaderNoLightingProperty (0x80): +0x60 NiTexture* srcTexture                                                                                     conf: MEDIUM-HIGH (JIP)
// NiPixelData (0x74): +0x08 NiPixelFormat(0x44), +0x50 pixels, +0x60 mipmaps, +0x64 stride                                                            conf: HIGH

// ---- Gameplay (P9) ----
// ActorValue::Index: HEALTH = 0x10, FATIGUE = 0x16, ACTION_POINTS = 0x0C                                           conf: HIGH (Xbox enum, PC code constants)
// Actor (PC): +0xA4 ActorValueOwner (vtbl: [3] GetActorFloatValue cur, [1] GetBaseActorFloatValue, [8] GetPermanentActorFloatValue)
//             +0xC0 Actor* killer, +0x108 lifeState (0 alive,1 dying,2 dead,3 unconscious,4 reanimate,5 restrained,6 essential_down)
//             +0x14C u8 bInWater, +0x14D u8 bSwimming        (Xbox: +0xD0 / +0x118 / +0x15C / +0x15D, i.e. PC = Xbox - 0x10 here)   conf: HIGH
// Actor vtable (PC): +0x22C IsDead(bool), +0x338 DoDamage, +0x39C TempMod(f), +0x3A8 PermMod(i), +0x3AC DamageModAV(f), +0x3B0 DamageModAV(i), +0x4B8 HandleHealthDamage  conf: HIGH
// REFR vtable (PC): +0x144 DamageObject(float, bool)                                                                 conf: HIGH
// Process vtable (PC == Xbox): +0x40C GetKnockState, +0x410 SetKnockState, +0x414 KnockDown, +0x418 KnockExplosion(Actor*, NiPoint3, float), +0x41C UpdateKnockState, +0x420 ClearKnockState  conf: HIGH
// enum KNOCK_STATE { NORMAL=0, EXPLODE=1, EXPLODE_LEAD_IN=2, OUT=3, OUT_LEAD_IN=4, QUEUED=5, GET_UP=6 };              conf: HIGH (Xbox enum; PC GetKnockedState maps 3,4 -> 1)
// ProcessLists @0x011E0E80: +0x80 BSSimpleList<Actor*> AliveActorList, +0x88 Actor* actorsCloseToPlayer[50], +0x150 int count   conf: HIGH (Xbox type == JIP)
// bhkCharacterController: +0x500 hkVector4 VelocityMod, +0x524 float fVelocityTime, +0x53C float fWaterHeight            conf: HIGH (Xbox type == JIP)
