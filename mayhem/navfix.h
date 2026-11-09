// Fixed navigation-mesh fixtures shared by the Detour/DetourCrowd/DetourTileCache harnesses. They are
// built once from constant geometry with the stock Recast pipeline (as RecastDemo's Sample_SoloMesh
// and Sample_TempObstacles do) and are only read afterwards, so no state leaks between inputs.
#pragma once

#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "DetourTileCache.h"
#include "DetourTileCacheBuilder.h"
#include "Recast.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

enum NavArea
{
	NAV_AREA_GROUND = 0,
	NAV_AREA_WATER = 1,
	NAV_AREA_ROAD = 2,
	NAV_AREA_DOOR = 3,
	NAV_AREA_GRASS = 4,
	NAV_AREA_JUMP = 5,
};

enum NavFlags
{
	NAV_FLAG_WALK = 0x01,
	NAV_FLAG_SWIM = 0x02,
	NAV_FLAG_DOOR = 0x04,
	NAV_FLAG_JUMP = 0x08,
	NAV_FLAG_ALL = 0xffff,
};

static const float kFloorMin = -10.0f;
static const float kFloorMax = 10.0f;

inline void navfixAreaToFlags(const int polyCount, unsigned char* areas, unsigned short* flags, const unsigned char walkableArea)
{
	for (int i = 0; i < polyCount; ++i)
	{
		if (areas[i] == walkableArea)
		{
			areas[i] = NAV_AREA_GROUND;
		}
		if (areas[i] == NAV_AREA_GROUND || areas[i] == NAV_AREA_GRASS || areas[i] == NAV_AREA_ROAD)
		{
			flags[i] = NAV_FLAG_WALK;
		}
		else if (areas[i] == NAV_AREA_WATER)
		{
			flags[i] = NAV_FLAG_SWIM;
		}
		else if (areas[i] == NAV_AREA_DOOR)
		{
			flags[i] = NAV_FLAG_WALK | NAV_FLAG_DOOR;
		}
	}
}

// A 20x20 floor with a raised 4x4 platform in the middle and a 1.0-high step block to one side.
inline void navfixGeometry(std::vector<float>& verts, std::vector<int>& tris)
{
	verts.clear();
	tris.clear();
	auto quad = [&](const float x0, const float z0, const float x1, const float z1, const float y)
	{
		const int base = static_cast<int>(verts.size()) / 3;
		const float v[12] = {x0, y, z0, x1, y, z0, x1, y, z1, x0, y, z1};
		verts.insert(verts.end(), v, v + 12);
		const int t[6] = {base, base + 2, base + 1, base, base + 3, base + 2};
		tris.insert(tris.end(), t, t + 6);
	};
	quad(kFloorMin, kFloorMin, kFloorMax, kFloorMax, 0.0f);
	quad(-2.0f, -2.0f, 2.0f, 2.0f, 0.5f);
	quad(5.0f, 5.0f, 8.0f, 8.0f, 1.0f);
}

struct NavfixConfig
{
	float cs = 0.3f;
	float ch = 0.2f;
	float agentHeight = 2.0f;
	float agentRadius = 0.4f;
	float agentClimb = 0.9f;
	float slope = 45.0f;
};

