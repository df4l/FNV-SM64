#include "mario_render.h"
#include "game_addrs.h"
#include "config.h"
#include "sm64_loader.h"
#include "units.h"
#include "nvse/GameAPI.h"
#include "nvse/Utilities.h"

#include <cmath>
#include <cstring>

namespace
{
	using namespace GameAddr;

	constexpr uint32_t kMaxTris = SM64_GEO_MAX_TRIANGLES;
	constexpr uint32_t kMaxVerts = 3 * kMaxTris;
	// Blend src alpha / inv src alpha, alpha test GREATER 0 so fully clear texels write no depth (JIP AlphaFlag).
	constexpr uint16_t kOverlayAlphaFlags = 0x12ED;

	struct Vec3 { float x, y, z; };
	struct UV { float u, v; };
	struct ColorA { float r, g, b, a; };
	struct Triangle { uint16_t v[3]; };
	struct UpdateData { uint8_t bytes[0x0C]; };	// NiUpdateData, all zero (JIP kNiUpdateData)
	struct FormatPrefs { UInt32 pixelLayout, alphaFormat, mipMaps; };	// JIP TextureFormatPrefs

	uint8_t* s_node = nullptr;		// our NiNode; we hold one reference
	uint8_t* s_shape = nullptr;		// colour NiTriShape (every triangle, vertex colours), owned by s_node
	uint8_t* s_data = nullptr;		// its NiTriShapeData
	uint8_t* s_texShape = nullptr;	// overlay NiTriShape (textured triangles only, atlas x white), owned by s_node
	uint8_t* s_texData = nullptr;	// its NiTriShapeData
	uint8_t* s_texture = nullptr;	// NiSourceTexture of the atlas, built once; we hold one reference for the session
	bool s_textureFailed = false;	// don't retry a texture that failed to build
	bool s_failed = false;			// a fault in game code: no render until the next spawn
	bool s_loggedFirstUpdate = false;
	bool s_loggedConsistency = false;
	uint32_t s_updates = 0;
	Vec3 s_tint{ 1.0f, 1.0f, 1.0f };	// colour multiplier of the last update
	Vec3 s_loggedTint{ -1.0f, -1.0f, -1.0f };
	Vec3 s_sunAmbient{}, s_sunDiffuse{};
	const char* s_lightStatus = "not read yet";
	uint32_t s_tintLogs = 0;
	uint32_t s_lastVerts = 0;		// vertices written by the previous update; the rest stay collapsed at the origin
	uint32_t s_lastTexVerts = 0;	// same for the overlay
	float s_boundRadius = 0.0f;

	template <typename T>
	T& Field(void* obj, uint32_t offset)
	{
		return *reinterpret_cast<T*>(static_cast<uint8_t*>(obj) + offset);
	}

	UInt32 VFunc(void* obj, uint32_t offset)
	{
		return *reinterpret_cast<UInt32*>(*static_cast<UInt32*>(obj) + offset);
	}

	uint8_t* NiNew(uint32_t size)
	{
		return CdeclCall<uint8_t*>(kNiNew, size);
	}

	void AddRef(void* object)
	{
		InterlockedIncrement(&Field<volatile LONG>(object, kRefCount));
	}

	// NiAVObject::AttachProperty as JIP's NiGeometry::AddProperty does it: refcount, then push to the list head.
	void AddProperty(uint8_t* object, uint8_t* property)
	{
		AddRef(property);
		void** node = CdeclCall<void**>(kDListNewNode);	// [0] next, [1] prev, [2] data
		node[2] = property;
		void*& head = Field<void*>(object, kAVPropertyList);
		void*& tail = Field<void*>(object, kAVPropertyList + 4);
		node[0] = head;
		if (head)
			static_cast<void**>(head)[1] = node;
		else
			tail = node;
		head = node;
		Field<UInt32>(object, kAVPropertyList + 8)++;
	}

	// The guarded functions hold no C++ objects so __try is allowed: a mistake in our use of game code is logged
	// instead of crashing the game.

