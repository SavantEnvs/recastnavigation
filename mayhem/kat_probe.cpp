// Known-answer probe: drives the RecastDemo loaders, the Recast->Detour build, Detour queries,
// DetourCrowd and DetourTileCache with fixed inputs and prints one "KAT <name> ok|FAIL" line per
// checked value. test.sh counts these lines, so a neutered or no-op build fails the oracle.
#include "DetourCommon.h"
#include "DetourCrowd.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "InputGeom.h"
#include "Recast.h"
#include "demo_fileio.h"
#include "navfix.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#define KAT_POLY_COUNT 29
#define KAT_TC_LAYERS 4
#define KAT_TC_TW 3
#define KAT_TC_TH 3

static int g_fail = 0;

static void check(const char* name, const bool cond)
{
	std::printf("KAT %s %s\n", name, cond ? "ok" : "FAIL");
	if (!cond)
	{
		++g_fail;
	}
}

static bool near(const float a, const float b, const float eps = 0.01f)
{
	return std::fabs(a - b) <= eps;
}

static void katObj()
{
	std::string obj = "v 0 0 0\nv 10 0 0\nv 0 0 10\nv 10 0 10\nf 1 3 2\nf 2/1/1 3/2/2 4/3/3\n";
	Mesh m;
	m.readFromObj(obj.data(), obj.size());
	check("obj.verts", m.getVertCount() == 4);
	check("obj.tris", m.getTriCount() == 2);
	check("obj.tri0", m.tris[0] == 0 && m.tris[1] == 2 && m.tris[2] == 1);
	check("obj.tri1", m.tris[3] == 1 && m.tris[4] == 2 && m.tris[5] == 3);
	check("obj.vert3", near(m.verts[9], 10.0f) && near(m.verts[10], 0.0f) && near(m.verts[11], 10.0f));
	check("obj.normal", m.normals.size() == 6 && near(m.normals[1], 1.0f));

	std::string quad = "v 0 0 0\nv 4 0 0\nv 4 0 4\nv 0 0 4\nf 1 4 3 2\n";
	Mesh q;
	q.readFromObj(quad.data(), quad.size());
	check("obj.quad", q.getTriCount() == 2);
}

static void katGset()
{
	const std::string gset =
		"s 0.3 0.2 2 0.6 0.9 45 8 20 12 1.3 6 6 1 0 -10 -1 -10 10 3 10 0\n"
		"f mesh.obj\n"
		"c 0 0 0 1 0 1 0.6 1 5 7\n"
		"v 3 2 0 1\n0 0 0\n4 0 0\n4 0 4\n";
	demoFileSetInput("input.gset", reinterpret_cast<const uint8_t*>(gset.data()), gset.size());
	rcContext ctx;
	InputGeom geom;
	check("gset.load", geom.load(&ctx, "input.gset"));
	check("gset.offmesh", geom.offmeshConnVerts.size() == 6 && near(geom.offmeshConnRadius[0], 0.6f) &&
	                          geom.offmeshConnArea[0] == 5 && geom.offmeshConnFlags[0] == 7 && geom.offmeshConnBidirectional[0] == 1 &&
	                          near(geom.offmeshConnVerts[3], 1.0f) && near(geom.offmeshConnVerts[5], 1.0f));
	check("gset.convex", geom.convexVolumes.size() == 1 && geom.convexVolumes[0].nverts == 3 && geom.convexVolumes[0].area == 2 &&
	                         near(geom.convexVolumes[0].hmax, 1.0f) && near(geom.convexVolumes[0].verts[3], 4.0f));
	const BuildSettings* bs = geom.getBuildSettings();
	check("gset.settings", bs && near(bs->cellSize, 0.3f) && bs->vertsPerPoly == 6 && near(bs->agentRadius, 0.6f) && near(bs->navMeshBMax[1], 3.0f));
	check("gset.mesh", geom.mesh.getVertCount() == 3 && geom.mesh.getTriCount() == 1);
	float src[3] = {2.0f, 5.0f, 2.0f};
	float dst[3] = {2.0f, -5.0f, 2.0f};
	float t = 0.0f;
	check("gset.raycast", geom.raycastMesh(src, dst, t) && near(t, 0.5f));
}

