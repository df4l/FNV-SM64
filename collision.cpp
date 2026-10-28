#include "collision.h"
#include "config.h"
#include "game_addrs.h"
#include "units.h"
#include "nvse/GameAPI.h"
#include "nvse/GameObjects.h"
#include "nvse/GameForms.h"
#include "decomp/include/surface_terrains.h"

#include <climits>
#include <cmath>
#include <cstring>

namespace
{
	using namespace GameAddr;

	constexpr float kCellSize = 4096.0f;		// exterior cell width, game units
	constexpr float kBlockSize = 2048.0f;		// a cell is 2 x 2 blocks
	constexpr int kGridRes = 17;				// vertices per block side, 128 units apart (16 tiles)
	constexpr int kBlockVerts = kGridRes * kGridRes;
	constexpr float kSpacing = 128.0f;
	constexpr uint32_t kMaxGridCells = 256;		// uGridsToLoad^2, uGridsToLoad <= 16
	// The terrain under the player may sit at most this far from their feet (game units). Further away means the
	// frame is wrong, so the terrain is not used.
	constexpr float kHeightTolerance = 64.0f;

	// One cell's land vertices, read from game memory. Plain data so it can be filled inside __try.
	struct LandGrid
	{
		int32_t cellX, cellY;
		float baseHeight;
		const float* blocks[4];		// kBlockVerts NiPoint3 each
		const char* source;			// "vertex arrays" or "render mesh"
		const char* failStage;
	};

	struct Status
	{
		const char* source = "none";	// what Mario stands on after the last spawn
		uint32_t cells = 0, cellsFailed = 0, blocksRejected = 0, triangles = 0, flipped = 0;
		bool heightFound = false;
		float heightDiff = 0.0f;		// terrain z under the player - player z
	} s_status;

