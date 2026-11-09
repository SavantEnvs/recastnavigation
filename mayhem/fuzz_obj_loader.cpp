// RecastDemo's OBJ mesh loader: InputGeom::load on a .obj file (rcMeshLoaderObj::load, rcCalcBounds,
// rcCreateChunkyTriMesh), then InputGeom::raycastMesh over the chunky mesh.
#include "InputGeom.h"
#include "MeshLoaderObj.h"
#include "Recast.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	// InputGeom::load only takes a path (the .obj suffix selects the OBJ loader); the file is private to
	// this input and removed before returning.
	const char* tmp = getenv("TMPDIR");
	std::string path = std::string(tmp && *tmp ? tmp : "/tmp") + "/fuzz_obj_XXXXXX.obj";
	const int fd = mkstemps(&path[0], 4);
	if (fd < 0)
	{
		return -1;
	}
	const bool written = write(fd, data, size) == static_cast<ssize_t>(size);
	close(fd);
	if (!written)
	{
		unlink(path.c_str());
		return -1;
	}

	// InputGeom::load calls rcCalcBounds, which copies verts[0..2] before looking at numVerts, so it needs at
	// least one vertex (Recast.h: verts is "(x, y, z) * numVerts"; an OBJ with none leaves it null).
	rcMeshLoaderObj probe;
	if (!probe.load(path) || probe.getVertCount() == 0)
	{
		unlink(path.c_str());
		return -1;
	}

	rcContext ctx;
	InputGeom geom;
	const bool loaded = geom.load(&ctx, path);
	unlink(path.c_str());
	if (!loaded)
	{
		return 0;
	}

	const float* bmin = geom.getMeshBoundsMin();
	const float* bmax = geom.getMeshBoundsMax();
	float src[3];
	float dst[3];
	for (int i = 0; i < 3; ++i)
	{
		src[i] = bmin[i];
		dst[i] = bmax[i];
	}
	float tmin = 0;
	geom.raycastMesh(src, dst, tmin);
	// Straight down through the middle of the bounds.
	src[0] = dst[0] = 0.5f * (bmin[0] + bmax[0]);
	src[2] = dst[2] = 0.5f * (bmin[2] + bmax[2]);
	src[1] = bmax[1] + 1.0f;
	dst[1] = bmin[1] - 1.0f;
	geom.raycastMesh(src, dst, tmin);
	return 0;
}
