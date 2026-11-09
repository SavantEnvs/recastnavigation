// Known-answer probe: drives RecastDemo's OBJ loading path (MeshLoaderObj, ChunkyTriMesh, InputGeom)
// with fixed inputs and prints one "KAT <name> ok|FAIL" line per checked value. test.sh counts these
// lines, so a neutered or no-op build fails the oracle.
#include "ChunkyTriMesh.h"
#include "InputGeom.h"
#include "MeshLoaderObj.h"
#include "Recast.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static int g_fail = 0;

static void check(const char* name, const bool cond)
{
	std::printf("KAT %s %s\n", name, cond ? "ok" : "FAIL");
	if (!cond)
	{
		++g_fail;
	}
}

static bool near(const float a, const float b, const float eps = 0.001f)
{
	return std::fabs(a - b) <= eps;
}

// Writes `text` to a fresh .obj file under TMPDIR; returns its path ("" on failure).
static std::string writeObj(const std::string& text)
{
	const char* tmp = getenv("TMPDIR");
	std::string path = std::string(tmp && *tmp ? tmp : "/tmp") + "/kat_probe_XXXXXX.obj";
	const int fd = mkstemps(&path[0], 4);
	if (fd < 0)
	{
		return "";
	}
	const bool ok = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
	close(fd);
	if (!ok)
	{
		unlink(path.c_str());
		return "";
	}
	return path;
}

static void katParse()
{
	const std::string path = writeObj(
		"# comment\nv 0 0 0\nv 10 0 0\nv 0 0 10\nv 10 0 10\nvn 0 1 0\nf 1 3 2\nf 2/1/1 3/2/2 4/3/3\nf -4 -2 -3 -1\n");
	rcMeshLoaderObj m;
	const bool ok = m.load(path);
	unlink(path.c_str());
	check("obj.load", ok);
	check("obj.verts", m.getVertCount() == 4);
	check("obj.tris", m.getTriCount() == 4);
	const int* t = m.getTris();
	check("obj.tri0", t && t[0] == 0 && t[1] == 2 && t[2] == 1);
	check("obj.tri1", t && t[3] == 1 && t[4] == 2 && t[5] == 3);
	check("obj.quad", t && t[6] == 0 && t[7] == 2 && t[8] == 1 && t[9] == 0 && t[10] == 1 && t[11] == 3);
	const float* v = m.getVerts();
	check("obj.vert3", v && near(v[9], 10.0f) && near(v[10], 0.0f) && near(v[11], 10.0f));
	const float* n = m.getNormals();
	check("obj.normal", n && near(n[0], 0.0f) && near(n[1], 1.0f) && near(n[2], 0.0f));
	rcMeshLoaderObj missing;
	check("obj.missing", !missing.load("/nonexistent/kat_probe.obj"));
}

static void katSmallMesh()
{
	const std::string path = writeObj("v 0 0 0\nv 4 0 0\nv 0 0 4\nf 1 3 2\n");
	rcContext ctx;
	InputGeom geom;
	const bool ok = geom.load(&ctx, path);
	unlink(path.c_str());
	check("small.load", ok);
	const rcChunkyTriMesh* cm = geom.getChunkyMesh();
	check("small.chunky", cm && cm->nnodes == 1 && cm->ntris == 1 && cm->maxTrisPerChunk == 1);
	const float* bmax = geom.getMeshBoundsMax();
	check("small.bounds", near(bmax[0], 4.0f) && near(bmax[1], 0.0f) && near(bmax[2], 4.0f));
	float src[3] = {1.0f, 10.0f, 1.0f};
	float dst[3] = {1.0f, -10.0f, 1.0f};
	float tmin = 0;
	check("small.hit", geom.raycastMesh(src, dst, tmin) && near(tmin, 0.5f));
	float osrc[3] = {3.5f, 10.0f, 3.5f};
	float odst[3] = {3.5f, -10.0f, 3.5f};
	check("small.miss", !geom.raycastMesh(osrc, odst, tmin));
}

// 1000 disjoint triangles in a row along x: triangle i spans x in [2i, 2i+1], z in [0, 1].
static void katSplit()
{
	std::string obj;
	for (int i = 0; i < 1000; ++i)
	{
		char row[160];
		std::snprintf(row, sizeof(row), "v %d 0 0\nv %d 0 0\nv %d 0 1\nf %d %d %d\n", 2 * i, 2 * i + 1, 2 * i, 3 * i + 1,
					  3 * i + 3, 3 * i + 2);
		obj += row;
	}
	const std::string path = writeObj(obj);
	rcContext ctx;
	InputGeom geom;
	const bool ok = geom.load(&ctx, path);
	unlink(path.c_str());
	check("split.load", ok);
	const rcChunkyTriMesh* cm = geom.getChunkyMesh();
	// 1000 -> 2x500 -> 4x250: four leaves under three inner nodes.
	check("split.tree", cm && cm->nnodes == 7 && cm->ntris == 1000 && cm->maxTrisPerChunk == 250);
	const float* bmax = geom.getMeshBoundsMax();
	check("split.bounds", near(bmax[0], 1999.0f) && near(bmax[2], 1.0f));
	int ids[16];
	float lo[2] = {-1.0f, -1.0f};
	float hi[2] = {2000.0f, 2.0f};
	check("split.rect.all", cm && rcGetChunksOverlappingRect(cm, lo, hi, ids, 16) == 4);
	float lo2[2] = {0.0f, 0.0f};
	float hi2[2] = {10.0f, 1.0f};
	check("split.rect.one", cm && rcGetChunksOverlappingRect(cm, lo2, hi2, ids, 16) == 1);
	float src[3] = {1000.25f, 5.0f, 0.25f};
	float dst[3] = {1000.25f, -5.0f, 0.25f};
	float tmin = 0;
	check("split.hit", geom.raycastMesh(src, dst, tmin) && near(tmin, 0.5f));
	float gsrc[3] = {1001.5f, 5.0f, 0.25f};
	float gdst[3] = {1001.5f, -5.0f, 0.25f};
	check("split.gap", !geom.raycastMesh(gsrc, gdst, tmin));
}

int main()
{
	katParse();
	katSmallMesh();
	katSplit();
	std::printf("KAT done failed=%d\n", g_fail);
	return g_fail == 0 ? 0 : 1;
}
