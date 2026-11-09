// The Recast voxelization pipeline end to end, as RecastDemo's Sample_SoloMesh::build() runs it,
// over a fuzzer-chosen triangle soup and build configuration, finishing with dtCreateNavMeshData and
// dtNavMesh::init.
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "Recast.h"

#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	FuzzedDataProvider fdp(data, size);

	// Ranges follow the documented rcConfig contracts (Recast.h): cs, ch > 0; walkableHeight >= 3;
	// walkableClimb >= 0; walkableRadius >= 0; maxVertsPerPoly >= 3.
	rcConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.cs = fdp.PickValueInArray({0.2f, 0.3f, 0.5f, 1.0f});
	cfg.ch = fdp.PickValueInArray({0.1f, 0.2f, 0.4f});
	cfg.walkableSlopeAngle = fdp.ConsumeFloatingPointInRange<float>(0.0f, 90.0f);
	const float agentHeight = fdp.ConsumeFloatingPointInRange<float>(0.5f, 3.0f);
	const float agentClimb = fdp.ConsumeFloatingPointInRange<float>(0.0f, 1.5f);
	const float agentRadius = fdp.ConsumeFloatingPointInRange<float>(0.0f, 1.0f);
	cfg.walkableHeight = std::max(3, static_cast<int>(ceilf(agentHeight / cfg.ch)));
	cfg.walkableClimb = static_cast<int>(floorf(agentClimb / cfg.ch));
	cfg.walkableRadius = static_cast<int>(ceilf(agentRadius / cfg.cs));
	cfg.maxEdgeLen = fdp.ConsumeIntegralInRange<int>(0, 64);
	cfg.maxSimplificationError = fdp.ConsumeFloatingPointInRange<float>(0.0f, 4.0f);
	cfg.minRegionArea = fdp.ConsumeIntegralInRange<int>(0, 100);
	cfg.mergeRegionArea = fdp.ConsumeIntegralInRange<int>(0, 400);
	cfg.maxVertsPerPoly = fdp.ConsumeIntegralInRange<int>(3, 6);
	const bool detail = fdp.ConsumeBool();
	cfg.detailSampleDist = detail ? cfg.cs * fdp.ConsumeFloatingPointInRange<float>(0.9f, 8.0f) : 0.0f;
	cfg.detailSampleMaxError = cfg.ch * fdp.ConsumeFloatingPointInRange<float>(0.0f, 4.0f);
	const int partition = fdp.ConsumeIntegralInRange<int>(0, 2);
	const bool filterLow = fdp.ConsumeBool();
	const bool filterLedge = fdp.ConsumeBool();
	const bool filterLowSpans = fdp.ConsumeBool();

	// Triangle soup on a quarter-unit lattice inside [-16, 16] x [-4, 12] x [-16, 16].
	const int ntris = fdp.ConsumeIntegralInRange<int>(1, 48);
	std::vector<float> verts;
	std::vector<int> tris;
	verts.reserve(ntris * 9);
	for (int i = 0; i < ntris; ++i)
	{
		for (int k = 0; k < 3; ++k)
		{
			const float x = fdp.ConsumeIntegralInRange<int>(-64, 64) * 0.25f;
			const float y = fdp.ConsumeIntegralInRange<int>(-16, 48) * 0.25f;
			const float z = fdp.ConsumeIntegralInRange<int>(-64, 64) * 0.25f;
			verts.push_back(x);
			verts.push_back(y);
			verts.push_back(z);
			tris.push_back(i * 3 + k);
		}
	}
	const int nverts = static_cast<int>(verts.size()) / 3;

	rcContext ctx;
	rcCalcBounds(verts.data(), nverts, cfg.bmin, cfg.bmax);
	rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

	rcHeightfield* solid = rcAllocHeightfield();
	rcCompactHeightfield* chf = rcAllocCompactHeightfield();
	rcContourSet* cset = rcAllocContourSet();
	rcPolyMesh* pmesh = rcAllocPolyMesh();
	rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
	std::vector<unsigned char> triAreas(ntris, 0);

	bool ok = rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch);
	if (ok)
	{
		rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts.data(), nverts, tris.data(), ntris, triAreas.data());
		ok = rcRasterizeTriangles(&ctx, verts.data(), nverts, tris.data(), triAreas.data(), ntris, *solid, cfg.walkableClimb);
	}
	if (ok)
	{
		if (filterLow)
		{
			rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
		}
		if (filterLedge)
		{
			rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
		}
		if (filterLowSpans)
		{
			rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);
		}
		ok = rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf) &&
		     rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf);
	}
	if (ok)
	{
		const int nconvex = fdp.ConsumeIntegralInRange<int>(0, 2);
		for (int i = 0; i < nconvex; ++i)
		{
			float poly[3 * 6];
			const int np = fdp.ConsumeIntegralInRange<int>(3, 6);
			for (int k = 0; k < np; ++k)
			{
				poly[k * 3 + 0] = fdp.ConsumeIntegralInRange<int>(-64, 64) * 0.25f;
				poly[k * 3 + 1] = 0.0f;
				poly[k * 3 + 2] = fdp.ConsumeIntegralInRange<int>(-64, 64) * 0.25f;
			}
			const float hmin = fdp.ConsumeIntegralInRange<int>(-16, 16) * 0.25f;
			const float hmax = hmin + fdp.ConsumeIntegralInRange<int>(0, 48) * 0.25f;
			rcMarkConvexPolyArea(&ctx, poly, np, hmin, hmax, static_cast<unsigned char>(fdp.ConsumeIntegralInRange<int>(0, 63)), *chf);
		}
		if (partition == 0)
		{
			ok = rcBuildDistanceField(&ctx, *chf) && rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea);
		}
		else if (partition == 1)
		{
			ok = rcBuildRegionsMonotone(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea);
		}
		else
		{
			ok = rcBuildLayerRegions(&ctx, *chf, 0, cfg.minRegionArea);
		}
	}
	if (ok)
	{
		ok = rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset) &&
		     rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh) &&
		     rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh);
	}
	if (ok)
	{
		for (int i = 0; i < pmesh->npolys; ++i)
		{
			pmesh->flags[i] = pmesh->areas[i] == RC_NULL_AREA ? 0 : 1;
		}
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
		p.walkableHeight = agentHeight;
		p.walkableRadius = agentRadius;
		p.walkableClimb = agentClimb;
		rcVcopy(p.bmin, pmesh->bmin);
		rcVcopy(p.bmax, pmesh->bmax);
		p.cs = cfg.cs;
		p.ch = cfg.ch;
		p.buildBvTree = fdp.ConsumeBool();
		// Detour's own build step only accepts up to DT_VERTS_PER_POLYGON vertices per polygon.
		unsigned char* navData = nullptr;
		int navDataSize = 0;
		if (pmesh->npolys > 0 && dtCreateNavMeshData(&p, &navData, &navDataSize))
		{
			dtNavMesh* nav = dtAllocNavMesh();
			if (nav && dtStatusSucceed(nav->init(navData, navDataSize, DT_TILE_FREE_DATA)))
			{
				dtFreeNavMesh(nav);
			}
			else
			{
				dtFree(navData);
				dtFreeNavMesh(nav);
			}
		}
	}

	rcFreeHeightField(solid);
	rcFreeCompactHeightfield(chf);
	rcFreeContourSet(cset);
	rcFreePolyMesh(pmesh);
	rcFreePolyMeshDetail(dmesh);
	return 0;
}
