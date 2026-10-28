# P6: Rendering a dynamic mesh (DONE, 2026-10-01)

Goal: show libsm64's per-frame mesh (≤ 1024 triangles, non-indexed, pos/normal/color/uv, world space in SM64 units) and its 704×64 RGBA atlas.
PC = FalloutNV.exe 1.4.0.525. Xbox = Fallout_Debug.exe. "JIP" = an address used by JIP LN NVSE (shipped, widely run code).

## 1. Building blocks (PC)
| Item | PC | Xbox | Signature (PC, __thiscall unless noted) | Conf. | How verified |
|---|---|---|---|---|---|
| Ni allocator | 0x00AA13E0 (cdecl size) | | `void* (size)` | high | JIP (`Ni_Alloc`) + used by Create helpers |
| `NiTriShapeData::NiTriShapeData` | **0x00A7B630** | 0x83953890 | `(this, numVerts, NiPoint3* v, NiPoint3* n, NiColorA* c, NiPoint2* uv, numTexSets, nbtMethod, numTris, NiTriangle* tris)` | high | JIP Create + vtable 0x109DAC4 ref |
| `NiTriShapeDynamicData::NiTriShapeDynamicData` | **0x00A816A0** | 0x83960380 | `(this, usVertices, v, n, c, uv, texSets, nbt, usTriangles, triList, usActiveVertices, usActiveTriangles)` | high | body (writes active counts) + Xbox signature |
| `NiTriShape::NiTriShape(NiTriShapeData*)` | **0x00A74480** | | `(this, data)`; alloc 0xC4 | high | JIP + vtable 0x109D454 ref |
| `BSShaderNoLightingProperty` ctor | **0x00B6FC90** | | `(this)`; alloc 0x80 | high | JIP |
| `NiMaterialProperty::Create` / `NiAlphaProperty::Create` | 0x00A756D0 / 0x00A5CEB0 (cdecl) | | | medium (Material: JIP only); **Alpha: high** | Alpha disassembled 2026-10-02: NiNew(0x1C), ctor 0xA5C200, flags 0xEC, threshold 0, vtbl 0x10162DC |
| `NiPixelData::NiPixelData(w, h, const NiPixelFormat&, mipmaps, faces)` | **0x00A7C190** | 0x83955030 | mipmaps 0 = compute; alloc 0x74 | high | body = Gamebryo ctor |
| **`NiPixelFormat::RGBA32`** (static, 0x44 B) | **0x011AA2A0** | 0x8412FC18 | | high | "has alpha" (0xA78010) + 32-bpp selector (0xA8C120) + Xbox spacing 0x48 |
| other formats | PAL4 0x11AA180, PALA4 0x11AA1C8, PAL8 0x11AA210, PALA8 0x11AA258, RGB24 0x11AA2E8 | | | high | |
| **`NiSourceTexture::Create(NiPixelData*, const char* name, FormatPrefs&)`** (static, cdecl) | **0x00A5FF10** | 0x8391B060 | pixelData stored @ +0x3C, FormatPrefs (12 B) @ +0x18; renderer data created if 0x11A9594 | high | body + ctor 0xA5FC10 |
| `NiSourceTexture::Create(NiFile*, …)` / `Create(path)` (JIP uses the path one) | 0x00A5FE30 / 0x00A5FD70 | 0x8391ADD8 / 0x8391AC58 | | high | |
| `NiAVObject::UpdateProperties()` | 0x00A5A040 | | builds NiPropertyState from the parent, then vtbl+0xB0 | medium-high | body + Gamebryo semantics |
| `BSShaderManager::maybe_InitGeometryRecurse(obj, 0, 0, 0)` | 0x00B57BD0 | | per-geometry shader setup (0xB579E0) | medium | JIP AssignGeometryProps |
| `NiNode::UpdateDownwardPass(updateData, flags)` (user) | 0x00A5DD70 | | transforms/bounds refresh | high | user + JIP |
| NiNode child ops | vtbl +0xDC `AddObject(child, firstFree)`, +0xE8 `RemoveObject(child)` (NiNode vtbl 0x109B5AC) | | | medium-high | JIP |