inline dtNavMesh* navfixBuildSolo()
{
	const NavfixConfig nc;
	std::vector<float> verts;
	std::vector<int> tris;
	navfixGeometry(verts, tris);
	const int nverts = static_cast<int>(verts.size()) / 3;
	const int ntris = static_cast<int>(tris.size()) / 3;

	rcContext ctx;
	rcConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.cs = nc.cs;
	cfg.ch = nc.ch;
	cfg.walkableSlopeAngle = nc.slope;
	cfg.walkableHeight = static_cast<int>(ceilf(nc.agentHeight / nc.ch));
	cfg.walkableClimb = static_cast<int>(floorf(nc.agentClimb / nc.ch));
	cfg.walkableRadius = static_cast<int>(ceilf(nc.agentRadius / nc.cs));
	cfg.maxEdgeLen = 12;
	cfg.maxSimplificationError = 1.3f;
	cfg.minRegionArea = 8 * 8;
	cfg.mergeRegionArea = 20 * 20;
	cfg.maxVertsPerPoly = 6;
	cfg.detailSampleDist = nc.cs * 6.0f;
	cfg.detailSampleMaxError = nc.ch;
	float bmin[3];
	float bmax[3];
	rcCalcBounds(verts.data(), nverts, bmin, bmax);
	bmax[1] += 3.0f;
	rcVcopy(cfg.bmin, bmin);
	rcVcopy(cfg.bmax, bmax);
	rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

	rcHeightfield* solid = rcAllocHeightfield();
	rcCompactHeightfield* chf = rcAllocCompactHeightfield();
	rcContourSet* cset = rcAllocContourSet();
	rcPolyMesh* pmesh = rcAllocPolyMesh();
	rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
	std::vector<unsigned char> triAreas(ntris, 0);
	dtNavMesh* nav = nullptr;

	bool ok = rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch);
	if (ok)
	{
		rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts.data(), nverts, tris.data(), ntris, triAreas.data());
		ok = rcRasterizeTriangles(&ctx, verts.data(), nverts, tris.data(), triAreas.data(), ntris, *solid, cfg.walkableClimb);
	}
	if (ok)
	{
		rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
		rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
		rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);
		ok = rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf) &&
		     rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf);
	}
	if (ok)
	{
		// A water pond on the floor and a road strip, so area costs and flags matter to queries.
		const float pond[12] = {-8.0f, 0.0f, -8.0f, -8.0f, 0.0f, -4.0f, -4.0f, 0.0f, -4.0f, -4.0f, 0.0f, -8.0f};
		rcMarkConvexPolyArea(&ctx, pond, 4, -1.0f, 1.0f, NAV_AREA_WATER, *chf);
		const float road[12] = {3.0f, 0.0f, -8.0f, 3.0f, 0.0f, 3.0f, 4.5f, 0.0f, 3.0f, 4.5f, 0.0f, -8.0f};
		rcMarkConvexPolyArea(&ctx, road, 4, -1.0f, 1.0f, NAV_AREA_ROAD, *chf);
		ok = rcBuildDistanceField(&ctx, *chf) && rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea) &&
		     rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset) &&
		     rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh) &&
		     rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh);
	}
	if (ok)
	{
		navfixAreaToFlags(pmesh->npolys, pmesh->areas, pmesh->flags, RC_WALKABLE_AREA);

		// One bidirectional off-mesh link between the floor and the platform.
		float offVerts[6] = {-3.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f};
		float offRad = 0.6f;
		unsigned char offDir = 1;
		unsigned char offArea = NAV_AREA_JUMP;
		unsigned short offFlags = NAV_FLAG_JUMP;
		unsigned int offId = 1000;

		dtNavMeshCreateParams p;
		memset(&p, 0, sizeof(p));
		p.verts = pmesh->verts;
		p.vertCount = pmesh->nverts;
		p.polys = pmesh->polys;
		p.polyAreas = pmesh->areas;
		p.polyFlags = pmesh->flags;
		p.polyCount = pmesh->npolys;
		p.nvp = pmesh->nvp;
		p.detailMeshes = dmesh->meshes;
		p.detailVerts = dmesh->verts;
		p.detailVertsCount = dmesh->nverts;
		p.detailTris = dmesh->tris;
		p.detailTriCount = dmesh->ntris;
		p.offMeshConVerts = offVerts;
		p.offMeshConRad = &offRad;
		p.offMeshConDir = &offDir;
		p.offMeshConAreas = &offArea;
		p.offMeshConFlags = &offFlags;
		p.offMeshConUserID = &offId;
		p.offMeshConCount = 1;
		p.walkableHeight = nc.agentHeight;
		p.walkableRadius = nc.agentRadius;
		p.walkableClimb = nc.agentClimb;
		rcVcopy(p.bmin, pmesh->bmin);
		rcVcopy(p.bmax, pmesh->bmax);
		p.cs = cfg.cs;
		p.ch = cfg.ch;
		p.buildBvTree = true;

		unsigned char* navData = nullptr;
		int navDataSize = 0;
		if (dtCreateNavMeshData(&p, &navData, &navDataSize))
		{
			nav = dtAllocNavMesh();
			if (!nav || dtStatusFailed(nav->init(navData, navDataSize, DT_TILE_FREE_DATA)))
			{
				dtFree(navData);
				dtFreeNavMesh(nav);
				nav = nullptr;
			}
		}
	}
	rcFreeHeightField(solid);
	rcFreeCompactHeightfield(chf);
	rcFreeContourSet(cset);
	rcFreePolyMesh(pmesh);
	rcFreePolyMeshDetail(dmesh);
	if (!nav)
	{
		abort();
	}
	return nav;
}

inline const dtNavMesh* navfixSolo()
{
	static dtNavMesh* nav = navfixBuildSolo();
	return nav;
}

// ---- tile cache fixture ----