	template <class T> T At(const void* p, uint32_t offset)
	{
		return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(p) + offset);
	}

	// Copies the loaded grid's cell pointers; returns the count (0 on fault).
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

	// cell -> LAND -> loaded data -> 4 blocks of 17x17 vertices. Like TESObjectLAND::GetVertex: the vertex arrays,
	// or else vertex 0.. of each block's render geometry (child 0 of the block node).
	bool ReadLandGuarded(const void* cell, LandGrid* g)
	{
		__try
		{
			g->cellX = g->cellY = INT32_MIN;
			g->failStage = "no LAND";
			const void* land = At<const void*>(cell, kCellLand);
			if (!land)
				return false;
			g->failStage = "LAND data not loaded";
			const void* data = At<const void*>(land, kLandLoadedData);
			if (!data)
				return false;
			g->cellX = At<int32_t>(data, kLandDataCellX);
			g->cellY = At<int32_t>(data, kLandDataCellY);
			g->baseHeight = At<float>(data, kLandDataBaseHeight);

			const float* const* vertices = At<const float* const*>(data, kLandDataVertices);
			if (vertices && vertices[0] && vertices[1] && vertices[2] && vertices[3])
			{
				for (int b = 0; b < 4; b++)
					g->blocks[b] = vertices[b];
				g->source = "vertex arrays";
				g->failStage = nullptr;
				return true;
			}

			g->failStage = "no vertex arrays and no render mesh";
			const void* const* meshes = At<const void* const*>(data, kLandDataMeshes);
			if (!meshes)
				return false;
			for (int b = 0; b < 4; b++)
			{
				g->failStage = "block node missing or empty";
				const void* node = meshes[b];
				if (!node || At<uint16_t>(node, kNodeChildCount) == 0)
					return false;
				const void* geometry = At<const void* const*>(node, kNodeChildren)[0];
				const void* geomData = geometry ? At<const void*>(geometry, kGeomData) : nullptr;
				g->failStage = "block geometry is not 17x17";
				if (!geomData || At<uint16_t>(geomData, kGDVertexCount) != kBlockVerts)
					return false;
				g->blocks[b] = At<const float*>(geomData, kGDVertices);
				if (!g->blocks[b])
					return false;
			}
			g->source = "render mesh";
			g->failStage = nullptr;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			g->failStage = "fault while reading";
			return false;
		}
	}

	struct BlockFrame
	{
		float offX, offY;	// added to x/y to reach world coordinates
		bool xFastest;		// vertex index = row * 17 + col with col along x
	};

	// Blocks are laid out as in TESObjectLAND::Attach3D: block b covers x from cell + (b % 2) * 2048 and y from
	// cell + (b / 2) * 2048. The vertices' own frame (world, cell or block relative) is found by matching that.
	bool FrameGuarded(const LandGrid* g, int b, BlockFrame* f, float* minX, float* minY, bool* faulted)
	{
		*faulted = false;
		__try
		{
			const float* v = g->blocks[b];
			*minX = v[0];
			*minY = v[1];
			for (int i = 1; i < kBlockVerts; i++)
			{
				*minX = v[i * 3] < *minX ? v[i * 3] : *minX;
				*minY = v[i * 3 + 1] < *minY ? v[i * 3 + 1] : *minY;
			}
			const float stepX = v[3] - v[0], stepY = v[4] - v[1];
			if (fabsf(stepX - kSpacing) < 1.0f && fabsf(stepY) < 1.0f)
				f->xFastest = true;
			else if (fabsf(stepY - kSpacing) < 1.0f && fabsf(stepX) < 1.0f)
				f->xFastest = false;
			else
				return false;
			f->offX = g->cellX * kCellSize + (b % 2) * kBlockSize - *minX;
			f->offY = g->cellY * kCellSize + (b / 2) * kBlockSize - *minY;
			// Any sensible frame differs from world by whole blocks.
			return fmodf(fabsf(f->offX), 1024.0f) < 1.0f && fmodf(fabsf(f->offY), 1024.0f) < 1.0f;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			*faulted = true;
			return false;
		}
	}

	// 16 x 16 tiles, 2 triangles each; returns how many surfaces were written.
	uint32_t ConvertBlockGuarded(const LandGrid* g, int b, const BlockFrame* f, SM64Surface* out, uint32_t* flipped,
		bool* faulted)
	{
		uint32_t written = 0;
		*faulted = false;
		__try
		{
			const float* v = g->blocks[b];
			float grid[kBlockVerts][3];
			for (int i = 0; i < kBlockVerts; i++)
				Units::FnvToSm64(v[i * 3] + f->offX, v[i * 3 + 1] + f->offY, v[i * 3 + 2] + g->baseHeight, grid[i]);

			for (int r = 0; r < kGridRes - 1; r++)
				for (int c = 0; c < kGridRes - 1; c++)
				{
					const auto idx = [f](int row, int col) { return f->xFastest ? row * kGridRes + col : col * kGridRes + row; };
					const int quad[2][3] = {
						{ idx(r, c), idx(r, c + 1), idx(r + 1, c + 1) },
						{ idx(r, c), idx(r + 1, c + 1), idx(r + 1, c) },
					};
					for (const auto& tri : quad)
					{
						const float* a = grid[tri[0]];
						const float* p = grid[tri[1]];
						const float* q = grid[tri[2]];
						// Upward normal (SM64 Y) for the winding the test floor uses; terrain must face up.
						const float e1x = p[0] - a[0], e1z = p[2] - a[2], e2x = q[0] - a[0], e2z = q[2] - a[2];
						const bool flip = e1z * e2x - e1x * e2z < 0.0f;
						if (flip)
							(*flipped)++;
						const float* corners[3] = { a, flip ? q : p, flip ? p : q };
						SM64Surface& s = out[written++];
						s.type = SURFACE_DEFAULT;
						s.force = 0;
						s.terrain = TERRAIN_GRASS;	// no material on land vertices; land textures come in M5.4
						for (int i = 0; i < 3; i++)
							for (int k = 0; k < 3; k++)
								s.vertices[i][k] = (int32_t)lroundf(corners[i][k]);
					}
				}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			*faulted = true;
		}
		return written;
	}

	// Highest surface under (x, z), SM64 units; false if none.
	bool HeightUnder(const std::vector<SM64Surface>& surfaces, float x, float z, float* outY)
	{
		bool found = false;
		for (const SM64Surface& s : surfaces)
		{
			const float ax = (float)s.vertices[0][0], az = (float)s.vertices[0][2];
			const float bx = (float)s.vertices[1][0], bz = (float)s.vertices[1][2];
			const float cx = (float)s.vertices[2][0], cz = (float)s.vertices[2][2];
			const float det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
			if (fabsf(det) < 1e-3f)
				continue;
			const float l0 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / det;
			const float l1 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / det;
			const float l2 = 1.0f - l0 - l1;
			if (l0 < 0.0f || l1 < 0.0f || l2 < 0.0f)
				continue;
			const float y = l0 * s.vertices[0][1] + l1 * s.vertices[1][1] + l2 * s.vertices[2][1];
			if (!found || y > *outY)
				*outY = y;
			found = true;
		}
		return found;
	}
}

