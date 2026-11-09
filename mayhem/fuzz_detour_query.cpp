// Detour's dtNavMeshQuery API over a fixed navigation mesh (navfix.h): the input is a program of
// query operations (nearest poly, A*, sliced A*, straight path, raycast, surface walk, Dijkstra
// searches, random points, wall queries, ...) with fuzzer-chosen positions, filters and polygon refs.
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "navfix.h"

#include <fuzzer/FuzzedDataProvider.h>

#include <cstdint>

namespace
{
FuzzedDataProvider* g_fdp = nullptr;

float randomUnit()
{
	return g_fdp->ConsumeProbability<float>();
}

constexpr int kMaxPath = 48;
constexpr int kNumSlots = 6;

struct Slot
{
	dtPolyRef ref = 0;
	float pt[3] = {0, 0, 0};
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	FuzzedDataProvider fdp(data, size);
	g_fdp = &fdp;

	const dtNavMesh* nav = navfixSolo();
	dtNavMeshQuery* query = dtAllocNavMeshQuery();
	if (dtStatusFailed(query->init(nav, fdp.ConsumeIntegralInRange<int>(16, 2048))))
	{
		dtFreeNavMeshQuery(query);
		return 0;
	}

	dtQueryFilter filter;
	filter.setIncludeFlags(NAV_FLAG_ALL);
	filter.setExcludeFlags(0);

	auto pos = [&](float* p, const float lo, const float hi)
	{
		p[0] = fdp.ConsumeFloatingPointInRange<float>(lo, hi);
		p[1] = fdp.ConsumeFloatingPointInRange<float>(-1.0f, 3.0f);
		p[2] = fdp.ConsumeFloatingPointInRange<float>(lo, hi);
	};

	Slot slots[kNumSlots];
	dtPolyRef path[kMaxPath];
	int pathCount = 0;
	float pathStart[3] = {0, 0, 0};
	float pathEnd[3] = {0, 0, 0};
	bool slicedActive = false;

	auto slotIdx = [&]() { return fdp.ConsumeIntegralInRange<int>(0, kNumSlots - 1); };

