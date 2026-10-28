#include "collision.h"
#include "config.h"
#include "game_addrs.h"
#include "units.h"
#include "nvse/GameAPI.h"
#include "nvse/GameObjects.h"
#include "nvse/GameForms.h"
#include "decomp/include/surface_terrains.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>

// M5.2: fixed Havok bodies of the loaded refs (collision.md §2-§4) become static SM64 triangles.
namespace
{
	using namespace GameAddr;

	constexpr uint32_t kMaxGridCells = 256;
	constexpr int kMaxNodeDepth = 48;
	constexpr int kMaxShapeDepth = 16;
	constexpr int kMaxUnknownVtables = 8;
	constexpr int kMaxSubParts = 256;
	constexpr int kLoggedBodies = 5;
	constexpr uint32_t kLoggedSkipped = 16;
	constexpr uint32_t kLoggedDoors = 16;
	constexpr float kSkippedLogRadius = 768.0f;
	// Diagnostic dump around Mario (M5.4c): walls this close to his body are listed, at most kDumpLines of each kind.
	constexpr float kDumpWallRadius = 96.0f;
	constexpr int kDumpLines = 24;
	// Ceiling-facing triangles this far below / above Mario's feet are listed as low ceilings (Mario is ~95 units tall).
	constexpr float kLowCeilingBelow = 30.0f, kLowCeilingAbove = 100.0f;
	constexpr int kLayers = 64;
	// Added to a ref's render bound before culling it: Havok shapes can stick out a little past the visible mesh.
	constexpr float kCullMargin = 512.0f;
	// Below this, a triangle is too thin for SM64's normal (game units^2).
	constexpr float kMinDoubleArea = 0.01f;
	// Mario's body for the diagnostics: about his waist, above any floor he stands on (game units).
	constexpr float kViewHeight = 40.0f;

	// Havok hkTransform: world = t + x * c[0] + y * c[1] + z * c[2] (column-major, hkVector4 padded).
	struct Xf
	{
		float c[3][3];
		float t[3];
	};

	struct UnknownShape
	{
		uintptr_t vtable;
		uint32_t count;
	};

	struct Stats
	{
		uint32_t cells, refs, refsWith3D, refsCulled, refsFaulted;
		uint32_t bodiesFixed, bodiesSkippedMotion, bodiesNotInWorld, doorBodies, bodiesMoving, movingDoors, movingDynamic;
		uint32_t packed, boxes, mopps, lists, transforms, convexTransforms;
		uint32_t trianglesIn, triangles, clipped, degenerate, badIndex;
		uint32_t hulls, hullsSkipped;
		// Connected pieces of packed meshes: closed (faced out by volume), open (faced up), room (floors up, ceilings down),
		// vertical (stored winding)
		uint32_t piecesClosed, piecesClosedFlipped, piecesOpen, piecesOpenFlipped, piecesRoom, piecesRoomFlipped, piecesRoomShell,
			piecesVertical;
		bool capped;
		UnknownShape unknown[kMaxUnknownVtables];
		uint32_t unknownOther;
		uint32_t terrains[7];
		uint32_t layerKept[kLayers], layerSkipped[kLayers];
		double buildMs;
	};

	// Where each kept static triangle came from (M5.4c diagnostics): world vertices in emitted order, game units.
	enum : uint8_t { kShapePacked, kShapeBox, kShapeHull };
	enum : uint8_t { kClosed, kOpen, kVertical, kRoom, kNoPiece };
	struct TriMeta
	{
		float w[3][3];
		uint32_t refID, baseID;
		uint8_t shape, piece, flipped, layer, baseType;
	};

	// Everything the guarded walk needs, plain data so it can live under __try.
	struct Context
	{
		TriMeta* meta;
		TriMeta cur;		// ref, shape, piece and flip of the triangles being emitted
		uint32_t skippedLogged;
		SM64Surface* out;
		uint32_t capacity;
		float hk2bs;
		float minX, maxX, minY, maxY;	// clip square, game units
		bool flip;
		bool interior;
		float view[3];		// Mario's body (feet + kViewHeight), game units, for the skipped-body log
		Stats* st;
		uint32_t loggedBodies;
		uint32_t loggedDoors;
		bool verbose;
		// M8.1 moving body: vertices go out relative to `pose` (its body frame), unclipped and without meta.
		bool local;
		Xf pose;
	};

	// Havok layers (JIP CollisionLayerTypes) that Mario must not stand on: water (M10 handles it), triggers, trap
	// zones, non-collidable, portals, acoustic/actor/projectile zones, and the picking/character layers (>= 29).
	bool SkipLayer(uint8_t layer)
	{
		switch (layer)
		{
		case 11: case 12: case 14: case 15: case 16: case 18: case 21: case 22: case 23: case 24:
			return true;
		default:
			return layer >= 29;
		}
	}

	// Havok motion types 1-3 (dynamic, sphere/box inertia) and 6 (thin box): simulated loose objects.
	bool IsDynamicMotion(uint8_t motion)
	{
		return (motion >= 1 && motion <= 3) || motion == 6;
	}

	// M8.2: solid layers a loose object Mario can stand on uses (static, anim static, clutter, props, large debris);
	// weapons, projectiles, small debris and shell casings stay out.
	bool DynamicLayer(uint8_t layer)
	{
		return layer == 1 || layer == 2 || layer == 4 || layer == 10 || layer == 20;
	}

	Stats s_stats{};
	bool s_built = false;