struct NavfixPassthroughCompressor : dtTileCacheCompressor
{
	int maxCompressedSize(const int bufferSize) override { return bufferSize; }
	dtStatus compress(const unsigned char* buffer, const int bufferSize, unsigned char* compressed, const int, int* compressedSize) override
	{
		memcpy(compressed, buffer, bufferSize);
		*compressedSize = bufferSize;
		return DT_SUCCESS;
	}
	dtStatus decompress(const unsigned char* compressed, const int compressedSize, unsigned char* buffer, const int maxBufferSize, int* bufferSize) override
	{
		if (compressedSize > maxBufferSize)
		{
			return DT_FAILURE;
		}
		memcpy(buffer, compressed, compressedSize);
		*bufferSize = compressedSize;
		return DT_SUCCESS;
	}
};

struct NavfixAllocator : dtTileCacheAlloc
{
	void reset() override {}
	void* alloc(const size_t size) override { return dtAlloc(size, DT_ALLOC_TEMP); }
	void free(void* ptr) override { dtFree(ptr); }
};

struct NavfixMeshProcess : dtTileCacheMeshProcess
{
	void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override
	{
		navfixAreaToFlags(params->polyCount, polyAreas, polyFlags, DT_TILECACHE_WALKABLE_AREA);
	}
};

static const int kTcTileSize = 32;
static const int kTcBorder = 4;  // walkableRadius (2) + 2

struct NavfixTileLayer
{
	std::vector<unsigned char> data;
};

struct NavfixTileCacheFixture
{
	dtTileCacheParams params;
	dtNavMeshParams navParams;
	int tw = 0;
	int th = 0;
	std::vector<NavfixTileLayer> layers;
};

