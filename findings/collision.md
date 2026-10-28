# P5: Collision / world geometry (DONE, 2026-10-01)

Goal: feed `sm64_static_surfaces_load` / `sm64_surface_object_create` with triangles near Mario.
PC = FalloutNV.exe 1.4.0.525; Xbox = Fallout_Debug.exe. Havok layouts were checked on the Xbox PDB **and** JIP or PC code. Every Havok struct in Bethesda's own mesh shapes was re-checked on PC, (an earlier note said the packed mesh layout differs between Xbox and PC; that was wrong, see §4).

## 1. Finding geometry near Mario (enumeration)
| Item | PC | Source | Conf. |
|---|---|---|---|
| `TES*` singleton | `*(TES**)0x011DEA10` | NVSE/JIP | high |
| `TES::gridCellArray` | TES+0x08 → GridCellArray: `gridSize` +0x0C, `TESObjectCELL** gridCells` +0x10 (gridSize²) | JIP | medium-high |
| `TES::currentInterior` | TES+0x34 | JIP | medium-high |
| `TESObjectCELL::objectList` (tList<TESObjectREFR>) | cell+0xAC | JIP | medium-high |
| `TESObjectCELL::land` / `waterHeight` / `worldSpace` | +0x4C / +0x50 / +0xC0 | JIP/NVSE | medium-high |
| Ref 3D root | `TESObjectREFR::Get3D` 0x43FCD0 (user) = renderState(+0x64)->rootNode(+0x14) | P2 | high |
| Lifecycle events | NVSE `kMessage_OnCellStateChange`, `OnRefSet3D/Unset3D`, `OnCellRefsLoaded` | NVSE | high |
Alternative (not explored): a Havok broadphase AABB query on `bhkWorldM` (`*(bhkWorldM**)0x11CA0D8`, JIP) → hkpWorld.

## 2. From a NiNode to Havok shapes
```
NiAVObject +0x1C  m_collisionObject  (bhkNiCollisionObject / bhkCollisionObject, vtbl 0x103046C)   [Xbox PDB + JIP]
  +0x10  spWorldObject -> bhkRigidBody (vtbl 0x10301B4) / bhkRigidBodyT (0x10C879C) / phantoms     [Xbox PDB + JIP]
           +0x08  phkObject -> hkpRigidBody (vtbl 0x10C7888)                                       [Xbox PDB + JIP]
                     +0x10  m_collidable.m_shape  -> hkpShape*                                     [Xbox PDB + JIP]
                     +0x18  m_collidable.m_motion -> hkMotionState* (hkTransform first: rot 3x hkVector4 @+0, translation @+0x30, Havok units)   [JIP + Havok]
                     motion type @ hkpMotion (rigidBody+0xE0) +0x08: 4 keyframed, 5 fixed (static)  [JIP]
hkpShape: +0x08 m_userData (back-pointer to the bhkShape wrapper), +0x0C m_type (hkpShapeType)        [Xbox PDB + JIP]
bhkShape (wrapper): +0x08 hkpShape*, +0x10 eMaterial (MATERIAL_TYPE, used for primitive shapes)       [Xbox PDB + JIP]
```
World (game units) = hkTransform applied to the local Havok vertex, then × 7 (1 / fBS2HkScale, P2). Inferred from JIP's SetPos (×1/7 game→Havok). Conf. medium until checked in game.