	template <class T> T At(const void* p, uint32_t offset)
	{
		return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(p) + offset);
	}

	uintptr_t Vtable(const void* p)
	{
		return *reinterpret_cast<const uintptr_t*>(p);
	}

	void ReadXf(const void* p, uint32_t offset, Xf* x)
	{
		const float* f = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(p) + offset);
		for (int col = 0; col < 3; col++)
			for (int k = 0; k < 3; k++)
				x->c[col][k] = f[col * 4 + k];
		for (int k = 0; k < 3; k++)
			x->t[k] = f[12 + k];
	}

	void Apply(const Xf& x, const float v[3], float out[3])
	{
		for (int k = 0; k < 3; k++)
			out[k] = x.t[k] + v[0] * x.c[0][k] + v[1] * x.c[1][k] + v[2] * x.c[2][k];
	}

	// parent * child
	Xf Compose(const Xf& parent, const Xf& child)
	{
		Xf r;
		for (int col = 0; col < 3; col++)
			for (int k = 0; k < 3; k++)
				r.c[col][k] = child.c[col][0] * parent.c[0][k] + child.c[col][1] * parent.c[1][k] + child.c[col][2] * parent.c[2][k];
		Apply(parent, child.t, r.t);
		return r;
	}

	// MATERIAL_TYPE -> SM64 terrain (collision.md §6, D10).
	int16_t TerrainFor(uint32_t material)
	{
		switch (material)
		{
		case 18: return TERRAIN_SAND;
		case 1: case 2: case 4: case 6: case 34: return TERRAIN_GRASS;
		case 9: case 12: return TERRAIN_SPOOKY;
		case 8: return TERRAIN_WATER;
		default: return TERRAIN_STONE;
		}
	}

	void CountUnknown(Stats* st, uintptr_t vtable)
	{
		for (UnknownShape& u : st->unknown)
		{
			if (u.vtable == vtable || !u.vtable)
			{
				u.vtable = vtable;
				u.count++;
				return;
			}
		}
		st->unknownOther++;
	}

	// One triangle, Havok local coordinates, emitted in the given order (callers orient it; [Collision] bStaticFlipWinding
	// reverses everything as a debug switch).
	void Emit(Context* ctx, const Xf& xf, const float* a, const float* b, const float* c, int16_t terrain)
	{
		ctx->st->trianglesIn++;
		float w[3][3];
		const float* local[3] = { a, b, c };
		for (int i = 0; i < 3; i++)
		{
			Apply(xf, local[i], w[i]);
			for (int k = 0; k < 3; k++)
				w[i][k] *= ctx->hk2bs;
		}
		const float minX = fminf(w[0][0], fminf(w[1][0], w[2][0])), maxX = fmaxf(w[0][0], fmaxf(w[1][0], w[2][0]));
		const float minY = fminf(w[0][1], fminf(w[1][1], w[2][1])), maxY = fmaxf(w[0][1], fmaxf(w[1][1], w[2][1]));
		if (!ctx->local && (maxX < ctx->minX || minX > ctx->maxX || maxY < ctx->minY || minY > ctx->maxY))
		{
			ctx->st->clipped++;
			return;
		}
		const float e1[3] = { w[1][0] - w[0][0], w[1][1] - w[0][1], w[1][2] - w[0][2] };
		const float e2[3] = { w[2][0] - w[0][0], w[2][1] - w[0][1], w[2][2] - w[0][2] };
		const float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < kMinDoubleArea * kMinDoubleArea)
		{
			ctx->st->degenerate++;
			return;
		}
		if (ctx->st->triangles >= ctx->capacity)
		{
			ctx->st->capped = true;
			return;
		}
		SM64Surface& s = ctx->out[ctx->st->triangles];
		s.type = SURFACE_DEFAULT;
		s.force = 0;
		s.terrain = terrain;
		const int order[3] = { 0, ctx->flip ? 2 : 1, ctx->flip ? 1 : 2 };
		for (int i = 0; i < 3; i++)
		{
			float p[3] = { w[order[i]][0], w[order[i]][1], w[order[i]][2] };
			if (ctx->local)
			{
				// Body frame: R^T (w - t), game units.
				const float d[3] = { p[0] - ctx->pose.t[0] * ctx->hk2bs, p[1] - ctx->pose.t[1] * ctx->hk2bs, p[2] - ctx->pose.t[2] * ctx->hk2bs };
				for (int k = 0; k < 3; k++)
					p[k] = ctx->pose.c[k][0] * d[0] + ctx->pose.c[k][1] * d[1] + ctx->pose.c[k][2] * d[2];
			}
			float sm[3];
			Units::FnvToSm64(p[0], p[1], p[2], sm);
			for (int k = 0; k < 3; k++)
				s.vertices[i][k] = (int32_t)lroundf(sm[k]);
		}
		// libsm64 rejects (and logs) a triangle whose integer vertices give a zero normal: drop it here instead.
		const double r1[3] = { (double)s.vertices[1][0] - s.vertices[0][0], (double)s.vertices[1][1] - s.vertices[0][1],
			(double)s.vertices[1][2] - s.vertices[0][2] };
		const double r2[3] = { (double)s.vertices[2][0] - s.vertices[0][0], (double)s.vertices[2][1] - s.vertices[0][1],
			(double)s.vertices[2][2] - s.vertices[0][2] };
		if (r1[1] * r2[2] - r1[2] * r2[1] == 0.0 && r1[2] * r2[0] - r1[0] * r2[2] == 0.0 && r1[0] * r2[1] - r1[1] * r2[0] == 0.0)
		{
			ctx->st->degenerate++;
			return;
		}
		if (ctx->meta)
		{
			TriMeta& m = ctx->meta[ctx->st->triangles];
			m = ctx->cur;
			for (int i = 0; i < 3; i++)
				memcpy(m.w[i], w[order[i]], sizeof(m.w[i]));
		}
		ctx->st->triangles++;
	}

	// Hulls and boxes are built outward here; undo the ini flip that is meant for stored meshes.
	void EmitOutward(Context* ctx, const Xf& xf, const float* a, const float* b, const float* c, int16_t terrain)
	{
		Emit(ctx, xf, a, ctx->flip ? c : b, ctx->flip ? b : c, terrain);
	}

	// Scratch for one packed mesh, reused across meshes so nothing under the SEH guard needs unwinding.
	struct MeshTri
	{
		uint32_t v[3];		// welded vertex indices
		uint16_t first;		// stored index of v0, for the sub-part lookup
	};
	struct EdgeRef
	{
		uint64_t key;		// (low index << 32) | high index
		uint32_t tri;
		bool forward;		// the triangle runs this edge low -> high
	};
	std::vector<float> s_meshVerts;
	std::vector<uint32_t> s_weld, s_sorted;
	std::vector<MeshTri> s_meshTris;
	std::vector<EdgeRef> s_edges;
	std::vector<uint32_t> s_parent;
	std::vector<uint8_t> s_parity;		// orientation relative to the parent: 1 = reversed
	constexpr size_t kComp = 7;
	// A non-closed, non-open piece with at least this share of horizontal area (|normal z| weighted) is a room piece.
	constexpr double kRoomHorizontal = 0.2;
	// Interiors only: a closed piece at least this tall and wide (game units) is a room shell, not a solid block.
	constexpr float kRoomMinHeight = 160.0f, kRoomMinWidth = 128.0f;
	std::vector<double> s_component;	// per root: signed volume, sum |cross|, cross sum x/y/z, sum up x height, sum |up|
	std::vector<float> s_rootBox;		// per root: world min x/y/z, max x/y/z (Havok units)
	std::vector<uint8_t> s_rootKind;	// per root: kClosed / kOpen / kVertical / kRoom
	std::vector<uint8_t> s_rootFlip;
	std::vector<TriMeta> s_meta;		// per kept triangle of the last build
	std::vector<MovingBody> s_moving;	// keyframed bodies found by the last build (M8.1)

	uint32_t Find(uint32_t x, uint8_t* parity)
	{
		uint32_t root = x;
		uint8_t p = 0;
		while (s_parent[root] != root)
		{
			p ^= s_parity[root];
			root = s_parent[root];
		}
		uint32_t cur = x;
		uint8_t curParity = p;
		while (s_parent[cur] != root)
		{
			const uint32_t next = s_parent[cur];
			const uint8_t nextParity = curParity ^ s_parity[cur];
			s_parent[cur] = root;
			s_parity[cur] = curParity;
			cur = next;
			curParity = nextParity;
		}
		*parity = p;
		return root;
	}

	// rel = 1 when a and b must have opposite orientation flags. A conflict (non-manifold or non-orientable edge) is
	// ignored: the first link wins.
	void Union(uint32_t a, uint32_t b, uint8_t rel)
	{
		uint8_t pa, pb;
		const uint32_t ra = Find(a, &pa), rb = Find(b, &pb);
		if (ra == rb)
			return;
		s_parent[rb] = ra;
		s_parity[rb] = pa ^ pb ^ rel;
	}

	void Cross(const float* a, const float* b, const float* c, double n[3])
	{
		const double e1[3] = { (double)b[0] - a[0], (double)b[1] - a[1], (double)b[2] - a[2] };
		const double e2[3] = { (double)c[0] - a[0], (double)c[1] - a[1], (double)c[2] - a[2] };
		n[0] = e1[1] * e2[2] - e1[2] * e2[1];
		n[1] = e1[2] * e2[0] - e1[0] * e2[2];
		n[2] = e1[0] * e2[1] - e1[1] * e2[0];
	}

	// Havok meshes are two-sided and Bethesda's winding is arbitrary (M5.2 retest: 567 of 1711 meshes inward), but SM64
	// uses the winding: a reversed wall pulls Mario into the object and a reversed floor is a ceiling. So triangles
	// sharing an edge are made consistent (welded vertices, union-find with parity), then each connected piece faces
	// out by its signed volume if closed, or up (world Z) if open; a vertical open piece keeps the stored winding.
	void EmitPacked(Context* ctx, const void* shape, const Xf& xf)
	{
		ctx->st->packed++;
		const void* data = At<const void*>(shape, kPackedShapeData);
		if (!data)
			return;
		const uint32_t triCount = At<uint32_t>(data, 0x08);
		const uint32_t vertCount = At<uint32_t>(data, 0x0C);
		const uint16_t partCount = At<uint16_t>(data, 0x10);
		const uint16_t* tris = At<const uint16_t*>(data, 0x14);
		const float* verts = At<const float*>(data, 0x18);
		const uint32_t* parts = At<const uint32_t*>(data, 0x24);
		if (!tris || !verts || !triCount || vertCount > 65536)
			return;

		// Sub-part k owns vertices [start_k, start_k + numVerts_k) (getNextKey logic, collision.md §4).
		uint32_t partEnd[kMaxSubParts];
		int16_t partTerrain[kMaxSubParts];
		const int partsUsed = parts ? (partCount < kMaxSubParts ? partCount : kMaxSubParts) : 0;
		uint32_t sum = 0;
		for (int k = 0; k < partsUsed; k++)
		{
			sum += parts[k * 3 + 1];
			partEnd[k] = sum;
			partTerrain[k] = TerrainFor(parts[k * 3 + 2]);
		}

		const float* sc = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(shape) + kPackedShapeScale);
		const bool scaled = sc[0] != 1.0f;
		s_meshVerts.resize(vertCount * 3);
		float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
		for (uint32_t i = 0; i < vertCount; i++)
			for (int k = 0; k < 3; k++)
			{
				const float v = verts[i * 3 + k] * (scaled ? sc[k] : 1.0f);
				s_meshVerts[i * 3 + k] = v;
				lo[k] = fminf(lo[k], v);
				hi[k] = fmaxf(hi[k], v);
			}
		const float ref[3] = { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };

		// Weld identical positions: strips often repeat a vertex under another index.
		s_sorted.resize(vertCount);
		s_weld.resize(vertCount);
		for (uint32_t i = 0; i < vertCount; i++)
			s_sorted[i] = i;
		const float* mv = s_meshVerts.data();
		std::sort(s_sorted.begin(), s_sorted.end(), [mv](uint32_t a, uint32_t b) {
			return memcmp(mv + a * 3, mv + b * 3, 12) < 0;
		});
		for (uint32_t i = 0; i < vertCount; i++)
			s_weld[s_sorted[i]] = i > 0 && memcmp(mv + s_sorted[i] * 3, mv + s_sorted[i - 1] * 3, 12) == 0
				? s_weld[s_sorted[i - 1]] : s_sorted[i];

		s_meshTris.clear();
		s_edges.clear();
		for (uint32_t i = 0; i < triCount; i++)
		{
			const uint16_t* t = tris + i * 4;
			if (t[0] >= vertCount || t[1] >= vertCount || t[2] >= vertCount)
			{
				ctx->st->badIndex++;
				continue;
			}
			const MeshTri m{ { s_weld[t[0]], s_weld[t[1]], s_weld[t[2]] }, t[0] };
			const uint32_t index = (uint32_t)s_meshTris.size();
			s_meshTris.push_back(m);
			for (int k = 0; k < 3; k++)
			{
				const uint32_t a = m.v[k], b = m.v[(k + 1) % 3];
				if (a == b)
					continue;
				const uint32_t low = a < b ? a : b, high = a < b ? b : a;
				s_edges.push_back({ ((uint64_t)low << 32) | high, index, a < b });
			}
		}
		const uint32_t n = (uint32_t)s_meshTris.size();
		s_parent.resize(n);
		s_parity.assign(n, 0);
		for (uint32_t i = 0; i < n; i++)
			s_parent[i] = i;
		std::sort(s_edges.begin(), s_edges.end(), [](const EdgeRef& a, const EdgeRef& b) { return a.key < b.key; });
		for (size_t i = 0; i < s_edges.size();)
		{
			size_t j = i + 1;
			for (; j < s_edges.size() && s_edges[j].key == s_edges[i].key; j++)
				Union(s_edges[i].tri, s_edges[j].tri, s_edges[i].forward == s_edges[j].forward ? 1 : 0);
			i = j;
		}

		s_component.assign((size_t)n * kComp, 0.0);
		s_rootBox.resize((size_t)n * 6);
		for (uint32_t r = 0; r < n; r++)
			for (int k = 0; k < 3; k++)
			{
				s_rootBox[(size_t)r * 6 + k] = 1e30f;
				s_rootBox[(size_t)r * 6 + 3 + k] = -1e30f;
			}
		for (uint32_t i = 0; i < n; i++)
		{
			uint8_t parity;
			const uint32_t root = Find(i, &parity);
			const float* a = mv + s_meshTris[i].v[0] * 3;
			const float* b = mv + s_meshTris[i].v[1] * 3;
			const float* c = mv + s_meshTris[i].v[2] * 3;
			double cr[3];
			Cross(a, b, c, cr);
			const double sign = parity ? -1.0 : 1.0;
			double* comp = &s_component[(size_t)root * kComp];
			comp[0] += sign * ((a[0] - ref[0]) * cr[0] + (a[1] - ref[1]) * cr[1] + (a[2] - ref[2]) * cr[2]);
			comp[1] += sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
			for (int k = 0; k < 3; k++)
				comp[2 + k] += sign * cr[k];
			// World up component of the normal against the triangle's height (relative to the mesh centre).
			const double upZ = cr[0] * xf.c[0][2] + cr[1] * xf.c[1][2] + cr[2] * xf.c[2][2];
			double dz = 0.0;
			for (int k = 0; k < 3; k++)
				dz += (((double)a[k] + b[k] + c[k]) / 3.0 - ref[k]) * xf.c[k][2];
			comp[5] += sign * upZ * dz;
			comp[6] += fabs(upZ);
			// World box of the piece (Havok units), for the interior room-shell size test.
			float* box = &s_rootBox[(size_t)root * 6];
			const float* corners[3] = { a, b, c };
			for (const float* v : corners)
			{
				float w[3];
				Apply(xf, v, w);
				for (int k = 0; k < 3; k++)
				{
					box[k] = fminf(box[k], w[k]);
					box[3 + k] = fmaxf(box[3 + k], w[k]);
				}
			}
		}

		s_rootFlip.assign(n, 0);
		s_rootKind.assign(n, kVertical);
		const double extent = sqrt((double)(hi[0] - lo[0]) * (hi[0] - lo[0]) + (double)(hi[1] - lo[1]) * (hi[1] - lo[1])
			+ (double)(hi[2] - lo[2]) * (hi[2] - lo[2]));
		for (uint32_t r = 0; r < n; r++)
		{
			if (s_parent[r] != r)
				continue;
			const double* comp = &s_component[(size_t)r * kComp];
			const double area = comp[1];
			const double net = sqrt(comp[2] * comp[2] + comp[3] * comp[3] + comp[4] * comp[4]);
			const double upWorld = comp[2] * xf.c[0][2] + comp[3] * xf.c[1][2] + comp[4] * xf.c[2][2];
			const bool roomShape = g_config.roomPieces && comp[6] > kRoomHorizontal * area && comp[5] != 0.0;
			const bool closed = net < 0.2 * area && fabs(comp[0]) > 1e-4 * area * extent;
			const float* box = &s_rootBox[(size_t)r * 6];
			// Interiors: a closed kit piece the size of a room (M7.1c dump: base 00120D6A, 256 tall) is a shell seen from
			// inside, not a solid block; wall slabs and pillars are thinner, furniture lower.
			const bool roomShell = closed && roomShape && ctx->interior && (box[5] - box[2]) * ctx->hk2bs >= kRoomMinHeight
				&& (box[3] - box[0]) * ctx->hk2bs >= kRoomMinWidth && (box[4] - box[1]) * ctx->hk2bs >= kRoomMinWidth;
			if (roomShell)
			{
				ctx->st->piecesRoomShell++;
				ctx->st->piecesRoom++;
				s_rootKind[r] = kRoom;
				s_rootFlip[r] = comp[5] > 0.0;
				ctx->st->piecesRoomFlipped += s_rootFlip[r];
			}
			else if (closed)
			{
				ctx->st->piecesClosed++;
				s_rootKind[r] = kClosed;
				s_rootFlip[r] = comp[0] < 0.0;
				ctx->st->piecesClosedFlipped += s_rootFlip[r];
			}
			else if (fabs(upWorld) > 0.3 * net)
			{
				ctx->st->piecesOpen++;
				s_rootKind[r] = kOpen;
				s_rootFlip[r] = upWorld < 0.0;
				ctx->st->piecesOpenFlipped += s_rootFlip[r];
			}
			else if (roomShape)
			{
				// Interior kit piece holding floor + walls + ceiling (net normal ~0, M5.4c/M7.1b dumps): the low
				// horizontal faces are floors and face up, the high ones ceilings and face down. Stored inside-out, its
				// floor is a ceiling at foot level, which SM64 treats as a wall (Mario stops at an invisible "box").
				ctx->st->piecesRoom++;
				s_rootKind[r] = kRoom;
				s_rootFlip[r] = comp[5] > 0.0;
				ctx->st->piecesRoomFlipped += s_rootFlip[r];
			}
			else
				ctx->st->piecesVertical++;
		}

		for (uint32_t i = 0; i < n; i++)
		{
			uint8_t parity;
			const uint32_t root = Find(i, &parity);
			const bool flip = (parity ^ s_rootFlip[root]) != 0;
			ctx->cur.shape = kShapePacked;
			ctx->cur.piece = s_rootKind[root];
			ctx->cur.flipped = s_rootFlip[root];
			const MeshTri& m = s_meshTris[i];
			int16_t terrain = TERRAIN_STONE;
			for (int k = 0; k < partsUsed; k++)
				if (m.first < partEnd[k])
				{
					terrain = partTerrain[k];
					break;
				}
			const float* a = mv + m.v[0] * 3;
			const float* b = mv + m.v[1] * 3;
			const float* c = mv + m.v[2] * 3;
			Emit(ctx, xf, a, flip ? c : b, flip ? b : c, terrain);
		}
	}

	// hkpConvexVerticesShape: each plane (n, d; outside where n.p + d > 0) gives one face, the hull vertices lying on
	// it sorted counter-clockwise around n and fanned, so faces come out outward without a hull algorithm.
	void EmitConvexVertices(Context* ctx, const void* shape, const Xf& xf)
	{
		ctx->st->hulls++;
		ctx->cur.shape = kShapeHull;
		ctx->cur.piece = kNoPiece;
		ctx->cur.flipped = 0;
		const uint8_t* blocks = At<const uint8_t*>(shape, kConvexVerticesBlocks);
		const int32_t blockCount = At<int32_t>(shape, kConvexVerticesBlockCount);
		const float* planes = At<const float*>(shape, kConvexVerticesPlanes);
		const int32_t planeCount = At<int32_t>(shape, kConvexVerticesPlaneCount);
		if (!blocks || blockCount <= 0 || blockCount > 64 || !planes || planeCount < 4 || planeCount > 256)
		{
			ctx->st->hullsSkipped++;
			return;
		}
		int16_t terrain = TERRAIN_STONE;
		if (const void* wrapper = At<const void*>(shape, kHkShapeWrapper))
			terrain = TerrainFor(At<uint32_t>(wrapper, kBhkShapeMaterial));

		// Blocks of 4 vertices stored as x[4], y[4], z[4] (getSupportingVertex 0x00D58EE0); padding repeats a vertex.
		float v[256][3];
		const int count = blockCount * 4;
		float extent = 0.0f;
		for (int i = 0; i < count; i++)
			for (int k = 0; k < 3; k++)
			{
				v[i][k] = *reinterpret_cast<const float*>(blocks + (i >> 2) * 0x30 + k * 0x10 + (i & 3) * 4);
				extent = fmaxf(extent, fabsf(v[i][k]));
			}
		const float eps = 1e-3f * extent + 1e-5f;

		for (int p = 0; p < planeCount; p++)
		{
			const float* pl = planes + p * 4;
			int face[256];
			int faceCount = 0;
			for (int i = 0; i < count; i++)
			{
				if (fabsf(pl[0] * v[i][0] + pl[1] * v[i][1] + pl[2] * v[i][2] + pl[3]) > eps)
					continue;
				bool duplicate = false;
				for (int j = 0; j < faceCount && !duplicate; j++)
					duplicate = fabsf(v[face[j]][0] - v[i][0]) + fabsf(v[face[j]][1] - v[i][1]) + fabsf(v[face[j]][2] - v[i][2]) < eps;
				if (!duplicate)
					face[faceCount++] = i;
			}
			if (faceCount < 3)
				continue;
			float centre[3] = {};
			for (int j = 0; j < faceCount; j++)
				for (int k = 0; k < 3; k++)
					centre[k] += v[face[j]][k] / faceCount;
			const float u[3] = { v[face[0]][0] - centre[0], v[face[0]][1] - centre[1], v[face[0]][2] - centre[2] };
			const float w[3] = { pl[1] * u[2] - pl[2] * u[1], pl[2] * u[0] - pl[0] * u[2], pl[0] * u[1] - pl[1] * u[0] };
			float angle[256];
			for (int j = 0; j < faceCount; j++)
			{
				const float d[3] = { v[face[j]][0] - centre[0], v[face[j]][1] - centre[1], v[face[j]][2] - centre[2] };
				angle[j] = atan2f(d[0] * w[0] + d[1] * w[1] + d[2] * w[2], d[0] * u[0] + d[1] * u[1] + d[2] * u[2]);
			}
			for (int j = 1; j < faceCount; j++)
				for (int m = j; m > 0 && angle[m] < angle[m - 1]; m--)
				{
					std::swap(angle[m], angle[m - 1]);
					std::swap(face[m], face[m - 1]);
				}
			for (int j = 1; j + 1 < faceCount; j++)
				EmitOutward(ctx, xf, v[face[0]], v[face[j]], v[face[j + 1]], terrain);
		}
	}

	// 12 triangles, each wound to face away from the box centre (counter-clockwise from outside).
	void EmitBox(Context* ctx, const void* shape, const Xf& xf)
	{
		ctx->st->boxes++;
		ctx->cur.shape = kShapeBox;
		ctx->cur.piece = kNoPiece;
		ctx->cur.flipped = 0;
		const float radius = At<float>(shape, 0x10);
		float h[3];
		for (int k = 0; k < 3; k++)
			h[k] = At<float>(shape, 0x20 + k * 4) + radius;
		int16_t terrain = TERRAIN_STONE;
		if (const void* wrapper = At<const void*>(shape, kHkShapeWrapper))
			terrain = TerrainFor(At<uint32_t>(wrapper, kBhkShapeMaterial));

		float corner[8][3];
		for (int i = 0; i < 8; i++)
			for (int k = 0; k < 3; k++)
				corner[i][k] = (i >> k) & 1 ? h[k] : -h[k];
		// Faces as corner index quads (bit k = +axis k).
		static const int kFaces[6][4] = { { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 } };
		for (const auto& f : kFaces)
		{
			const int tri[2][3] = { { f[0], f[1], f[2] }, { f[0], f[2], f[3] } };
			for (const auto& t : tri)
			{
				const float* a = corner[t[0]];
				const float* b = corner[t[1]];
				const float* c = corner[t[2]];
				const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
				const float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
				const float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
				const bool outward = n[0] * (a[0] + b[0] + c[0]) + n[1] * (a[1] + b[1] + c[1]) + n[2] * (a[2] + b[2] + c[2]) > 0.0f;
				EmitOutward(ctx, xf, a, outward ? b : c, outward ? c : b, terrain);
			}
		}
	}

	void WalkShape(Context* ctx, const void* shape, const Xf& xf, int depth)
	{
		if (!shape || depth > kMaxShapeDepth)
			return;
		uintptr_t vt = Vtable(shape);
		if (vt == kVtblHkPackedNiTriStripsContainer)
		{
			shape = reinterpret_cast<const uint8_t*>(shape) - 0x10;
			vt = Vtable(shape);
		}
		switch (vt)
		{
		case kVtblHkpMoppBvTreeShape:
		case kVtblHkScaledMoppBvTreeShape:
			ctx->st->mopps++;
			WalkShape(ctx, At<const void*>(shape, 0x34), xf, depth + 1);
			break;
		case kVtblHkpListShape:
		{
			ctx->st->lists++;
			const uint8_t* children = At<const uint8_t*>(shape, 0x18);
			const int32_t count = At<int32_t>(shape, 0x1C);
			for (int32_t i = 0; children && i < count && i < 4096; i++)
				WalkShape(ctx, *reinterpret_cast<const void* const*>(children + i * 0x10), xf, depth + 1);
			break;
		}
		case kVtblHkpTransformShape:
		{
			ctx->st->transforms++;
			Xf local;
			ReadXf(shape, 0x30, &local);
			WalkShape(ctx, At<const void*>(shape, 0x14), Compose(xf, local), depth + 1);
			break;
		}
		case kVtblHkpConvexTransformShape:
		{
			ctx->st->convexTransforms++;
			Xf local;
			ReadXf(shape, 0x20, &local);
			WalkShape(ctx, At<const void*>(shape, 0x18), Compose(xf, local), depth + 1);
			break;
		}
		case kVtblHkPackedNiTriStripsShape:
			if (Vtable(reinterpret_cast<const uint8_t*>(shape) + 0x10) == kVtblHkPackedNiTriStripsContainer)
				EmitPacked(ctx, shape, xf);
			else
				CountUnknown(ctx->st, vt);
			break;
		case kVtblHkpConvexVerticesShape:
			EmitConvexVertices(ctx, shape, xf);
			break;
		case kVtblHkpBoxShape:
			EmitBox(ctx, shape, xf);
			break;
		default:
			CountUnknown(ctx->st, vt);
			break;
		}
	}

	// M5.4c: which non-fixed bodies (doors, lifts, clutter) were left out near Mario.
	void LogSkippedNearMario(Context* ctx, const TESObjectREFR* ref, const void* body, uint8_t motion)
	{
		if (!ctx->verbose || ctx->skippedLogged >= kLoggedSkipped || (ref->baseForm && ref->baseForm->typeID == kFormType_TESNPC))
			return;
		Xf xf;
		ReadXf(body, kHkRigidBodyTransform, &xf);
		const float d[3] = { xf.t[0] * ctx->hk2bs - ctx->view[0], xf.t[1] * ctx->hk2bs - ctx->view[1], xf.t[2] * ctx->hk2bs - ctx->view[2] };
		const float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (dist > kSkippedLogRadius)
			return;
		ctx->skippedLogged++;
		const TESForm* base = ref->baseForm;
		_MESSAGE("collision: skipped body near Mario: ref %08X base %08X (form type %u), motion type %u, %.0f units away",
			ref->refID, base ? base->refID : 0, base ? base->typeID : 0, motion, dist);
	}

	void WalkBody(Context* ctx, const TESObjectREFR* ref, const void* collisionObject)
	{
		const void* worldObject = At<const void*>(collisionObject, kCollisionWorldObject);
		if (!worldObject)
			return;
		const uintptr_t vt = Vtable(worldObject);
		if (vt != kVtblBhkRigidBody && vt != kVtblBhkRigidBodyT)
			return;	// phantoms and the like
		const void* body = At<const void*>(worldObject, kBhkRefObject);
		if (!body || Vtable(body) != kVtblHkpRigidBody)
			return;
		if (!At<const void*>(body, kHkWorldObjectWorld))
		{
			ctx->st->bodiesNotInWorld++;
			return;
		}
		const uint8_t layer = At<uint8_t>(body, kHkWorldObjectLayer);
		const uint8_t motion = At<uint8_t>(body, kHkRigidBodyMotionType);
		// M5.4d: doors are taken as fixed in whatever pose they have at build time (closed, usually), whatever their
		// motion type or layer, until M8 moves them. Only the no-collision flag still drops them.
		const bool door = g_config.doors && ref->baseForm && ref->baseForm->typeID == kFormType_TESObjectDOOR;
		if (door && !(At<uint8_t>(body, kHkWorldObjectFilterFlags) & kHkFilterNoCollision))
		{
			ctx->st->doorBodies++;
			if (ctx->verbose && ctx->loggedDoors < kLoggedDoors)
			{
				ctx->loggedDoors++;
				_MESSAGE("collision: door ref %08X base %08X included: layer %u, motion type %u", ref->refID,
					ref->baseForm->refID, layer, motion);
			}
		}
		else if (SkipLayer(layer) || (At<uint8_t>(body, kHkWorldObjectFilterFlags) & kHkFilterNoCollision))
		{
			ctx->st->layerSkipped[layer < kLayers ? layer : 0]++;
			return;
		}
		else
			ctx->st->layerKept[layer < kLayers ? layer : 0]++;
		ctx->cur.layer = layer;
		// M8.1: keyframed bodies (animated doors, lifts, moving platforms) become libsm64 surface objects that follow the
		// body every tick. M8.2: so do dynamic bodies (crates, barrels, furniture) on solid layers; the small ones are
		// dropped by size when the object is built. Actors' bones and ragdolls stay out (M10 colliders).
		const uint8_t baseType = ref->baseForm ? ref->baseForm->typeID : 0;
		const bool actor = baseType == kFormType_TESNPC || baseType == kFormType_TESCreature;
		const bool keyframed = g_config.movingBodies && motion == kHkMotionKeyframed;
		const bool dynamic = g_config.dynamicBodies && IsDynamicMotion(motion) && DynamicLayer(layer);
		if (!actor && (keyframed || dynamic))
		{
			Xf xf;
			ReadXf(body, kHkRigidBodyTransform, &xf);
			const float dx = xf.t[0] * ctx->hk2bs - ctx->view[0], dy = xf.t[1] * ctx->hk2bs - ctx->view[1];
			const float dist = sqrtf(dx * dx + dy * dy);
			if (dist <= g_config.staticRadius)
			{
				ctx->st->bodiesMoving++;
				ctx->st->movingDoors += door;
				ctx->st->movingDynamic += dynamic && !keyframed;
				MovingBody b{ const_cast<void*>(worldObject), body, ref->refID, ref->baseForm ? ref->baseForm->refID : 0,
					baseType, layer, motion, door, !keyframed, dist };
				s_moving.push_back(b);
			}
			return;
		}
		if (!door && motion != kHkMotionFixed)
		{
			ctx->st->bodiesSkippedMotion++;	// dynamic on other layers, actors, or the M8 switches off
			LogSkippedNearMario(ctx, ref, body, motion);
			return;
		}
		ctx->st->bodiesFixed++;
		Xf xf;
		ReadXf(body, kHkRigidBodyTransform, &xf);
		if (ctx->verbose && ctx->loggedBodies < kLoggedBodies)
		{
			ctx->loggedBodies++;
			_MESSAGE("collision: body of ref %08X at fnv (%.1f, %.1f, %.1f), ref at (%.1f, %.1f, %.1f), rotation columns "
				"(%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f)", ref->refID, xf.t[0] * ctx->hk2bs, xf.t[1] * ctx->hk2bs,
				xf.t[2] * ctx->hk2bs, ref->posX, ref->posY, ref->posZ, xf.c[0][0], xf.c[0][1], xf.c[0][2], xf.c[1][0],
				xf.c[1][1], xf.c[1][2], xf.c[2][0], xf.c[2][1], xf.c[2][2]);
		}
		WalkShape(ctx, At<const void*>(body, kHkRigidBodyShape), xf, 0);
	}

	void WalkNode(Context* ctx, const TESObjectREFR* ref, const void* node, int depth)
	{
		if (!node || depth > kMaxNodeDepth)
			return;
		if (const void* collisionObject = At<const void*>(node, kAVCollisionObject))
			WalkBody(ctx, ref, collisionObject);
		const auto getNiNode = *reinterpret_cast<void* (__thiscall* const*)(const void*)>(Vtable(node) + kVtblGetNiNode);
		if (!getNiNode(node))
			return;
		const void* const* children = At<const void* const*>(node, kNodeChildren);
		const uint16_t count = At<uint16_t>(node, kNodeChildCount);
		for (uint16_t i = 0; children && i < count; i++)
			WalkNode(ctx, ref, children[i], depth + 1);
	}

	// A fault inside one ref's 3D drops what that ref had added so far and moves on.
	void WalkRefGuarded(Context* ctx, const TESObjectREFR* ref)
	{
		if (ctx->st->capped)
			return;
		const uint32_t before = ctx->st->triangles;
		const size_t movingBefore = s_moving.size();
		__try
		{
			const void* renderState = At<const void*>(ref, kRefRenderState);
			const void* root = renderState ? At<const void*>(renderState, kRenderStateRoot) : nullptr;
			if (!root)
				return;
			ctx->st->refsWith3D++;
			ctx->cur.refID = ref->refID;
			ctx->cur.baseID = ref->baseForm ? ref->baseForm->refID : 0;
			ctx->cur.baseType = ref->baseForm ? ref->baseForm->typeID : 0;
			// Whole ref outside the clip square: nothing of it would be kept. A zero radius (no geometry) is walked.
			if (const float* bound = At<const float*>(root, kAVWorldBound))
			{
				const float r = bound[3] + kCullMargin;
				if (bound[3] > 0.0f && (bound[0] + r < ctx->minX || bound[0] - r > ctx->maxX || bound[1] + r < ctx->minY ||
					bound[1] - r > ctx->maxY))
				{
					ctx->st->refsCulled++;
					return;
				}
			}
			WalkNode(ctx, ref, root, 0);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			ctx->st->refsFaulted++;
			ctx->st->triangles = before;
			s_moving.resize(movingBefore);
		}
	}

	uint32_t ReadGridGuarded(const void** cells)
	{
		__try
		{
			const void* tes = *reinterpret_cast<const void* const*>(kTES);
			const void* grid = tes ? At<const void*>(tes, kTESGridCells) : nullptr;
			if (!grid)
				return 0;
			const uint32_t size = At<uint32_t>(grid, kGridSize);
			const void* const* array = At<const void* const*>(grid, kGridCellArray);
			if (!array || size == 0 || size * size > kMaxGridCells)
				return 0;
			for (uint32_t i = 0; i < size * size; i++)
				cells[i] = array[i];
			return size * size;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return 0;
		}
	}

	// Copies a cell's ref list (tList: data, next) into `refs`; returns the count, or UINT32_MAX on fault.
	uint32_t ReadRefsGuarded(const void* cell, const TESObjectREFR** refs, uint32_t capacity)
	{
		uint32_t n = 0;
		__try
		{
			const uint8_t* entry = reinterpret_cast<const uint8_t*>(cell) + kCellObjectList;
			for (uint32_t guard = 0; entry && guard < 100000; guard++)
			{
				const TESObjectREFR* ref = *reinterpret_cast<const TESObjectREFR* const*>(entry);
				if (ref && n < capacity)
					refs[n++] = ref;
				entry = *reinterpret_cast<const uint8_t* const*>(entry + 4);
			}
			return n;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return UINT32_MAX;
		}
	}
}

