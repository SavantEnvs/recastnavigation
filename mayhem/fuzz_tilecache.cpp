// DetourTileCache's dynamic obstacle pipeline: a tile cache over fixed compressed layers (navfix.h)
// feeds a dtNavMesh; the input adds and removes cylinder, box and oriented-box obstacles, advances
// time (obstacle rasterization, layer decompression, region/contour/poly-mesh rebuild, tile
// replacement), and queries the resulting navigation mesh.
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourTileCache.h"
#include "navfix.h"

#include <fuzzer/FuzzedDataProvider.h>

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	FuzzedDataProvider fdp(data, size);

	const NavfixTileCacheFixture& fx = navfixTileCacheFixture();
	NavfixPassthroughCompressor comp;
	NavfixAllocator alloc;
	NavfixMeshProcess proc;

	dtTileCache* tc = dtAllocTileCache();
	dtNavMesh* nav = dtAllocNavMesh();
	if (dtStatusFailed(tc->init(&fx.params, &alloc, &comp, &proc)) || dtStatusFailed(nav->init(&fx.navParams)))
	{
		dtFreeNavMesh(nav);
		dtFreeTileCache(tc);
		return 0;
	}
	for (const NavfixTileLayer& layer : fx.layers)
	{
		// Not DT_COMPRESSEDTILE_FREE_DATA: the fixture owns the bytes and the cache only reads them.
		tc->addTile(const_cast<unsigned char*>(layer.data.data()), static_cast<int>(layer.data.size()), 0, nullptr);
	}
	for (int y = 0; y < fx.th; ++y)
	{
		for (int x = 0; x < fx.tw; ++x)
		{
			tc->buildNavMeshTilesAt(x, y, nav);
		}
	}

	dtObstacleRef refs[16];
	int nrefs = 0;
	auto point = [&](float* p)
	{
		p[0] = fdp.ConsumeFloatingPointInRange<float>(kFloorMin - 1.0f, kFloorMax + 1.0f);
		p[1] = fdp.ConsumeFloatingPointInRange<float>(-1.0f, 2.0f);
		p[2] = fdp.ConsumeFloatingPointInRange<float>(kFloorMin - 1.0f, kFloorMax + 1.0f);
	};

	const int nops = fdp.ConsumeIntegralInRange<int>(1, 40);
	for (int op = 0; op < nops && fdp.remaining_bytes() > 0; ++op)
	{
		switch (fdp.ConsumeIntegralInRange<int>(0, 6))
		{
		case 0:
		{
			float p[3];
			point(p);
			dtObstacleRef r = 0;
			if (dtStatusSucceed(tc->addObstacle(p, fdp.ConsumeFloatingPointInRange<float>(0.1f, 4.0f), fdp.ConsumeFloatingPointInRange<float>(0.1f, 4.0f), &r)) && nrefs < 16)
			{
				refs[nrefs++] = r;
			}
			break;
		}
		case 1:
		{
			float lo[3];
			point(lo);
			const float hi[3] = {lo[0] + fdp.ConsumeFloatingPointInRange<float>(0.1f, 5.0f), lo[1] + fdp.ConsumeFloatingPointInRange<float>(0.1f, 4.0f),
			                     lo[2] + fdp.ConsumeFloatingPointInRange<float>(0.1f, 5.0f)};
			dtObstacleRef r = 0;
			if (dtStatusSucceed(tc->addBoxObstacle(lo, hi, &r)) && nrefs < 16)
			{
				refs[nrefs++] = r;
			}
			break;
		}
		case 2:
		{
			float c[3];
			point(c);
			const float half[3] = {fdp.ConsumeFloatingPointInRange<float>(0.1f, 4.0f), fdp.ConsumeFloatingPointInRange<float>(0.1f, 3.0f),
			                       fdp.ConsumeFloatingPointInRange<float>(0.1f, 4.0f)};
			dtObstacleRef r = 0;
			if (dtStatusSucceed(tc->addBoxObstacle(c, half, fdp.ConsumeFloatingPointInRange<float>(-7.0f, 7.0f), &r)) && nrefs < 16)
			{
				refs[nrefs++] = r;
			}
			break;
		}
		case 3:
		{
			if (nrefs > 0)
			{
				const int i = fdp.ConsumeIntegralInRange<int>(0, nrefs - 1);
				tc->removeObstacle(refs[i]);
				refs[i] = refs[--nrefs];
			}
			else
			{
				// A reference the cache never issued must be rejected.
				tc->removeObstacle(fdp.ConsumeIntegral<dtObstacleRef>());
			}
			break;
		}
		case 4:
		{
			const int steps = fdp.ConsumeIntegralInRange<int>(1, 6);
			for (int i = 0; i < steps; ++i)
			{
				bool upToDate = false;
				tc->update(fdp.ConsumeFloatingPointInRange<float>(0.0f, 1.0f), nav, &upToDate);
			}
			break;
		}
		case 5:
		{
			float lo[3];
			point(lo);
			const float hi[3] = {lo[0] + 6.0f, lo[1] + 4.0f, lo[2] + 6.0f};
			dtCompressedTileRef tiles[8];
			int n = 0;
			tc->queryTiles(lo, hi, tiles, &n, 8);
			break;
		}
		default:
		{
			dtNavMeshQuery* q = dtAllocNavMeshQuery();
			if (dtStatusSucceed(q->init(nav, 512)))
			{
				dtQueryFilter f;
				const float ext[3] = {2.0f, 4.0f, 2.0f};
				float a[3];
				float b[3];
				point(a);
				point(b);
				dtPolyRef ra = 0;
				dtPolyRef rb = 0;
				float pa[3];
				float pb[3];
				q->findNearestPoly(a, ext, &f, &ra, pa);
				q->findNearestPoly(b, ext, &f, &rb, pb);
				if (ra && rb)
				{
					dtPolyRef path[64];
					int n = 0;
					q->findPath(ra, rb, pa, pb, &f, path, &n, 64);
				}
			}
			dtFreeNavMeshQuery(q);
			break;
		}
		}
	}

	dtFreeNavMesh(nav);
	dtFreeTileCache(tc);
	return 0;
}