static void katNavmesh()
{
	const dtNavMesh* nav = navfixSolo();
	dtNavMeshQuery* q = dtAllocNavMeshQuery();
	check("nav.query.init", dtStatusSucceed(q->init(nav, 2048)));
	dtQueryFilter filter;
	const float ext[3] = {0.5f, 2.0f, 0.5f};

	auto nearest = [&](const float x, const float y, const float z, dtPolyRef& ref, float* pt)
	{
		const float c[3] = {x, y, z};
		ref = 0;
		q->findNearestPoly(c, ext, &filter, &ref, pt);
	};
	dtPolyRef floorRef;
	dtPolyRef platformRef;
	dtPolyRef pondRef;
	dtPolyRef roadRef;
	float floorPt[3];
	float platformPt[3];
	float pondPt[3];
	float roadPt[3];
	nearest(-9.0f, 0.0f, 9.0f, floorRef, floorPt);
	nearest(0.0f, 0.5f, 0.0f, platformRef, platformPt);
	nearest(-6.0f, 0.0f, -6.0f, pondRef, pondPt);
	nearest(3.7f, 0.0f, 0.0f, roadRef, roadPt);
	check("nav.floor", floorRef != 0 && near(floorPt[1], 0.0f, 0.25f));
	check("nav.platform", platformRef != 0 && platformPt[1] > 0.4f && platformPt[1] < 0.9f);
	unsigned char area = 255;
	nav->getPolyArea(pondRef, &area);
	check("nav.pond.area", pondRef != 0 && area == NAV_AREA_WATER);
	nav->getPolyArea(roadRef, &area);
	check("nav.road.area", roadRef != 0 && area == NAV_AREA_ROAD);
	nav->getPolyArea(floorRef, &area);
	check("nav.ground.area", area == NAV_AREA_GROUND);

	// Nothing walkable far outside the floor.
	dtPolyRef none = 1;
	float nonePt[3];
	nearest(40.0f, 0.0f, 40.0f, none, nonePt);
	check("nav.outside", none == 0);

	// Floor corner to opposite corner: a path exists, and its straight form runs from start to end.
	dtPolyRef endRef;
	float endPt[3];
	nearest(9.0f, 0.0f, -9.0f, endRef, endPt);
	dtPolyRef path[256];
	int pathCount = 0;
	const dtStatus ps = q->findPath(floorRef, endRef, floorPt, endPt, &filter, path, &pathCount, 256);
	check("nav.path", dtStatusSucceed(ps) && pathCount > 1 && path[0] == floorRef && path[pathCount - 1] == endRef);
	float sp[64 * 3];
	unsigned char sf[64];
	dtPolyRef sr[64];
	int sn = 0;
	q->findStraightPath(floorPt, endPt, path, pathCount, sp, sf, sr, &sn, 64);
	check("nav.straight", sn >= 2 && near(sp[0], floorPt[0]) && near(sp[(sn - 1) * 3], endPt[0]) && near(sp[(sn - 1) * 3 + 2], endPt[2]) &&
	                          (sf[sn - 1] & DT_STRAIGHTPATH_END));

	// Free line of sight along the floor edge, blocked going out of the mesh.
	float t = 0;
	float n[3];
	dtPolyRef rp[16];
	int rn = 0;
	const float e1[3] = {9.0f, 0.0f, 9.0f};
	q->raycast(floorRef, floorPt, e1, &filter, &t, n, rp, &rn, 16);
	check("nav.ray.free", t == FLT_MAX);
	const float e2[3] = {-30.0f, 0.0f, 9.0f};
	q->raycast(floorRef, floorPt, e2, &filter, &t, n, rp, &rn, 16);
	check("nav.ray.blocked", t < 1.0f && t > 0.0f);

	// An excluded area is routed around: with water excluded no polygon is found inside the pond.
	dtQueryFilter noWater;
	noWater.setExcludeFlags(NAV_FLAG_SWIM);
	dtPolyRef pr = 1;
	float pp[3];
	const float pc[3] = {-6.0f, 0.0f, -6.0f};
	const float tight[3] = {0.5f, 1.0f, 0.5f};
	q->findNearestPoly(pc, tight, &noWater, &pr, pp);
	check("nav.filter.water", dtVdist2D(pp, pc) > 1.0f);
	dtPolyRef wr = 0;
	q->findNearestPoly(pc, tight, &filter, &wr, pp);
	check("nav.filter.water.incl", wr == pondRef && dtVdist2D(pp, pc) < 0.2f);

	float h = -1;
	q->getPolyHeight(platformRef, platformPt, &h);
	check("nav.height", h > 0.4f && h < 0.9f);
	dtFreeNavMeshQuery(q);

	const dtMeshTile* tile = nav->getTile(0);
	check("nav.counts", tile && tile->header && tile->header->polyCount == KAT_POLY_COUNT && tile->header->offMeshConCount == 1);
}