void Collision_AddStatics(const PlayerCharacter* player, const float centre[3], bool verbose, std::vector<SM64Surface>& out)
{
	s_stats = {};
	s_built = false;
	s_moving.clear();
	if (!g_config.statics)
	{
		_MESSAGE("collision: statics off (ini)");
		return;
	}
	if (!player->parentCell)
		return;
	LARGE_INTEGER freq, start, end;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);

	// Exteriors: the loaded grid. Interiors (M5.4): the player's cell alone, assumed to use the same Havok frame
	// (ref position = body translation x fHk2BSScale; the spawn body log checks it).
	const bool interior = !player->parentCell->worldSpace;
	const void* cells[kMaxGridCells];
	uint32_t cellCount = 1;
	if (interior)
		cells[0] = player->parentCell;
	else if (!(cellCount = ReadGridGuarded(cells)))
	{
		_ERROR("collision: could not read the loaded cell grid, no statics");
		return;
	}

	const size_t base = out.size();
	out.resize(base + g_config.maxStaticTriangles);
	Context ctx{};
	ctx.out = out.data() + base;
	s_meta.resize(g_config.maxStaticTriangles);
	ctx.meta = s_meta.data();
	ctx.capacity = g_config.maxStaticTriangles;
	ctx.hk2bs = *reinterpret_cast<const float*>(kHk2BSScale);
	ctx.minX = centre[0] - g_config.staticRadius;
	ctx.maxX = centre[0] + g_config.staticRadius;
	ctx.minY = centre[1] - g_config.staticRadius;
	ctx.maxY = centre[1] + g_config.staticRadius;
	ctx.flip = g_config.staticFlipWinding;
	ctx.interior = interior;
	ctx.view[0] = centre[0];
	ctx.view[1] = centre[1];
	ctx.view[2] = centre[2] + kViewHeight;
	ctx.st = &s_stats;
	ctx.verbose = verbose;
	if (!(ctx.hk2bs > 1.0f && ctx.hk2bs < 20.0f))
	{
		_ERROR("collision: fHk2BSScale reads %f, no statics", ctx.hk2bs);
		out.resize(base);
		return;
	}

	std::vector<const TESObjectREFR*> refs(16384);
	for (uint32_t i = 0; i < cellCount; i++)
	{
		if (!cells[i])
			continue;
		const uint32_t n = ReadRefsGuarded(cells[i], refs.data(), (uint32_t)refs.size());
		if (n == UINT32_MAX)
		{
			_ERROR("collision: fault reading the ref list of grid cell %u", i);
			continue;
		}
		s_stats.cells++;
		for (uint32_t r = 0; r < n; r++)
		{
			if (refs[r] == player)
				continue;
			s_stats.refs++;
			WalkRefGuarded(&ctx, refs[r]);
		}
	}
	out.resize(base + s_stats.triangles);
	for (size_t i = base; i < out.size(); i++)
		s_stats.terrains[out[i].terrain < 7 ? out[i].terrain : 0]++;
	QueryPerformanceCounter(&end);
	s_stats.buildMs = 1000.0 * (end.QuadPart - start.QuadPart) / freq.QuadPart;
	s_built = true;
	if (!verbose)
	{
		if (s_stats.capped)
			_WARNING("collision: static triangle cap %u reached, some statics near Mario are missing", g_config.maxStaticTriangles);
		return;
	}

	_MESSAGE("collision: statics (%s) from %u cells, %u refs (%u with 3D, %u culled by bound, %u faulted) in %.1f ms; fHk2BSScale "
		"%.5f, radius %.0f, winding %s", interior ? "interior" : "exterior", s_stats.cells, s_stats.refs, s_stats.refsWith3D, s_stats.refsCulled,
		s_stats.refsFaulted, s_stats.buildMs, ctx.hk2bs, g_config.staticRadius, ctx.flip ? "flipped (ini)" : "as stored");
	_MESSAGE("collision: bodies %u fixed (%u of them door bodies), %u moving (%u doors, %u dynamic), %u skipped (other dynamic, "
		"actors), %u not in the Havok world", s_stats.bodiesFixed, s_stats.doorBodies - s_stats.movingDoors, s_stats.bodiesMoving,
		s_stats.movingDoors, s_stats.movingDynamic, s_stats.bodiesSkippedMotion, s_stats.bodiesNotInWorld);
	char kept[512] = "", skipped[512] = "";
	for (int i = 0; i < kLayers; i++)
	{
		if (s_stats.layerKept[i])
			sprintf_s(kept + strlen(kept), sizeof(kept) - strlen(kept), " %d:%u", i, s_stats.layerKept[i]);
		if (s_stats.layerSkipped[i])
			sprintf_s(skipped + strlen(skipped), sizeof(skipped) - strlen(skipped), " %d:%u", i, s_stats.layerSkipped[i]);
	}
	_MESSAGE("collision: body layers kept%s; skipped (layer or no-collision flag)%s", kept[0] ? kept : " none",
		skipped[0] ? skipped : " none");
	_MESSAGE("collision: shapes %u packed meshes, %u boxes, %u convex hulls (%u skipped: no planes), %u MOPP, %u lists, %u "
		"transforms, %u convex transforms", s_stats.packed, s_stats.boxes, s_stats.hulls, s_stats.hullsSkipped, s_stats.mopps,
		s_stats.lists, s_stats.transforms, s_stats.convexTransforms);
	for (const UnknownShape& u : s_stats.unknown)
		if (u.vtable)
			_MESSAGE("collision: unhandled shape vtable %08X x%u", u.vtable, u.count);
	if (s_stats.unknownOther)
		_MESSAGE("collision: %u more unhandled shapes (other vtables)", s_stats.unknownOther);
	_MESSAGE("collision: static triangles %u kept of %u (%u outside the radius, %u degenerate, %u bad index)%s; "
		"terrain grass %u stone %u sand %u spooky %u water %u", s_stats.triangles, s_stats.trianglesIn, s_stats.clipped,
		s_stats.degenerate, s_stats.badIndex, s_stats.capped ? ", CAP REACHED (iMaxStaticTriangles)" : "",
		s_stats.terrains[TERRAIN_GRASS], s_stats.terrains[TERRAIN_STONE], s_stats.terrains[TERRAIN_SAND],
		s_stats.terrains[TERRAIN_SPOOKY], s_stats.terrains[TERRAIN_WATER]);
	_MESSAGE("collision: packed mesh pieces: %u closed (%u flipped to face out), %u open (%u flipped to face up), %u room pieces "
		"(%u closed room shells; %u flipped: floors up, ceilings down), %u vertical open (stored winding)", s_stats.piecesClosed,
		s_stats.piecesClosedFlipped, s_stats.piecesOpen, s_stats.piecesOpenFlipped, s_stats.piecesRoom, s_stats.piecesRoomShell,
		s_stats.piecesRoomFlipped, s_stats.piecesVertical);
	if (s_stats.capped)
		_WARNING("collision: static triangle cap %u reached, some statics near the player are missing", g_config.maxStaticTriangles);
}