## 2. Layouts (PC)
- `NiGeometryData` (0x40): +0x08 u16 numVertices, +0x0C u16 dataFlags, **+0x0E u16 dirtyFlags** (Xbox `MarkAsChanged` = `dirty |= flags & 0xFFF`), +0x10 NiBound, +0x20 vertices, +0x24 normals, +0x28 colors (NiColorA), +0x2C uv, +0x34 buffer data. [JIP + Xbox]
- `NiTriBasedGeomData`: +0x40 u16 numTriangles, +0x42 u16 activeTriangles. `NiTriShapeData` (0x58): +0x44 triListLength, +0x48 NiTriangle*. `NiTriShapeDynamicData` (0x5C): **+0x58 u16 activeVertices, +0x5A u16 activeTriangles**. [PC ctor 0xA816A0]
- `NiGeometry` (0xC4): property slots +0x9C alpha, +0xA0 culling, +0xA4 material, **+0xA8 shader (BSShaderProperty)**, +0xAC stencil, +0xB0 texturing, **+0xB8 NiGeometryData***, +0xBC skin, +0xC0 NiShader. [JIP]
- `BSShaderNoLightingProperty` (0x80): +0x1C shaderType, +0x20/+0x24 flags1/flags2, +0x28 alpha, **+0x60 NiTexture* srcTexture**, +0x64 texturePath. [JIP]
- `NiPixelData` (0x74): +0x08 NiPixelFormat, +0x50 pixels, +0x54/+0x58 width/height arrays, +0x5C offsets, +0x60 mipmaps, +0x64 pixel stride. [JIP + PC ctor]
- NiAVObject property list (DList) @ +0x24 (head) / +0x28 (tail) / +0x2C count. [JIP AddProperty]

## 3. Known-working recipe (JIP draws debug shapes this way; documentation only)
`shape = NiTriShape(data)` → AddProperty(`BSShaderNoLightingProperty`) → AddProperty(`NiAlphaProperty`) → `AssignGeometryProps` = `NiAVObject::UpdateProperties()` + `BSShaderManager::maybe_InitGeometryRecurse(shape,0,0,0)` → parent->AddObject(shape) → `UpdateDownwardPass`.
For Mario: `NiTriShapeDynamicData` sized 3072 verts / 1024 tris (identity triangle list), update vertex/normal/color/uv arrays each frame, set `activeTriangles = numTrianglesUsed` (and activeVertices), mark dirty (+0x0E) and refresh the bound (+0x10).
Texture: `NiPixelData(704, 64, RGBA32, 1, 1)` → copy the atlas into +0x50 pixels → `NiSourceTexture::Create(pixelData, name, prefs)` → put it in the shader property `srcTexture` (+0x60) or an `NiTexturingProperty` (vtbl 0x109C514).

## 4. Where to attach (scene graph)
- `Main::spWorldRoot` SceneGraph 0x11DEB7C (P1). Child 0 = camera root (P4).
- `TES::objRoot` (TES+0x0C, JIP) = root of world objects; `ShadowSceneNode` via user `GetShadowSceneNode(0)` 0x00450B80 / `*(ShadowSceneNode**)0x11F91C8` (JIP) for world lighting.
- Or under the player's 3rd-person root (`TESObjectREFR::Get3D` 0x43FCD0), with the vanilla body parts AppCulled (P2): it moves with the player (option A of the camera plan), but don't cull the whole root.

## 5. Open (see open-questions D11)
- Lighting: BSShaderNoLightingProperty is unlit (texture × vertex color). Lit output would need BSShaderPPLightingProperty (no RTTI type descriptor; not located) or a manual ambient/sun tint (as the SA mod does with ambient colours).
- RGBA vs BGRA byte order of RGBA32 → D3D format: test in game (the SA mod swapped R/B for RenderWare).
- Culling: refresh NiBound or set `kNiFlag_AlwaysDraw` (0x800, JIP flags) on the geometry.
- Thread safety: mutate the geometry only on the main thread, before `Main::Swap` (P1 frame order).

