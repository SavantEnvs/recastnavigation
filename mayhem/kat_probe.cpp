// Known-answer probe: dtMergeCorridorStartMoved with fixed arrays, and a dtPathCorridor walked over
// the fixed navigation mesh (navfix.h). Prints one "KAT <name> ok|FAIL" line per checked value;
// test.sh counts these lines, so a neutered or no-op build fails the oracle.
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourPathCorridor.h"
#include "navfix.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;

static void check(const char* name, const bool cond)
{
	std::printf("KAT %s %s\n", name, cond ? "ok" : "FAIL");
	if (!cond)
	{
		++g_fail;
	}
}

static bool near(const float a, const float b, const float eps = 0.05f)
{
	return std::fabs(a - b) <= eps;
}

static void katMerge()
{
	{
		dtPolyRef path[] = {1};
		const int r = dtMergeCorridorStartMoved(path, 1, 1, nullptr, 0);
		check("merge.empty_visited", r == 1 && path[0] == 1);
	}
	{
		dtPolyRef path[] = {1, 2};
		const dtPolyRef visited[] = {1, 2};
		const int r = dtMergeCorridorStartMoved(path, 2, 2, visited, 2);
		check("merge.strip_visited", r == 1 && path[0] == 2 && path[1] == 2);
	}
	{
		dtPolyRef path[] = {1, 2, 0};
		const dtPolyRef visited[] = {1, 2, 3, 4};
		const int r = dtMergeCorridorStartMoved(path, 2, 3, visited, 4);
		check("merge.reverse_order", r == 3 && path[0] == 4 && path[1] == 3 && path[2] == 2);
	}
	{
		dtPolyRef path[] = {1, 2};
		const dtPolyRef visited[] = {3, 4};
		const int r = dtMergeCorridorStartMoved(path, 2, 2, visited, 2);
		check("merge.no_intersection", r == 2 && path[0] == 1 && path[1] == 2);
	}
	{
		dtPolyRef path[] = {1, 2, 0};
		const dtPolyRef visited[] = {1, 3};
		const int r = dtMergeCorridorStartMoved(path, 2, 3, visited, 2);
		check("merge.keep_unvisited", r == 3 && path[0] == 3 && path[1] == 1 && path[2] == 2);
	}
	{
		dtPolyRef path[] = {1, 2};
		const dtPolyRef visited[] = {1, 3};
		const int r = dtMergeCorridorStartMoved(path, 2, 2, visited, 2);
		check("merge.capacity_tail", r == 2 && path[0] == 3 && path[1] == 1);
	}
}

static void katCorridor()
{
	const dtNavMesh* nav = navfixSolo();
	dtNavMeshQuery* query = dtAllocNavMeshQuery();
	check("corridor.query_init", dtStatusSucceed(query->init(nav, 256)));
	dtQueryFilter filter;
	const float ext[3] = {2.0f, 4.0f, 2.0f};

	const float from[3] = {-6.0f, 0.0f, 6.0f};
	dtPolyRef ref = 0;
	float pos[3];
	query->findNearestPoly(from, ext, &filter, &ref, pos);
	check("corridor.nearest", ref != 0 && near(pos[0], -6.0f) && near(pos[2], 6.0f));

	dtPathCorridor corridor;
	check("corridor.init", corridor.init(64));
	corridor.reset(ref, pos);
	check("corridor.reset", corridor.getPathCount() == 1 && corridor.getFirstPoly() == ref && corridor.getLastPoly() == ref);

	const float to[3] = {-5.0f, 0.0f, 6.0f};
	check("corridor.move", corridor.movePosition(to, query, &filter));
	check("corridor.moved_pos", near(corridor.getPos()[0], -5.0f) && near(corridor.getPos()[2], 6.0f));
	check("corridor.moved_height", near(corridor.getPos()[1], 0.0f, 0.3f));
	check("corridor.path_nonempty", corridor.getPathCount() >= 1 && corridor.getPathCount() <= 64);

	const float far[3] = {8.0f, 0.0f, -8.0f};
	dtPolyRef farRef = 0;
	float farPos[3];
	query->findNearestPoly(far, ext, &filter, &farRef, farPos);
	dtPolyRef path[64];
	int npath = 0;
	query->findPath(ref, farRef, pos, farPos, &filter, path, &npath, 64);
	check("corridor.findpath", npath > 1 && path[0] == ref && path[npath - 1] == farRef);
	corridor.setCorridor(farPos, path, npath);
	check("corridor.set", corridor.getPathCount() == npath && corridor.getLastPoly() == farRef && near(corridor.getTarget()[0], farPos[0]));
	dtFreeNavMeshQuery(query);
}

int main()
{
	katMerge();
	katCorridor();
	std::printf("KAT done failed=%d\n", g_fail);
	return g_fail == 0 ? 0 : 1;
}