const std::vector<MovingBody>& Collision_MovingBodies()
{
	return s_moving;
}

namespace
{
	// The body is still the one we found: same wrapper link and vtables, still in a Havok world.
	bool BodyValid(const MovingBody& b)
	{
		const uintptr_t vt = Vtable(b.worldObject);
		return (vt == kVtblBhkRigidBody || vt == kVtblBhkRigidBodyT) && At<const void*>(b.worldObject, kBhkRefObject) == b.body
			&& Vtable(b.body) == kVtblHkpRigidBody && At<const void*>(b.body, kHkWorldObjectWorld);
	}

	void ToPose(const Xf& xf, float hk2bs, BodyPose* pose)
	{
		memcpy(pose->rot, xf.c, sizeof(pose->rot));
		for (int k = 0; k < 3; k++)
			pose->pos[k] = xf.t[k] * hk2bs;
	}

	bool ReadPoseGuarded(const MovingBody& b, BodyPose* pose)
	{
		__try
		{
			if (!BodyValid(b))
				return false;
			Xf xf;
			ReadXf(b.body, kHkRigidBodyTransform, &xf);
			ToPose(xf, *reinterpret_cast<const float*>(kHk2BSScale), pose);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool BodySurfacesGuarded(Context* ctx, const MovingBody& b, BodyPose* pose)
	{
		__try
		{
			if (!BodyValid(b))
				return false;
			ReadXf(b.body, kHkRigidBodyTransform, &ctx->pose);
			ToPose(ctx->pose, ctx->hk2bs, pose);
			WalkShape(ctx, At<const void*>(b.body, kHkRigidBodyShape), ctx->pose, 0);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

bool Collision_ReadBodyPose(const MovingBody& body, BodyPose* pose)
{
	return ReadPoseGuarded(body, pose);
}

bool Collision_BodySurfaces(const MovingBody& body, bool interior, uint32_t maxTriangles, std::vector<SM64Surface>& out,
	BodyPose* pose)
{
	out.resize(maxTriangles);
	Stats st{};
	Context ctx{};
	ctx.out = out.data();
	ctx.capacity = maxTriangles;
	ctx.hk2bs = *reinterpret_cast<const float*>(kHk2BSScale);
	ctx.flip = g_config.staticFlipWinding;
	ctx.interior = interior;
	ctx.st = &st;
	ctx.local = true;
	const bool ok = BodySurfacesGuarded(&ctx, body, pose);
	out.resize(ok ? st.triangles : 0);
	return ok;
}

void Collision_DumpStatics()
{
	if (!s_built)
	{
		Console_Print("SM64FNV: statics not loaded");
		return;
	}
	Console_Print("SM64FNV: statics %u triangles from %u fixed bodies (%u packed, %u boxes, %u hulls), %u skipped bodies, "
		"%u refs culled, %u door bodies, %.0f ms%s", s_stats.triangles, s_stats.bodiesFixed, s_stats.packed, s_stats.boxes,
		s_stats.hulls, s_stats.bodiesSkippedMotion, s_stats.refsCulled, s_stats.doorBodies, s_stats.buildMs, s_stats.capped ? ", CAPPED" : "");
	_MESSAGE("state dump statics: %u triangles, %u fixed bodies, %u packed, %u boxes, %u hulls, %u skipped bodies, %u of %u "
		"refs culled, pieces closed %u (%u flipped) open %u (%u flipped) room %u (%u flipped) vertical %u, %u door bodies, %.1f ms",
		s_stats.triangles, s_stats.bodiesFixed, s_stats.packed, s_stats.boxes, s_stats.hulls, s_stats.bodiesSkippedMotion,
		s_stats.refsCulled, s_stats.refsWith3D, s_stats.piecesClosed, s_stats.piecesClosedFlipped, s_stats.piecesOpen,
		s_stats.piecesOpenFlipped, s_stats.piecesRoom, s_stats.piecesRoomFlipped, s_stats.piecesVertical, s_stats.doorBodies,
		s_stats.buildMs);
}

namespace
{
	const char* PieceName(const TriMeta& m)
	{
		static const char* const kShapes[] = { "packed", "box", "hull" };
		static const char* const kPieces[] = { "closed(out)", "open(up)", "vertical(stored)", "room(floors up)", "-" };
		static char buffer[96];
		sprintf_s(buffer, "%s %s%s, layer %u, base form type %u", kShapes[m.shape], kPieces[m.piece], m.flipped ? " flipped" : "",
			m.layer, m.baseType);
		return buffer;
	}

	// Closest point to p on triangle abc (Ericson, Real-Time Collision Detection 5.1.5).
	void ClosestOnTriangle(const float p[3], const float a[3], const float b[3], const float c[3], float out[3])
	{
		auto dot = [](const float* u, const float* v) { return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
		const float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ac[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		const float ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
		auto set = [out, a, ab, ac](float v, float w) {
			for (int k = 0; k < 3; k++)
				out[k] = a[k] + ab[k] * v + ac[k] * w;
		};
		const float d1 = dot(ab, ap), d2 = dot(ac, ap);
		if (d1 <= 0.0f && d2 <= 0.0f)
			return set(0.0f, 0.0f);
		const float bp[3] = { p[0] - b[0], p[1] - b[1], p[2] - b[2] };
		const float d3 = dot(ab, bp), d4 = dot(ac, bp);
		if (d3 >= 0.0f && d4 <= d3)
			return set(1.0f, 0.0f);
		const float vc = d1 * d4 - d3 * d2;
		if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
			return set(d1 / (d1 - d3), 0.0f);
		const float cp[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] };
		const float d5 = dot(ab, cp), d6 = dot(ac, cp);
		if (d6 >= 0.0f && d5 <= d6)
			return set(0.0f, 1.0f);
		const float vb = d5 * d2 - d1 * d6;
		if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
			return set(0.0f, d2 / (d2 - d6));
		const float va = d3 * d6 - d5 * d4;
		if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f)
		{
			const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
			return set(1.0f - w, w);
		}
		const float denom = 1.0f / (va + vb + vc);
		set(vb * denom, vc * denom);
	}

	struct DumpLine
	{
		float key;
		uint32_t index;
		float a, b;
	};
}

// M5.4c: logs the static triangles above/below Mario (column through his feet) and the walls within kDumpWallRadius of
// his body, with the ref they come from and how they were oriented, to diagnose missing floors and walls he clips through.
void Collision_DumpNearby(const float feet[3], const char* reason)
{
	if (!s_built)
		return;
	const float body[3] = { feet[0], feet[1], feet[2] + kViewHeight };
	std::vector<DumpLine> column, walls, lowCeilings;
	for (uint32_t i = 0; i < s_stats.triangles; i++)
	{
		const TriMeta& m = s_meta[i];
		const float e1[3] = { m.w[1][0] - m.w[0][0], m.w[1][1] - m.w[0][1], m.w[1][2] - m.w[0][2] };
		const float e2[3] = { m.w[2][0] - m.w[0][0], m.w[2][1] - m.w[0][1], m.w[2][2] - m.w[0][2] };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (len <= 0.0f)
			continue;
		for (float& v : n)
			v /= len;
		// Column: (x, y) inside the triangle's ground projection, height of the triangle there.
		if (fabsf(n[2]) > 0.05f)
		{
			const float det = e1[0] * e2[1] - e1[1] * e2[0];
			const float px = feet[0] - m.w[0][0], py = feet[1] - m.w[0][1];
			const float u = (px * e2[1] - py * e2[0]) / det, v = (e1[0] * py - e1[1] * px) / det;
			if (u >= 0.0f && v >= 0.0f && u + v <= 1.0f)
			{
				const float z = m.w[0][2] + e1[2] * u + e2[2] * v - feet[2];
				column.push_back({ fabsf(z), i, z, n[2] });
			}
		}
		// Walls (SM64: |normal y| < 0.01, we list up to 0.3) near the body; offset > 0 = Mario on the side it faces.
		if (fabsf(n[2]) < 0.3f)
		{
			float q[3];
			ClosestOnTriangle(body, m.w[0], m.w[1], m.w[2], q);
			const float d = sqrtf((q[0] - body[0]) * (q[0] - body[0]) + (q[1] - body[1]) * (q[1] - body[1]) + (q[2] - body[2]) * (q[2] - body[2]));
			if (d <= kDumpWallRadius)
			{
				const float offset = n[0] * (body[0] - m.w[0][0]) + n[1] * (body[1] - m.w[0][1]) + n[2] * (body[2] - m.w[0][2]);
				walls.push_back({ d, i, offset, n[2] });
			}
		}
		// Low ceilings near Mario: SM64 stops a walk where the ceiling ahead is under his height above the floor,
		// so a ceiling-facing triangle near foot level acts as an invisible wall (M7.1b).
		else if (n[2] < -0.3f)
		{
			float q[3];
			ClosestOnTriangle(body, m.w[0], m.w[1], m.w[2], q);
			const float horizontal = sqrtf((q[0] - body[0]) * (q[0] - body[0]) + (q[1] - body[1]) * (q[1] - body[1]));
			const float height = q[2] - feet[2];
			if (horizontal <= kDumpWallRadius && height > -kLowCeilingBelow && height < kLowCeilingAbove)
				lowCeilings.push_back({ horizontal, i, height, n[2] });
		}
	}
	auto byKey = [](const DumpLine& x, const DumpLine& y) { return x.key < y.key; };
	std::sort(column.begin(), column.end(), byKey);
	std::sort(walls.begin(), walls.end(), byKey);
	std::sort(lowCeilings.begin(), lowCeilings.end(), byKey);
	_MESSAGE("collision dump (%s) at feet fnv (%.0f, %.0f, %.0f): %u surfaces in the column, %u walls and %u low ceilings within "
		"%.0f units", reason, feet[0], feet[1], feet[2], (uint32_t)column.size(), (uint32_t)walls.size(), (uint32_t)lowCeilings.size(),
		kDumpWallRadius);
	for (int i = 0; i < (int)lowCeilings.size() && i < kDumpLines; i++)
	{
		const TriMeta& m = s_meta[lowCeilings[i].index];
		_MESSAGE("  low ceiling: %5.1f away, %+6.1f from feet, normal z %+.2f, ref %08X base %08X, %s", lowCeilings[i].key,
			lowCeilings[i].a, lowCeilings[i].b, m.refID, m.baseID, PieceName(m));
	}
	for (int i = 0; i < (int)column.size() && i < kDumpLines; i++)
	{
		const TriMeta& m = s_meta[column[i].index];
		_MESSAGE("  column: %+7.1f from feet, %s (normal z %+.2f), ref %08X base %08X, %s", column[i].a,
			column[i].b > 0.01f ? "floor  " : column[i].b < -0.01f ? "ceiling" : "wall   ", column[i].b, m.refID, m.baseID, PieceName(m));
	}
	for (int i = 0; i < (int)walls.size() && i < kDumpLines; i++)
	{
		const TriMeta& m = s_meta[walls[i].index];
		_MESSAGE("  wall: %5.1f away, offset %+6.1f (%s), normal z %+.2f, ref %08X base %08X, %s, tri (%.0f %.0f %.0f) (%.0f %.0f %.0f) "
			"(%.0f %.0f %.0f)", walls[i].key, walls[i].a, walls[i].a >= 0.0f ? "faces Mario" : "faces AWAY", walls[i].b, m.refID,
			m.baseID, PieceName(m), m.w[0][0], m.w[0][1], m.w[0][2], m.w[1][0], m.w[1][1], m.w[1][2], m.w[2][0], m.w[2][1], m.w[2][2]);
	}
	uint32_t away = 0;
	for (const DumpLine& w : walls)
		away += w.a < 0.0f;
	Console_Print("SM64FNV: collision dump: %u surfaces above/below Mario, %u walls near him (%u facing away), see the log",
		(uint32_t)column.size(), (uint32_t)walls.size(), away);
	if (!lowCeilings.empty())
		Console_Print("SM64FNV: %u low ceilings near Mario (they stop him like walls)", (uint32_t)lowCeilings.size());
}