## 6. Plugin notes (M3.1, 2026-10-02)
- Scene-graph layout from Main init (0x0086D590): world SceneGraph → ShadowSceneNode (`*(0x11F91C8)`), whose children are set by index: 0 Sky, 1 Weather, 2 LODRoot (0x11DEA14), 3 ObjectLODRoot (0x11DEA18), 5 Master Particle Systems. The plugin **appends** its own NiNode (`AddObject(child, firstFree=false)`) so it never lands in a reserved slot.
- Arrays passed to the NiTriShapeData ctor are owned by the data object: allocate them with `FormHeap_Allocate` (the dtor frees them with MemoryManager::Deallocate).
- Dynamic updates (Gamebryo semantics, checked on Xbox `PackGeometryBuffer`): a buffer is skipped only when its consistency is STATIC (0x4000) and the VB chip is valid; otherwise it is repacked for the dirty bits (`+0x0E |= mask`, 0xFFF = full repack, as its bForce path). New data is mutable by default.
- Not verified on PC yet (M3.1 test): that the PC/BSShader path honours the same dirty bits; the backface winding of SM64's triangle order.

## 7. Texturing the unlit shader (M3.2, 2026-10-02, Ghidra)
- `BSShaderNoLightingProperty` ctor 0x00B6FC90 sets shaderType (+0x1C) 0x15, shaderIndex (+0x58) 0x21, flags1 |= 0x80000000, flags2 |= 1; srcTexture (+0x60) and texturePath (+0x64) null. **high**
- Its per-geometry setup (vtbl +0x9C, 0x00B700D0, reached through InitGeometry) sets flags2 0x20 (vertex colours) when the data has a colour array (data +0x28), and flags1 0x02 when the geometry has a skin instance (+0xBC). **high**
- Its render-pass selector (vtbl +0xA0, 0x00B6F5C0, not defined as a function in Ghidra) picks pass IDs 0x42–0x49 from: srcTexture (+0x60) set or not, vertex colours, skinned (0x4A2020 = flag test by index). With srcTexture set and vertex colours, pass 0x44 (0x48 skinned); without a texture, 0x43/0x47. So **setting +0x60 before InitGeometry is enough to get a textured pass**. Then, if the alpha property (+0x9C) has no blending, it picks the opaque render group (pass byte +6). **high** (control flow), pass semantics inferred.
- `NiPixelData` ctor 0x00A7C190: `(w, h, const NiPixelFormat*, mipmaps (0 = full chain), faces)`; copies the 0x44-byte format, stride (+0x64) = bits/8 from format byte 1, allocates the pixel buffer (+0x50). Refcount 0 on return. **high**
- `NiSourceTexture::Create(NiPixelData*, name, FormatPrefs*)` 0x00A5FF10 (cdecl): NiNew(0x48), copies prefs to +0x18, refs the pixel data into +0x3C, name optional (goes through the global string table); if the preload global is set it creates renderer data at once and, on failure, deletes itself and returns null. FormatPrefs = {pixelLayout (2 = TrueColor32), alpha (2 = Smooth), mipmaps (0 = off)} (JIP enums). **high**
- Plugin recipe (M3.2): two shapes under one node, as SA does: shape 1 = every triangle, vertex colours, no texture; shape 2 = only the textured triangles (libsm64 writes uv (1,1) on untextured ones), white vertex colour, atlas in srcTexture, NiAlphaProperty 0x12ED (blend SRCALPHA/INVSRCALPHA, alpha test GREATER 0), pushed out along the normal by `fDecalOffset`. R/B order of RGBA32 still to be confirmed in game.

## 8. Scene sun light for Mario's tint (M3.5, 2026-10-02)
- Path: `*(ShadowSceneNode**)0x011F91C8` → `+0xE0 LightingData* pSunLight` → `+0xF8 NiLight* light`. NiLight: `+0x9D` u8 effect type (3 = directional), `+0xC8` NiColor ambient, `+0xD4` NiColor diffuse; NiDirectionalLight `+0xF0` direction. [JIP netimmerse.h; JIP's minimap swaps `pSunLight->light` and restores `TES::directionalLight` (TES 0x011DEA10 +0x1C), so normally they are the same light]. **high (JIP)**; effect type checked at runtime.
- Xbox (read-only): `Sky::GetLight` = `Sun::GetSunLight(sky->sun)`; `ShadowSceneNode::SetSunLight` is only called from `Main::InitSky`, so the scene keeps one sun light object whose colours the sky (and, presumably, interior cell lighting) update. **Interior behaviour unverified**: logged in game (M3.5 test).
- libsm64 already bakes SM64's own directional shading into the vertex colours, so the plugin applies the game light as a colour tint (ambient + w × diffuse), not a second Lambert term. Local point lights are ignored.