	const int nops = fdp.ConsumeIntegralInRange<int>(1, 24);
	for (int op = 0; op < nops && fdp.remaining_bytes() > 0; ++op)
	{
		switch (fdp.ConsumeIntegralInRange<int>(0, 16))
		{
		case 0:
		{
			float c[3];
			float ext[3];
			pos(c, -13.0f, 13.0f);
			ext[0] = fdp.ConsumeFloatingPointInRange<float>(0.0f, 6.0f);
			ext[1] = fdp.ConsumeFloatingPointInRange<float>(0.0f, 6.0f);
			ext[2] = fdp.ConsumeFloatingPointInRange<float>(0.0f, 6.0f);
			Slot& s = slots[slotIdx()];
			bool over = false;
			if (fdp.ConsumeBool())
			{
				query->findNearestPoly(c, ext, &filter, &s.ref, s.pt, &over);
			}
			else
			{
				query->findNearestPoly(c, ext, &filter, &s.ref, s.pt);
			}
			break;
		}
		case 1:
		{
			const Slot& a = slots[slotIdx()];
			const Slot& b = slots[slotIdx()];
			int n = 0;
			dtPolyRef out[kMaxPath];
			if (dtStatusSucceed(query->findPath(a.ref, b.ref, a.pt, b.pt, &filter, out, &n, kMaxPath)) && n > 0)
			{
				memcpy(path, out, sizeof(dtPolyRef) * n);
				pathCount = n;
				dtVcopy(pathStart, a.pt);
				dtVcopy(pathEnd, b.pt);
			}
			break;
		}
		case 2:
		{
			if (pathCount > 0)
			{
				float sp[kMaxPath * 3];
				unsigned char sf[kMaxPath];
				dtPolyRef sr[kMaxPath];
				int sn = 0;
				const int opts = fdp.ConsumeIntegralInRange<int>(0, 3);
				query->findStraightPath(pathStart, pathEnd, path, pathCount, sp, sf, sr, &sn, fdp.ConsumeIntegralInRange<int>(1, kMaxPath), opts);
			}
			break;
		}
		case 3:
		{
			const Slot& a = slots[slotIdx()];
			float end[3];
			pos(end, -13.0f, 13.0f);
			float res[3];
			dtPolyRef visited[kMaxPath];
			int nv = 0;
			query->moveAlongSurface(a.ref, a.pt, end, &filter, res, visited, &nv, fdp.ConsumeIntegralInRange<int>(1, kMaxPath));
			break;
		}
		case 4:
		{
			const Slot& a = slots[slotIdx()];
			float end[3];
			pos(end, -13.0f, 13.0f);
			if (fdp.ConsumeBool())
			{
				float t = 0;
				float normal[3];
				dtPolyRef rp[kMaxPath];
				int rn = 0;
				query->raycast(a.ref, a.pt, end, &filter, &t, normal, rp, &rn, kMaxPath);
			}
			else
			{
				dtRaycastHit hit;
				dtPolyRef rp[kMaxPath];
				hit.path = rp;
				hit.maxPath = kMaxPath;
				query->raycast(a.ref, a.pt, end, &filter, fdp.ConsumeBool() ? DT_RAYCAST_USE_COSTS : 0, &hit);
			}
			break;
		}
		case 5:
		{
			const Slot& a = slots[slotIdx()];
			dtPolyRef rr[kMaxPath];
			dtPolyRef rp[kMaxPath];
			float rc[kMaxPath];
			int rn = 0;
			query->findPolysAroundCircle(a.ref, a.pt, fdp.ConsumeFloatingPointInRange<float>(0.0f, 12.0f), &filter, rr, rp, rc, &rn, kMaxPath);
			break;
		}
		case 6:
		{
			const Slot& a = slots[slotIdx()];
			const float hx = fdp.ConsumeFloatingPointInRange<float>(0.1f, 8.0f);
			const float hz = fdp.ConsumeFloatingPointInRange<float>(0.1f, 8.0f);
			const float shape[12] = {a.pt[0] - hx, a.pt[1], a.pt[2] - hz, a.pt[0] - hx, a.pt[1], a.pt[2] + hz,
			                         a.pt[0] + hx, a.pt[1], a.pt[2] + hz, a.pt[0] + hx, a.pt[1], a.pt[2] - hz};
			dtPolyRef rr[kMaxPath];
			dtPolyRef rp[kMaxPath];
			float rc[kMaxPath];
			int rn = 0;
			query->findPolysAroundShape(a.ref, shape, 4, &filter, rr, rp, rc, &rn, kMaxPath);
			break;
		}
		case 7:
		{
			const Slot& a = slots[slotIdx()];
			dtPolyRef rr[kMaxPath];
			dtPolyRef rp[kMaxPath];
			int rn = 0;
			query->findLocalNeighbourhood(a.ref, a.pt, fdp.ConsumeFloatingPointInRange<float>(0.0f, 8.0f), &filter, rr, rp, &rn, kMaxPath);
			break;
		}
		case 8:
		{
			const Slot& a = slots[slotIdx()];
			float dist = 0;
			float hp[3];
			float hn[3];
			query->findDistanceToWall(a.ref, a.pt, fdp.ConsumeFloatingPointInRange<float>(0.0f, 12.0f), &filter, &dist, hp, hn);
			break;
		}
		case 9:
		{
			const Slot& a = slots[slotIdx()];
			float sv[6 * 3 * 2];
			dtPolyRef sr[6];
			int sn = 0;
			query->getPolyWallSegments(a.ref, &filter, sv, sr, &sn, 6);
			break;
		}
		case 10:
		{
			dtPolyRef r = 0;
			float p[3];
			if (fdp.ConsumeBool())
			{
				query->findRandomPoint(&filter, randomUnit, &r, p);
			}
			else
			{
				const Slot& a = slots[slotIdx()];
				query->findRandomPointAroundCircle(a.ref, a.pt, fdp.ConsumeFloatingPointInRange<float>(0.0f, 12.0f), &filter, randomUnit, &r, p);
			}
			break;
		}
		case 11:
		{
			const Slot& a = slots[slotIdx()];
			float p[3];
			pos(p, -13.0f, 13.0f);
			float out[3];
			bool over = false;
			float h = 0;
			query->closestPointOnPoly(a.ref, p, out, &over);
			query->closestPointOnPolyBoundary(a.ref, p, out);
			query->getPolyHeight(a.ref, p, &h);
			break;
		}
		case 12:
		{
			const Slot& a = slots[slotIdx()];
			const Slot& b = slots[slotIdx()];
			if (dtStatusFailed(query->initSlicedFindPath(a.ref, b.ref, a.pt, b.pt, &filter, fdp.ConsumeBool() ? DT_FINDPATH_ANY_ANGLE : 0)))
			{
				slicedActive = false;
				break;
			}
			slicedActive = true;
			int done = 0;
			query->updateSlicedFindPath(fdp.ConsumeIntegralInRange<int>(0, 40), &done);
			if (fdp.ConsumeBool())
			{
				int n = 0;
				dtPolyRef out[kMaxPath];
				if (fdp.ConsumeBool())
				{
					query->finalizeSlicedFindPath(out, &n, kMaxPath);
				}
				else
				{
					query->finalizeSlicedFindPathPartial(path, pathCount, out, &n, kMaxPath);
				}
				slicedActive = false;
			}
			break;
		}
		case 13:
		{
			float c[3];
			float ext[3];
			pos(c, -13.0f, 13.0f);
			ext[0] = fdp.ConsumeFloatingPointInRange<float>(0.0f, 8.0f);
			ext[1] = fdp.ConsumeFloatingPointInRange<float>(0.0f, 8.0f);
			ext[2] = fdp.ConsumeFloatingPointInRange<float>(0.0f, 8.0f);
			dtPolyRef polys[kMaxPath];
			int n = 0;
			query->queryPolygons(c, ext, &filter, polys, &n, kMaxPath);
			break;
		}
		case 14:
		{
			filter.setIncludeFlags(fdp.ConsumeIntegral<unsigned short>());
			filter.setExcludeFlags(fdp.ConsumeIntegral<unsigned short>());
			for (int i = 0; i < 4; ++i)
			{
				filter.setAreaCost(fdp.ConsumeIntegralInRange<int>(0, DT_MAX_AREAS - 1), fdp.ConsumeFloatingPointInRange<float>(1.0f, 20.0f));
			}
			break;
		}
		case 15:
		{
			// Polygon references from outside (stale, out of range, wrong salt) must be rejected, not trusted.
			const dtPolyRef r = fdp.ConsumeIntegral<dtPolyRef>();
			const dtMeshTile* tile = nullptr;
			const dtPoly* poly = nullptr;
			nav->getTileAndPolyByRef(r, &tile, &poly);
			nav->isValidPolyRef(r);
			unsigned char area = 0;
			nav->getPolyArea(r, &area);
			float a[3];
			float b[3];
			nav->getOffMeshConnectionPolyEndPoints(r, fdp.ConsumeIntegral<dtPolyRef>(), a, b);
			float p[3];
			pos(p, -13.0f, 13.0f);
			float out[3];
			query->closestPointOnPolyBoundary(r, p, out);
			query->isValidPolyRef(r, &filter);
			break;
		}
		default:
		{
			if (slicedActive)
			{
				int done = 0;
				query->updateSlicedFindPath(fdp.ConsumeIntegralInRange<int>(0, 40), &done);
			}
			break;
		}
		}
	}

	dtFreeNavMeshQuery(query);
	g_fdp = nullptr;
	return 0;
}