// Rasterizes the fixed geometry per tile and stores the compressed layers (Sample_TempObstacles::rasterizeTileLayers).
inline NavfixTileCacheFixture navfixBuildTileCacheFixture()
{
	const NavfixConfig nc;
	std::vector<float> verts;
	std::vector<int> tris;
	navfixGeometry(verts, tris);
	const int nverts = static_cast<int>(verts.size()) / 3;
	const int ntris = static_cast<int>(tris.size()) / 3;
	float bmin[3];
	float bmax[3];
	rcCalcBounds(verts.data(), nverts, bmin, bmax);
	bmax[1] += 3.0f;

	NavfixTileCacheFixture fx;
	int gw = 0;
	int gh = 0;
	rcCalcGridSize(bmin, bmax, nc.cs, &gw, &gh);
	fx.tw = (gw + kTcTileSize - 1) / kTcTileSize;
	fx.th = (gh + kTcTileSize - 1) / kTcTileSize;

	rcConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.cs = nc.cs;
	cfg.ch = nc.ch;
	cfg.walkableSlopeAngle = nc.slope;
	cfg.walkableHeight = static_cast<int>(ceilf(nc.agentHeight / nc.ch));
	cfg.walkableClimb = static_cast<int>(floorf(nc.agentClimb / nc.ch));
	cfg.walkableRadius = static_cast<int>(ceilf(nc.agentRadius / nc.cs));
	cfg.maxEdgeLen = 12;
	cfg.maxSimplificationError = 1.3f;
	cfg.minRegionArea = 8 * 8;
	cfg.mergeRegionArea = 20 * 20;
	cfg.maxVertsPerPoly = 6;
	cfg.tileSize = kTcTileSize;
	cfg.borderSize = kTcBorder;
	cfg.width = cfg.tileSize + cfg.borderSize * 2;
	cfg.height = cfg.tileSize + cfg.borderSize * 2;
	cfg.detailSampleDist = nc.cs * 6.0f;
	cfg.detailSampleMaxError = nc.ch;

	memset(&fx.params, 0, sizeof(fx.params));
	rcVcopy(fx.params.orig, bmin);
	fx.params.cs = nc.cs;
	fx.params.ch = nc.ch;
	fx.params.width = kTcTileSize;
	fx.params.height = kTcTileSize;
	fx.params.walkableHeight = nc.agentHeight;
	fx.params.walkableRadius = nc.agentRadius;
	fx.params.walkableClimb = nc.agentClimb;
	fx.params.maxSimplificationError = 1.3f;
	fx.params.maxTiles = fx.tw * fx.th * 4;
	fx.params.maxObstacles = 32;

	memset(&fx.navParams, 0, sizeof(fx.navParams));
	rcVcopy(fx.navParams.orig, bmin);
	fx.navParams.tileWidth = kTcTileSize * nc.cs;
	fx.navParams.tileHeight = kTcTileSize * nc.cs;
	fx.navParams.maxTiles = fx.tw * fx.th * 4;
	fx.navParams.maxPolys = 1 << 14;

	rcContext ctx;
	NavfixPassthroughCompressor comp;
	const float tcs = kTcTileSize * nc.cs;
	for (int ty = 0; ty < fx.th; ++ty)
	{
		for (int tx = 0; tx < fx.tw; ++tx)
		{
			rcConfig tcfg = cfg;
			tcfg.bmin[0] = bmin[0] + tx * tcs - kTcBorder * nc.cs;
			tcfg.bmin[1] = bmin[1];
			tcfg.bmin[2] = bmin[2] + ty * tcs - kTcBorder * nc.cs;
			tcfg.bmax[0] = bmin[0] + (tx + 1) * tcs + kTcBorder * nc.cs;
			tcfg.bmax[1] = bmax[1];
			tcfg.bmax[2] = bmin[2] + (ty + 1) * tcs + kTcBorder * nc.cs;

			rcHeightfield* solid = rcAllocHeightfield();
			rcCompactHeightfield* chf = rcAllocCompactHeightfield();
			rcHeightfieldLayerSet* lset = rcAllocHeightfieldLayerSet();
			std::vector<unsigned char> triAreas(ntris, 0);
			bool ok = rcCreateHeightfield(&ctx, *solid, tcfg.width, tcfg.height, tcfg.bmin, tcfg.bmax, tcfg.cs, tcfg.ch);
			if (ok)
			{
				rcMarkWalkableTriangles(&ctx, tcfg.walkableSlopeAngle, verts.data(), nverts, tris.data(), ntris, triAreas.data());
				ok = rcRasterizeTriangles(&ctx, verts.data(), nverts, tris.data(), triAreas.data(), ntris, *solid, tcfg.walkableClimb);
			}
			if (ok)
			{
				rcFilterLowHangingWalkableObstacles(&ctx, tcfg.walkableClimb, *solid);
				rcFilterLedgeSpans(&ctx, tcfg.walkableHeight, tcfg.walkableClimb, *solid);
				rcFilterWalkableLowHeightSpans(&ctx, tcfg.walkableHeight, *solid);
				ok = rcBuildCompactHeightfield(&ctx, tcfg.walkableHeight, tcfg.walkableClimb, *solid, *chf) &&
				     rcErodeWalkableArea(&ctx, tcfg.walkableRadius, *chf) &&
				     rcBuildHeightfieldLayers(&ctx, *chf, tcfg.borderSize, tcfg.walkableHeight, *lset);
			}
			if (ok)
			{
				for (int i = 0; i < lset->nlayers && i < 4; ++i)
				{
					const rcHeightfieldLayer* layer = &lset->layers[i];
					dtTileCacheLayerHeader header;
					header.magic = DT_TILECACHE_MAGIC;
					header.version = DT_TILECACHE_VERSION;
					header.tx = tx;
					header.ty = ty;
					header.tlayer = i;
					dtVcopy(header.bmin, layer->bmin);
					dtVcopy(header.bmax, layer->bmax);
					header.width = static_cast<unsigned char>(layer->width);
					header.height = static_cast<unsigned char>(layer->height);
					header.minx = static_cast<unsigned char>(layer->minx);
					header.maxx = static_cast<unsigned char>(layer->maxx);
					header.miny = static_cast<unsigned char>(layer->miny);
					header.maxy = static_cast<unsigned char>(layer->maxy);
					header.hmin = static_cast<unsigned short>(layer->hmin);
					header.hmax = static_cast<unsigned short>(layer->hmax);
					unsigned char* data = nullptr;
					int dataSize = 0;
					if (dtStatusSucceed(dtBuildTileCacheLayer(&comp, &header, layer->heights, layer->areas, layer->cons, &data, &dataSize)))
					{
						NavfixTileLayer tl;
						tl.data.assign(data, data + dataSize);
						dtFree(data);
						tl.data.shrink_to_fit();
						fx.layers.push_back(std::move(tl));
					}
				}
			}
			rcFreeHeightField(solid);
			rcFreeCompactHeightfield(chf);
			rcFreeHeightfieldLayerSet(lset);
		}
	}
	if (fx.layers.empty())
	{
		abort();
	}
	return fx;
}

inline const NavfixTileCacheFixture& navfixTileCacheFixture()
{
	static const NavfixTileCacheFixture fx = navfixBuildTileCacheFixture();
	return fx;
}