static void katCrowd()
{
	dtNavMesh* nav = const_cast<dtNavMesh*>(navfixSolo());
	dtCrowd* crowd = dtAllocCrowd();
	check("crowd.init", crowd->init(4, 1.0f, nav));
	dtCrowdAgentParams ap;
	memset(&ap, 0, sizeof(ap));
	ap.radius = 0.4f;
	ap.height = 2.0f;
	ap.maxAcceleration = 8.0f;
	ap.maxSpeed = 3.5f;
	ap.collisionQueryRange = ap.radius * 12.0f;
	ap.pathOptimizationRange = ap.radius * 30.0f;
	ap.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
	const float start[3] = {-9.0f, 0.0f, 9.0f};
	const int idx = crowd->addAgent(start, &ap);
	check("crowd.add", idx >= 0);

	dtNavMeshQuery* q = dtAllocNavMeshQuery();
	q->init(nav, 256);
	dtQueryFilter filter;
	const float ext[3] = {1.0f, 2.0f, 1.0f};
	const float goal[3] = {9.0f, 0.0f, -9.0f};
	dtPolyRef goalRef = 0;
	float goalPt[3];
	q->findNearestPoly(goal, ext, &filter, &goalRef, goalPt);
	check("crowd.target", crowd->requestMoveTarget(idx, goalRef, goalPt));
	for (int i = 0; i < 600; ++i)
	{
		crowd->update(0.05f, nullptr);
	}
	const dtCrowdAgent* ag = crowd->getAgent(idx);
	check("crowd.arrived", dtVdist2D(ag->npos, goalPt) < 1.0f);
	dtFreeNavMeshQuery(q);
	dtFreeCrowd(crowd);
}

static void katTileCache()
{
	const NavfixTileCacheFixture& fx = navfixTileCacheFixture();
	check("tc.layers", static_cast<int>(fx.layers.size()) == KAT_TC_LAYERS && fx.tw == KAT_TC_TW && fx.th == KAT_TC_TH);

	NavfixPassthroughCompressor comp;
	NavfixAllocator alloc;
	NavfixMeshProcess proc;
	dtTileCache* tc = dtAllocTileCache();
	dtNavMesh* nav = dtAllocNavMesh();
	check("tc.init", dtStatusSucceed(tc->init(&fx.params, &alloc, &comp, &proc)) && dtStatusSucceed(nav->init(&fx.navParams)));
	for (const NavfixTileLayer& layer : fx.layers)
	{
		tc->addTile(const_cast<unsigned char*>(layer.data.data()), static_cast<int>(layer.data.size()), 0, nullptr);
	}
	for (int y = 0; y < fx.th; ++y)
	{
		for (int x = 0; x < fx.tw; ++x)
		{
			tc->buildNavMeshTilesAt(x, y, nav);
		}
	}
	dtNavMeshQuery* q = dtAllocNavMeshQuery();
	q->init(nav, 256);
	dtQueryFilter filter;
	const float c[3] = {-5.0f, 0.0f, 5.0f};
	const float tight[3] = {0.4f, 1.0f, 0.4f};
	dtPolyRef ref = 0;
	float pt[3];
	q->findNearestPoly(c, tight, &filter, &ref, pt);
	check("tc.walkable.before", ref != 0);

	dtObstacleRef obs = 0;
	check("tc.add.obstacle", dtStatusSucceed(tc->addObstacle(c, 2.0f, 2.0f, &obs)));
	bool upToDate = false;
	for (int i = 0; i < 20 && !upToDate; ++i)
	{
		tc->update(0.1f, nav, &upToDate);
	}
	check("tc.uptodate", upToDate);
	ref = 1;
	q->findNearestPoly(c, tight, &filter, &ref, pt);
	check("tc.blocked.after", ref == 0);

	tc->removeObstacle(obs);
	upToDate = false;
	for (int i = 0; i < 20 && !upToDate; ++i)
	{
		tc->update(0.1f, nav, &upToDate);
	}
	q->findNearestPoly(c, tight, &filter, &ref, pt);
	check("tc.walkable.restored", ref != 0);
	dtFreeNavMeshQuery(q);
	dtFreeNavMesh(nav);
	dtFreeTileCache(tc);
}

int main()
{
	katObj();
	katGset();
	katNavmesh();
	katCrowd();
	katTileCache();
	std::printf("KAT done failed=%d\n", g_fail);
	return g_fail == 0 ? 0 : 1;
}