## 3. Shape types to handle (PC vtables found via RTTI, labelled `vtbl_*` in Ghidra)
| Shape | PC vtable | Child / data (PC) | Conf. |
|---|---|---|---|
| `hkScaledMoppBvTreeShape` (Bethesda, used by most statics: M5.2 log 1711 of 1877 bodies) | 0x10CA330 | same vtable as hkpMoppBvTreeShape except the dtor 0x00CC9D80 → child @ +0x34 | high |
| `hkpMoppBvTreeShape` (type 10) | 0x10D0DAC | child hkpShape* @ **+0x34** (getContainer derefs [this+0x34]; Xbox m_child.m_childShape @0x30+4) | high |
| **`hkPackedNiTriStripsShape`** (Bethesda mesh; getType = **0x17** TRIANGLE_COLLECTION) | primary 0x10C755C, container 0x10C753C | see §4 | high |
| `hkNiTriStripsShape` (unpacked) | 0x10C75C4 | Xbox: NiTLargePrimitiveArray @+0x94 (PC not verified) | low |
| `hkpListShape` (type 9) | 0x10CC3D8 | `hkArray<ChildInfo>` @ +0x18 (ptr, size @+0x1C); ChildInfo 0x10 { hkpShape* @0 } | high |
| `hkpTransformShape` (15) | 0x10CC41C | child @ +0x14 (container @+0x10), hkTransform @ +0x30 (getAabb 0x00CF59E0, castRay 0x00CF5B20, 2026-10-03) | high |
| `hkpConvexTransformShape` (12) | 0x10D102C | child @ +0x18; hkTransform @ +0x20 (getAabb 0x00D5D800, castRay 0x00D5D940, 2026-10-03) | high |
| `hkpBoxShape` (4) | 0x10C70B4 | halfExtents hkVector4 @ +0x20; radius (convex skin) @ +0x10 → 12 triangles | high |
| `hkpCapsuleShape` (5) | 0x10CE25C | p1 @ +0x20, p2 @ +0x30, radius @ +0x10 (approximate with a box/prism) | high (JIP + slot read) |
| `hkpSphereShape` (1) | 0x10CB5AC | radius @ +0x10 | medium |
| `hkpConvexVerticesShape` (6) | 0x10D0FC4 | vertex blocks @ +0x40 (0x30 B: x[4], y[4], z[4]), block count @ +0x44, plane equations @ +0x54 (n, d; outside where n·p+d > 0), plane count @ +0x58 (getSupportingVertex 0x00D58EE0, castRay 0x00D592A0). Faces = on-plane vertices per plane | high (2026-10-03) |
| `hkpExtendedMeshShape` (14), `hkpCompressedMeshShape` (16) | 0x10D0ECC / 0x10EBA5C | present in the exe; usage in FNV content unknown | low |

## 4. `hkPackedNiTriStripsShape`: the main static mesh format (buildings, rocks, clutter)
**PC layout (verified in `hkPackedNiTriStripsShape::getChildShape` 0x00CA3C40 / `getNextKey` 0x00CA35D0):**
| Field | PC | Xbox | Note |
|---|---|---|---|
| hkpShapeContainer (2nd vtable) | +0x10 | +0x10 | |
| `hkPackedNiTriStripsData*` | **+0x84** | +0x84 | same (corrected 2026-10-03: getChildShape is a container-vtable method, `this` = shape+0x10, so its +0x74 = shape+0x84) |
| scale hkVector4 (applied if x != 1.0) | **+0x90** | +0x90 (vScale) | same (same correction) |
| `hkPackedNiTriStripsData::uiKeyCount` (triangle count) | data+0x08 | same | |
| `uiVertCount` / `usPartCount` | data+0x0C / +0x10 | same | |
| `TriangleData* paTriData` (8 B: u16 v0, v1, v2, welding) | data+0x14 | same | |
| **`NiPoint3* paVertData` (float xyz, Havok units)** | **data+0x18** | Xbox reads `paCompressedVertData` (+0x20, half-floats) instead | **PC uses floats** |
| `SubPartData* paSubPartData` (12 B: filter, numVerts, **MATERIAL_TYPE material**) | data+0x24 | same | |
| Shape key | `triIndex & 0xFFFFFF`, `subPart << 24` | same | |
A triangle's sub-part = first k with v0 < Σ numVerts[0..k] (getNextKey logic). Material = subPart[k].material.