bool Collision_BuildTerrain(const PlayerCharacter* player, const float centre[3], bool reload, std::vector<SM64Surface>& out)
{
	out.clear();
	s_status = {};
	s_status.source = "test floor";
	if (!g_config.terrain)
	{
		_MESSAGE("collision: terrain off (ini), using the test floor");
		return false;
	}
	if (!player->parentCell || !player->parentCell->worldSpace)
	{
		_MESSAGE("collision: interior, no terrain; using the test floor");
		return false;
	}

	const void* cells[kMaxGridCells];
	const uint32_t cellCount = ReadGridGuarded(cells);
	if (!cellCount)
	{
		_ERROR("collision: could not read the loaded cell grid, no terrain");
		return false;
	}
	const int32_t playerCellX = (int32_t)floorf(centre[0] / kCellSize);
	const int32_t playerCellY = (int32_t)floorf(centre[1] / kCellSize);

	// The centre's cell and its 8 neighbours; Mario_Tick rebuilds around him before he gets near their edge.
	out.resize(9 * 4 * (kGridRes - 1) * (kGridRes - 1) * 2);
	uint32_t written = 0;
	bool loggedFrame = reload;	// the frame is logged at spawn only
	for (uint32_t i = 0; i < cellCount && s_status.cells < 9; i++)
	{
		if (!cells[i])
			continue;
		LandGrid g{};
		const bool read = ReadLandGuarded(cells[i], &g);
		if (g.cellX == INT32_MIN || abs(g.cellX - playerCellX) > 1 || abs(g.cellY - playerCellY) > 1)
			continue;
		if (!read)
		{
			s_status.cellsFailed++;
			_MESSAGE("collision: cell (%d, %d) skipped: %s", g.cellX, g.cellY, g.failStage);
			continue;
		}
		s_status.cells++;
		for (int b = 0; b < 4; b++)
		{
			BlockFrame f{};
			float minX = 0.0f, minY = 0.0f;
			bool faulted = false;
			const bool framed = FrameGuarded(&g, b, &f, &minX, &minY, &faulted);
			if (!framed)
			{
				s_status.blocksRejected++;
				_MESSAGE("collision: cell (%d, %d) block %d rejected: %s (min x %.1f y %.1f, %s)", g.cellX, g.cellY, b,
					faulted ? "fault" : "grid not 128-spaced or frame not block-aligned", minX, minY, g.source);
				continue;
			}
			if (!loggedFrame)
			{
				loggedFrame = true;
				_MESSAGE("collision: land frame from cell (%d, %d) block %d: %s, base height %.1f, vertex min (%.1f, %.1f), "
					"offset to world (%.0f, %.0f), %s-fastest", g.cellX, g.cellY, b, g.source, g.baseHeight, minX, minY,
					f.offX, f.offY, f.xFastest ? "x" : "y");
			}
			const uint32_t n = ConvertBlockGuarded(&g, b, &f, out.data() + written, &s_status.flipped, &faulted);
			if (faulted)
			{
				s_status.blocksRejected++;
				_ERROR("collision: fault converting cell (%d, %d) block %d", g.cellX, g.cellY, b);
			}
			written += n;
		}
	}
	out.resize(written);
	s_status.triangles = written;

	float feet[3];
	Units::FnvToSm64(centre[0], centre[1], centre[2], feet);
	float terrainY = 0.0f;
	s_status.heightFound = written && HeightUnder(out, feet[0], feet[2], &terrainY);
	s_status.heightDiff = (terrainY - feet[1]) * Units::kFnvPerSm64;
	if (reload)
	{
		// Mario may be in the air or under an overhang: only an empty result is a failure.
		if (!written)
		{
			_WARNING("collision: reload: no terrain around cell (%d, %d) (%u cells, %u failed)", playerCellX, playerCellY,
				s_status.cells, s_status.cellsFailed);
			return false;
		}
		s_status.source = "terrain";
		return true;
	}
	_MESSAGE("collision: terrain %u cells (%u failed, %u blocks rejected), player cell (%d, %d), %u triangles, %u flipped",
		s_status.cells, s_status.cellsFailed, s_status.blocksRejected, playerCellX, playerCellY, written, s_status.flipped);

	if (!s_status.heightFound)
	{
		_WARNING("collision: no terrain triangle under the player at fnv (%.0f, %.0f): terrain NOT used, test floor instead",
			centre[0], centre[1]);
		out.clear();
		return false;
	}
	_MESSAGE("collision: terrain under the player at fnv z %.1f, player feet %.1f (diff %+.1f)",
		terrainY * Units::kFnvPerSm64, centre[2], s_status.heightDiff);
	// Terrain above the feet = the player would be underground, i.e. a bad land frame. Far below is a roof, a bridge
	// or a save in the air: the statics (or the fall to the terrain) catch Mario.
	if (s_status.heightDiff > kHeightTolerance)
	{
		_WARNING("collision: terrain is %+.0f units above the player's feet (tolerance %.0f): terrain NOT used, test floor "
			"instead", s_status.heightDiff, kHeightTolerance);
		out.clear();
		return false;
	}
	if (s_status.heightDiff < -kHeightTolerance)
		_MESSAGE("collision: terrain is %.0f units below the player's feet (roof, bridge or in the air): kept, the statics or "
			"the terrain catch Mario", -s_status.heightDiff);
	s_status.source = "terrain";
	return true;
}