	// NiPixelData(704x64, RGBA32, 1 mip) filled from libsm64's atlas, wrapped in an NiSourceTexture. *texture stays
	// null when the pixel layout is not what we expect (logged by the caller through *stride).
	bool CreateTextureGuarded(const uint8_t* atlas, bool swapRB, uint8_t** texture, UInt32* stride)
	{
		__try
		{
			uint8_t* pixelData = NiNew(kNiPixelDataSize);
			ThisStdCall<void*>(kNiPixelDataCtor, pixelData, (UInt32)SM64_TEXTURE_WIDTH, (UInt32)SM64_TEXTURE_HEIGHT,
				(void*)kNiPixelFormatRGBA32, (UInt32)1, (UInt32)1);
			*stride = Field<UInt32>(pixelData, kPixelDataStride);
			uint8_t* pixels = Field<uint8_t*>(pixelData, kPixelDataPixels);
			if (*stride != 4 || !pixels)
			{
				ThisStdCall<void>(VFunc(pixelData, kVtblDeleteThis), pixelData);
				return true;
			}
			for (uint32_t i = 0; i < SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT; i++)
			{
				const uint8_t* s = atlas + i * 4;
				uint8_t* d = pixels + i * 4;
				d[0] = s[swapRB ? 2 : 0];
				d[1] = s[1];
				d[2] = s[swapRB ? 0 : 2];
				d[3] = s[3];
			}
			// True colour 32, smooth alpha, no mipmaps (they would bleed between the atlas tiles).
			FormatPrefs prefs{ 2, 2, 0 };
			uint8_t* created = CdeclCall<uint8_t*>(kNiSourceTextureCreate, pixelData, (const char*)nullptr, &prefs);
			if (created)
				AddRef(created);
			*texture = created;
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	// One unlit NiTriShape of kMaxTris degenerate triangles (libsm64's flat list, SA uses i*3+0,1,2). With a texture
	// it gets UVs, the texture in its shader property and an alpha-blend property.
	uint8_t* CreateShape(bool flipWinding, uint8_t* texture, uint8_t** dataOut)
	{
		// The arrays belong to the data object, whose dtor frees them with FormHeap_Free.
		Vec3* vertices = static_cast<Vec3*>(FormHeap_Allocate(sizeof(Vec3) * kMaxVerts));
		Vec3* normals = static_cast<Vec3*>(FormHeap_Allocate(sizeof(Vec3) * kMaxVerts));
		ColorA* colors = static_cast<ColorA*>(FormHeap_Allocate(sizeof(ColorA) * kMaxVerts));
		UV* uvs = texture ? static_cast<UV*>(FormHeap_Allocate(sizeof(UV) * kMaxVerts)) : nullptr;
		Triangle* triangles = static_cast<Triangle*>(FormHeap_Allocate(sizeof(Triangle) * kMaxTris));
		memset(vertices, 0, sizeof(Vec3) * kMaxVerts);
		for (uint32_t i = 0; i < kMaxVerts; i++)
		{
			normals[i] = { 0.0f, 0.0f, 1.0f };
			colors[i] = { 1.0f, 1.0f, 1.0f, 1.0f };
			if (uvs)
				uvs[i] = { 0.0f, 0.0f };
		}
		for (uint32_t i = 0; i < kMaxTris; i++)
		{
			const uint16_t b = (uint16_t)(i * 3);
			triangles[i] = flipWinding ? Triangle{ { b, (uint16_t)(b + 2), (uint16_t)(b + 1) } }
				: Triangle{ { b, (uint16_t)(b + 1), (uint16_t)(b + 2) } };
		}

		uint8_t* data = NiNew(kNiTriShapeDataSize);
		ThisStdCall<void*>(kNiTriShapeDataCtor, data, (UInt32)kMaxVerts, vertices, normals, colors, uvs,
			(UInt32)(uvs ? 1 : 0), (UInt32)0, (UInt32)kMaxTris, triangles);

		uint8_t* shape = NiNew(kNiTriShapeSize);
		ThisStdCall<void*>(kNiTriShapeCtor, shape, data);

		// Unlit: texture x vertex colour; with no texture, the vertex colour alone (JIP's debug shapes).
		uint8_t* shader = NiNew(kBSShaderNoLightingPropertySize);
		ThisStdCall<void*>(kBSShaderNoLightingPropertyCtor, shader);
		if (texture)
		{
			AddRef(texture);
			Field<uint8_t*>(shader, kNoLightingTexture) = texture;
		}
		AddProperty(shape, shader);
		if (texture)
		{
			uint8_t* alpha = CdeclCall<uint8_t*>(kNiAlphaPropertyCreate);
			Field<uint16_t>(alpha, kAlphaFlags) = kOverlayAlphaFlags;
			Field<uint8_t>(alpha, kAlphaThreshold) = 0;
			AddProperty(shape, alpha);
		}
		// JIP AssignGeometryProps: property state, then the shader's per-geometry setup.
		ThisStdCall<void>(kNiAVObjectUpdateProperties, shape);
		CdeclCall<void>(kBSShaderManagerInitGeometry, shape, 0, 0, 0);

		*dataOut = data;
		return shape;
	}

	bool CreateGuarded(uint8_t* parent, bool flipWinding, uint8_t* texture)
	{
		__try
		{
			uint8_t* data = nullptr;
			uint8_t* shape = CreateShape(flipWinding, nullptr, &data);
			uint8_t* texData = nullptr;
			uint8_t* texShape = texture ? CreateShape(flipWinding, texture, &texData) : nullptr;

			uint8_t* node = NiNew(kNiNodeSize);
			ThisStdCall<void*>(kNiNodeCtor, node, (UInt32)2);
			AddRef(node);
			ThisStdCall<void>(VFunc(node, kVtblNiNodeAddObject), node, shape, (UInt32)1);
			if (texShape)
				ThisStdCall<void>(VFunc(node, kVtblNiNodeAddObject), node, texShape, (UInt32)1);
			// Appended (firstFree = false): the game fills the ShadowSceneNode's first slots by index.
			ThisStdCall<void>(VFunc(parent, kVtblNiNodeAddObject), parent, node, (UInt32)0);

			UpdateData update{};
			ThisStdCall<void>(kNiNodeUpdateDownwardPass, node, &update, (UInt32)0);

			s_node = node;
			s_shape = shape;
			s_data = data;
			s_texShape = texShape;
			s_texData = texData;
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	enum class SunRead { Ok, NoLight, NotDirectional };

	// The scene's sun light (exterior sky, or the interior's directional light). Raw reads, guarded.
	bool ReadSunGuarded(SunRead* result, Vec3* ambient, Vec3* diffuse)
	{
		__try
		{
			*result = SunRead::NoLight;
			uint8_t* ssn = *reinterpret_cast<uint8_t**>(kWorldShadowSceneNodePtr);
			uint8_t* lightingData = ssn ? Field<uint8_t*>(ssn, kSSNSunLight) : nullptr;
			uint8_t* light = lightingData ? Field<uint8_t*>(lightingData, kLightingDataLight) : nullptr;
			if (!light)
				return true;
			if (Field<uint8_t>(light, kEffectType) != kEffectDirectional)
			{
				*result = SunRead::NotDirectional;
				return true;
			}
			*ambient = Field<Vec3>(light, kLightAmbient);
			*diffuse = Field<Vec3>(light, kLightDiffuse);
			*result = SunRead::Ok;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	float Clamp01(float v)
	{
		return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
	}

	// libsm64 already shades the vertex colours with SM64's own light, so the game light only tints them (as SA
	// tints by the ambient colours): ambient + weighted sun diffuse.
	Vec3 ComputeTint()
	{
		if (!g_config.lighting)
		{
			s_lightStatus = "off (ini)";
			return { 1.0f, 1.0f, 1.0f };
		}
		SunRead result;
		Vec3 ambient{}, diffuse{};
		if (!ReadSunGuarded(&result, &ambient, &diffuse))
		{
			s_lightStatus = "read faulted";
			return { 1.0f, 1.0f, 1.0f };
		}
		if (result != SunRead::Ok)
		{
			s_lightStatus = result == SunRead::NoLight ? "no sun light" : "sun light not directional";
			return { 1.0f, 1.0f, 1.0f };
		}
		s_lightStatus = "sun";
		s_sunAmbient = ambient;
		s_sunDiffuse = diffuse;
		const float w = g_config.lightDiffuse, b = g_config.lightBrightness, m = g_config.lightMin;
		return { fmaxf(m, Clamp01((ambient.x + diffuse.x * w) * b)), fmaxf(m, Clamp01((ambient.y + diffuse.y * w) * b)),
			fmaxf(m, Clamp01((ambient.z + diffuse.z * w) * b)) };
	}

	void LogTintChange()
	{
		const float d = fmaxf(fabsf(s_tint.x - s_loggedTint.x), fmaxf(fabsf(s_tint.y - s_loggedTint.y), fabsf(s_tint.z - s_loggedTint.z)));
		if (d < 0.1f || s_tintLogs >= 50)
			return;
		s_loggedTint = s_tint;
		_MESSAGE("render: tint (%.2f, %.2f, %.2f) from %s, ambient (%.2f, %.2f, %.2f) diffuse (%.2f, %.2f, %.2f)%s", s_tint.x,
			s_tint.y, s_tint.z, s_lightStatus, s_sunAmbient.x, s_sunAmbient.y, s_sunAmbient.z, s_sunDiffuse.x, s_sunDiffuse.y,
			s_sunDiffuse.z, ++s_tintLogs == 50 ? " (no more tint logs)" : "");
	}

	// The renderer only repacks non-static data; a static buffer would freeze the first pose.
	void MarkChanged(uint8_t* data, const Vec3& centre, float radius)
	{
		Field<Vec3>(data, kGDBound) = centre;
		Field<float>(data, kGDBound + 12) = radius;
		uint16_t& flags = Field<uint16_t>(data, kGDDirtyFlags);
		if (flags & kGDConsistencyMask)
		{
			if (!s_loggedConsistency)
				_WARNING("render: geometry consistency became 0x%04X, forcing mutable", flags & kGDConsistencyMask);
			s_loggedConsistency = true;
			flags &= ~kGDConsistencyMask;
		}
		flags |= kGDDirtyAll;
	}

	bool UpdateGuarded(const SM64MarioGeometryBuffers* geometry, const float marioPos[3], float decalOffset, Vec3 tint)
	{
		__try
		{
			Vec3* vertices = Field<Vec3*>(s_data, kGDVertices);
			Vec3* normals = Field<Vec3*>(s_data, kGDNormals);
			ColorA* colors = Field<ColorA*>(s_data, kGDColors);
			const uint32_t tris = geometry->numTrianglesUsed < kMaxTris ? geometry->numTrianglesUsed : kMaxTris;
			const uint32_t verts = tris * 3;
			const float k = Units::kFnvPerSm64;

			// Same axis swap as Units::Sm64ToFnv, relative to Mario so the node carries the world position.
			Vec3 lo{ 0, 0, 0 }, hi{ 0, 0, 0 };
			for (uint32_t i = 0; i < verts; i++)
			{
				const float* p = geometry->position + i * 3;
				const float* n = geometry->normal + i * 3;
				const float* c = geometry->color + i * 3;
				const Vec3 v{ (p[0] - marioPos[0]) * k, -(p[2] - marioPos[2]) * k, (p[1] - marioPos[1]) * k };
				vertices[i] = v;
				normals[i] = { n[0], -n[2], n[1] };
				colors[i] = { c[0] * tint.x, c[1] * tint.y, c[2] * tint.z, 1.0f };
				if (i == 0)
					lo = hi = v;
				lo = { fminf(lo.x, v.x), fminf(lo.y, v.y), fminf(lo.z, v.z) };
				hi = { fmaxf(hi.x, v.x), fmaxf(hi.y, v.y), fmaxf(hi.z, v.z) };
			}
			for (uint32_t i = verts; i < s_lastVerts; i++)
				vertices[i] = { 0, 0, 0 };
			s_lastVerts = verts;

			const Vec3 centre{ (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
			float radiusSq = 0.0f;
			for (uint32_t i = 0; i < verts; i++)
			{
				const float dx = vertices[i].x - centre.x, dy = vertices[i].y - centre.y, dz = vertices[i].z - centre.z;
				radiusSq = fmaxf(radiusSq, dx * dx + dy * dy + dz * dz);
			}
			// The overlay is pushed out by decalOffset, so the shared bound covers it too.
			s_boundRadius = sqrtf(radiusSq) + fabsf(decalOffset);
			MarkChanged(s_data, centre, s_boundRadius);

			if (s_texData)
			{
				// libsm64 writes uv (1, 1) on all three vertices of an untextured triangle (gfx_adapter.c). Textured
				// triangles are drawn a second time, white x atlas, blended over the colour shape (as SA does).
				Vec3* texVertices = Field<Vec3*>(s_texData, kGDVertices);
				Vec3* texNormals = Field<Vec3*>(s_texData, kGDNormals);
				ColorA* texColors = Field<ColorA*>(s_texData, kGDColors);
				UV* texUVs = Field<UV*>(s_texData, kGDUVs);
				uint32_t texVerts = 0;
				for (uint32_t t = 0; t < tris; t++)
				{
					const float* uv = geometry->uv + t * 6;
					bool textured = false;
					for (uint32_t j = 0; j < 6; j++)
						textured |= uv[j] != 1.0f;
					if (!textured)
						continue;
					for (uint32_t j = 0; j < 3; j++)
					{
						const uint32_t i = t * 3 + j;
						const Vec3& v = vertices[i];
						const Vec3& n = normals[i];
						texVertices[texVerts] = { v.x + n.x * decalOffset, v.y + n.y * decalOffset, v.z + n.z * decalOffset };
						texNormals[texVerts] = n;
						texColors[texVerts] = { tint.x, tint.y, tint.z, 1.0f };
						texUVs[texVerts] = { uv[j * 2], uv[j * 2 + 1] };
						texVerts++;
					}
				}
				for (uint32_t i = texVerts; i < s_lastTexVerts; i++)
					texVertices[i] = { 0, 0, 0 };
				s_lastTexVerts = texVerts;
				MarkChanged(s_texData, centre, s_boundRadius);
			}

			Vec3& translate = Field<Vec3>(s_node, kAVLocalTranslate);
			Units::Sm64ToFnv(marioPos, translate.x, translate.y, translate.z);
			UpdateData update{};
			ThisStdCall<void>(kNiNodeUpdateDownwardPass, s_node, &update, (UInt32)0);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	bool DestroyGuarded()
	{
		__try
		{
			uint8_t* parent = Field<uint8_t*>(s_node, kAVParent);
			if (parent)
				ThisStdCall<void>(VFunc(parent, kVtblNiNodeRemoveObject), parent, s_node);
			if (InterlockedDecrement(&Field<volatile LONG>(s_node, kRefCount)) == 0)
				ThisStdCall<void>(VFunc(s_node, kVtblDeleteThis), s_node);
			return true;
		}
		__except (SM64_RecordFault(GetExceptionInformation()))
		{
			return false;
		}
	}

	void Forget()
	{
		s_node = s_shape = s_data = s_texShape = s_texData = nullptr;
		s_lastVerts = s_lastTexVerts = 0;
	}

	void Fail(const char* where)
	{
		SM64_LogFault(where);
		_ERROR("render: %s faulted, Mario's mesh is off until the next spawn (objects leaked, not touched again)", where);
		Console_Print("SM64FNV: Mario render crashed (see log)");
		Forget();
		s_failed = true;
	}

	// Builds the atlas texture on first use. Returns null (overlay off) if it is disabled or can't be built.
	uint8_t* GetTexture()
	{
		if (s_texture || s_textureFailed || !g_config.renderTexture)
			return s_texture;
		const uint8_t* atlas = SM64_Texture();
		if (!atlas)
		{
			_ERROR("render: no texture atlas from libsm64, overlay off");
			s_textureFailed = true;
			return nullptr;
		}
		UInt32 stride = 0;
		if (!CreateTextureGuarded(atlas, g_config.textureSwapRB, &s_texture, &stride))
		{
			SM64_LogFault("texture create");
			_ERROR("render: texture create faulted, overlay off for this session");
			s_texture = nullptr;
			s_textureFailed = true;
			return nullptr;
		}
		if (!s_texture)
		{
			_ERROR("render: texture not created (pixel stride %u, expected 4; or renderer data failed), overlay off", stride);
			s_textureFailed = true;
			return nullptr;
		}
		_MESSAGE("render: texture created: NiSourceTexture %p (refs %u), %dx%d RGBA32, stride %u, R/B %s", s_texture,
			Field<UInt32>(s_texture, kRefCount), SM64_TEXTURE_WIDTH, SM64_TEXTURE_HEIGHT, stride,
			g_config.textureSwapRB ? "swapped" : "as libsm64");
		return s_texture;
	}
}

void MarioRender_Create()
{
	if (!g_config.render)
	{
		_MESSAGE("render: off (ini bRender=0)");
		return;
	}
	if (s_node)
		MarioRender_Destroy("recreate");
	s_failed = false;
	s_loggedFirstUpdate = false;
	s_loggedConsistency = false;
	s_updates = 0;
	s_loggedTint = { -1.0f, -1.0f, -1.0f };

	uint8_t* parent = *reinterpret_cast<uint8_t**>(kWorldShadowSceneNodePtr);
	if (!parent)
	{
		_ERROR("render: world ShadowSceneNode is null, no mesh");
		return;
	}
	uint8_t* texture = GetTexture();
	if (!CreateGuarded(parent, g_config.renderFlipWinding, texture))
	{
		Fail("mesh create");
		return;
	}
	const uint16_t flags = Field<uint16_t>(s_data, kGDDirtyFlags);
	_MESSAGE("render: mesh created under ShadowSceneNode %p: node %p (refs %u), shape %p, data %p, overlay %p (data %p), "
		"%u verts / %u tris each, dirty/consistency 0x%04X, winding %s", parent, s_node, Field<UInt32>(s_node, kRefCount),
		s_shape, s_data, s_texShape, s_texData, kMaxVerts, kMaxTris, flags, g_config.renderFlipWinding ? "flipped" : "as SA");
	if (!s_texShape)
		_MESSAGE("render: no textured overlay (%s)", !g_config.renderTexture ? "ini bTexture=0" : "texture unavailable");
}

void MarioRender_Update(const SM64MarioGeometryBuffers& geometry, const float marioPos[3])
{
	if (!s_node)
		return;
	s_tint = ComputeTint();
	if (!UpdateGuarded(&geometry, marioPos, g_config.decalOffset, s_tint))
	{
		Fail("mesh update");
		return;
	}
	s_updates++;
	LogTintChange();
	if (!s_loggedFirstUpdate)
	{
		s_loggedFirstUpdate = true;
		const Vec3& t = Field<Vec3>(s_node, kAVLocalTranslate);
		_MESSAGE("render: first mesh update: %u tris (%u textured), node at fnv=(%.1f, %.1f, %.1f), bound radius %.1f",
			geometry.numTrianglesUsed, s_lastTexVerts / 3, t.x, t.y, t.z, s_boundRadius);
	}
}

void MarioRender_Destroy(const char* reason)
{
	if (!s_node)
		return;
	if (!DestroyGuarded())
	{
		Fail("mesh destroy");
		return;
	}
	_MESSAGE("render: mesh destroyed (%s) after %u updates", reason, s_updates);
	Forget();
}

void MarioRender_Abandon()
{
	if (s_node)
		_MESSAGE("render: mesh left to process exit");
	Forget();
	s_texture = nullptr;
}

void MarioRender_DumpState()
{
	if (!s_node)
	{
		Console_Print("SM64FNV: render %s", !g_config.render ? "off (ini)" : s_failed ? "FAILED (see log)" : "no mesh");
		return;
	}
	const Vec3& t = Field<Vec3>(s_node, kAVLocalTranslate);
	const uint16_t flags = Field<uint16_t>(s_data, kGDDirtyFlags);
	Console_Print("SM64FNV: render mesh %u tris (%u textured%s), %u updates, at (%.0f, %.0f, %.0f), radius %.0f",
		s_lastVerts / 3, s_lastTexVerts / 3, s_texShape ? "" : ", overlay off", s_updates, t.x, t.y, t.z, s_boundRadius);
	_MESSAGE("state dump render: node %p parent %p refs %u, %u tris (%u textured, overlay %p, texture %p refs %u), "
		"%u updates, fnv=(%.1f, %.1f, %.1f), radius %.1f, dirty/consistency 0x%04X", s_node, Field<void*>(s_node, kAVParent),
		Field<UInt32>(s_node, kRefCount), s_lastVerts / 3, s_lastTexVerts / 3, s_texShape, s_texture,
		s_texture ? Field<UInt32>(s_texture, kRefCount) : 0, s_updates, t.x, t.y, t.z, s_boundRadius, flags);
	Console_Print("SM64FNV: light %s, tint (%.2f, %.2f, %.2f), ambient (%.2f, %.2f, %.2f), diffuse (%.2f, %.2f, %.2f)",
		s_lightStatus, s_tint.x, s_tint.y, s_tint.z, s_sunAmbient.x, s_sunAmbient.y, s_sunAmbient.z, s_sunDiffuse.x,
		s_sunDiffuse.y, s_sunDiffuse.z);
	_MESSAGE("state dump light: %s, tint (%.3f, %.3f, %.3f), ambient (%.3f, %.3f, %.3f), diffuse (%.3f, %.3f, %.3f)",
		s_lightStatus, s_tint.x, s_tint.y, s_tint.z, s_sunAmbient.x, s_sunAmbient.y, s_sunAmbient.z, s_sunDiffuse.x,
		s_sunDiffuse.y, s_sunDiffuse.z);
}
