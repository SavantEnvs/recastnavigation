// RecastDemo's .gset scene-file loader: InputGeom::load -> loadGeomSet (off-mesh connections 'c',
// convex volumes 'v', build settings 's', mesh references 'f'). The file is served from memory by
// demo_fileio.cpp, so an 'f' row can only name the input itself or the built-in "mesh.obj" triangle.
#include "InputGeom.h"
#include "Recast.h"
#include "demo_fileio.h"

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	demoFileSetInput("input.gset", data, size);
	rcContext ctx;
	InputGeom geom;
	geom.load(&ctx, "input.gset");
	return 0;
}