void Collision_DumpState()
{
	Console_Print("SM64FNV: collision %s: %u cells (%u failed), %u triangles, %u flipped; terrain under player %s %+.0f",
		s_status.source, s_status.cells, s_status.cellsFailed, s_status.triangles, s_status.flipped,
		s_status.heightFound ? "at" : "none,", s_status.heightDiff);
	_MESSAGE("state dump collision %s: %u cells (%u failed, %u blocks rejected), %u triangles, %u flipped, height %s %+.1f",
		s_status.source, s_status.cells, s_status.cellsFailed, s_status.blocksRejected, s_status.triangles,
		s_status.flipped, s_status.heightFound ? "found" : "none", s_status.heightDiff);
}

bool Collision_NearestFloor(const std::vector<SM64Surface>& surfaces, const float pos[3], float* outY)
{
	bool found = false;
	for (const SM64Surface& s : surfaces)
	{
		const float ax = (float)s.vertices[0][0], az = (float)s.vertices[0][2];
		const float bx = (float)s.vertices[1][0], bz = (float)s.vertices[1][2];
		const float cx = (float)s.vertices[2][0], cz = (float)s.vertices[2][2];
		// SM64's surface normal y (load_surfaces.c): floors have it positive.
		if ((bz - az) * (cx - bx) - (bx - ax) * (cz - bz) <= 0.0f)
			continue;
		const float det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
		if (fabsf(det) < 1e-3f)
			continue;
		const float l0 = ((bz - cz) * (pos[0] - cx) + (cx - bx) * (pos[2] - cz)) / det;
		const float l1 = ((cz - az) * (pos[0] - cx) + (ax - cx) * (pos[2] - cz)) / det;
		const float l2 = 1.0f - l0 - l1;
		if (l0 < 0.0f || l1 < 0.0f || l2 < 0.0f)
			continue;
		const float y = l0 * s.vertices[0][1] + l1 * s.vertices[1][1] + l2 * s.vertices[2][1];
		if (!found || fabsf(y - pos[1]) < fabsf(*outY - pos[1]))
			*outY = y;
		found = true;
	}
	return found;
}

void Collision_ClearTerrain(const char* source)
{
	s_status = {};
	s_status.source = source;
}
