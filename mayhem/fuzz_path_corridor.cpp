// DetourCrowd's dtPathCorridor over a fixed navigation mesh (navfix.h) with a fuzzer-chosen, small
// path-buffer capacity: the corridor is reset on a polygon, loaded with a path, and moved with
// movePosition / moveTargetPosition, which merge the polygons a surface walk visited into the path.
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourPathCorridor.h"
#include "navfix.h"

#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	FuzzedDataProvider fdp(data, size);

	const dtNavMesh* nav = navfixSolo();
	dtNavMeshQuery* query = dtAllocNavMeshQuery();
	if (dtStatusFailed(query->init(nav, 256)))
	{
		dtFreeNavMeshQuery(query);
		return 0;
	}
	dtQueryFilter filter;
	const float ext[3] = {2.0f, 4.0f, 2.0f};

	auto point = [&](float* p)
	{
		p[0] = fdp.ConsumeFloatingPointInRange<float>(-11.0f, 11.0f);
		p[1] = fdp.ConsumeFloatingPointInRange<float>(-0.5f, 2.0f);
		p[2] = fdp.ConsumeFloatingPointInRange<float>(-11.0f, 11.0f);
	};

	const int maxPath = fdp.ConsumeIntegralInRange<int>(1, 24);
	dtPathCorridor corridor;
	if (!corridor.init(maxPath))
	{
		dtFreeNavMeshQuery(query);
		return 0;
	}

	float start[3];
	point(start);
	dtPolyRef startRef = 0;
	float startPos[3];
	query->findNearestPoly(start, ext, &filter, &startRef, startPos);
	if (startRef)
	{
		corridor.reset(startRef, startPos);

		const int nops = fdp.ConsumeIntegralInRange<int>(1, 24);
		for (int op = 0; op < nops && fdp.remaining_bytes() > 0; ++op)
		{
			float p[3];
			point(p);
			switch (fdp.ConsumeIntegralInRange<int>(0, 3))
			{
			case 0:
				corridor.movePosition(p, query, &filter);
				break;
			case 1:
				corridor.moveTargetPosition(p, query, &filter);
				break;
			case 2:
			{
				dtPolyRef endRef = 0;
				float endPos[3];
				query->findNearestPoly(p, ext, &filter, &endRef, endPos);
				if (endRef)
				{
					dtPolyRef path[64];
					int npath = 0;
					query->findPath(corridor.getFirstPoly(), endRef, corridor.getPos(), endPos, &filter, path, &npath, 64);
					npath = std::min(npath, maxPath);
					if (npath > 0)
					{
						corridor.setCorridor(endPos, path, npath);
					}
				}
				break;
			}
			default:
			{
				float verts[3 * 4];
				unsigned char flags[4];
				dtPolyRef polys[4];
				corridor.findCorners(verts, flags, polys, 4, query, &filter);
				break;
			}
			}
		}
	}

	dtFreeNavMeshQuery(query);
	return 0;
}