## 5. Terrain (LAND) (corrected 2026-10-03, M5.1)
`TESObjectCELL+0x4C land` → `TESObjectLAND+0x28 LoadedLandData*` (PC JIP layout == Xbox `TESObjectLAND::LoadedLandData`, 0xA4 bytes).
- **`+0x94 spLandRB` is null on PC** (M5.1 in-game log: 9/9 loaded cells). The terrain collision is not there: Xbox `TESObjectLAND::Attach3D` calls `CellMopp::Create(cellMopp, blockTriStrips[4], 4, fBaseHeight)`, which builds **one fixed bhkRigidBody per block with a `bhkTriSampledHeightFieldBvTreeShape`** (17×17, scale NI2HK(128, 1, 128), heights from the block geometry's vertices, offset fBaseHeight), stored in the cell's `ExtraCellMopp` (`CellMopp` = NiRefObject + `NiTArray<NiPointer<bhkRigidBody>>` @+0x08). Conf. high (Xbox), PC location of ExtraCellMopp not looked up.
- **Used instead (M5.1):** the same source data. `+0x04 ppVertices` = 4 blocks × 289 NiPoint3 (17×17, 128 apart); if null, `+0x00 ppMesh[b]` → child 0 geometry → NiGeometryData vertices (Xbox `GetVertex` does the same fallback). World z = vertex z + `+0xA0 fBaseHeight` (Xbox GetVertex). Block b covers x from cellX·4096 + (b%2)·2048 and y from cellY·4096 + (b/2)·2048 (Attach3D bounds). **Vertex x/y are relative to the cell centre** (cellX·4096+2048, cellY·4096+2048), row-major with x fastest (M5.1 log, 2026-10-03; the plugin still calibrates per block). Conf. high.
- `+0x98/+0x9C` cell X/Y (confirmed on PC by the M5.1 log), `+0xA0` base height.

## 6. Materials → SM64
`MATERIAL_TYPE` (Xbox enum; matches the FO3/FNV havok material list in NIFs): 0 STONE, 1 CLOTH, 2 DIRT, 3 GLASS, 4 GRASS, 5 METAL, 6 ORGANIC, 7 SKIN, 8 WATER, 9 WOOD, 10 HEAVYSTONE, 11 HEAVYMETAL, 12 HEAVYWOOD, 13 CHAIN, 14 BOTTLECAP, 15 ELEVATOR, 16 METAL_HOLLOW, 17 METAL_SHEET, 18 SAND, 19 CONCRETE_BROKEN, 20 VEHICLE_BODY, 21–22 VEHICLE_PART_*, 23 BARREL, 24 BOTTLE, 25 SODA_CAN, 26 PISTOL, 27 RIFLE, 28 SHOPPING_CART, 29 LUNCHBOX, 30 BABY_RATTLE, 31 RUBBER_BALL, 32 CHAINLINK, 33 TILE, 34 CARPET, 35 TUMBLEWEED.
(NVSE's `kMaterial_*` enum is a different one, the impact/sound material on forms.)
Suggested terrain mapping (design, see open-questions D10): SAND → TERRAIN_SAND; GRASS/DIRT/ORGANIC/CLOTH/CARPET → TERRAIN_GRASS; WOOD/HEAVYWOOD → TERRAIN_SPOOKY; WATER → TERRAIN_WATER; everything else → TERRAIN_STONE.

## 7. Static vs dynamic
- Fixed motion (type 5) bodies → static surfaces (reload when Mario moves / cells change).
- Keyframed (4: doors, elevators, moving platforms) and dynamic bodies → `sm64_surface_object_create`, transform updated from hkMotionState each tick (like the SA mod's 30-tick re-query).
- Water: `TESObjectCELL::waterHeight` +0x50 (exteriors also TESWorldSpace +0x7C, NVSE) → `sm64_set_mario_water_level`.

## 8. Not done / to verify in game
- Exact Havok → game scale (×7) and whether packed-mesh vertices are body-local (expected) or already world.
- Convex vertices shape hull; extended/compressed mesh usage; hkNiTriStripsShape PC layout.
- (done 2026-10-03: hkpConvexTransformShape transform @ +0x20, see §3)

## 9. Winding (M5.2, 2026-10-03)
Havok triangle meshes are two-sided, so the stored winding of `hkPackedNiTriStripsShape` is arbitrary: in Goodsprings, 1049 meshes had outward net volume and 567 inward. SM64 relies on winding (a wall pushes along its normal; a floor needs normal.y > 0), so the plugin re-orients each connected piece of a mesh (consistent across shared edges, then outward by signed volume, or upward for open pieces).
