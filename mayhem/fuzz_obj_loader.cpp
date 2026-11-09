// RecastDemo's OBJ mesh loader, as InputGeom::loadMesh runs it: Mesh::readFromObj, rcCalcBounds,
// PartitionedMesh::PartitionMesh, then InputGeom::raycastMesh over the partitioned mesh.
#include "InputGeom.h"
#include "Recast.h"

#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	// readFromObj takes a mutable buffer (readFace overwrites '/' with NUL).
	std::vector<char> buf(data, data + size);

	InputGeom geom;
	geom.mesh.readFromObj(buf.data(), buf.size());

	// rcCalcBounds copies verts[0..2] before looking at numVerts, so it needs at least one vertex.
	if (geom.mesh.getVertCount() == 0)
	{
		return -1;
	}
	rcCalcBounds(geom.mesh.verts.data(), geom.mesh.getVertCount(), geom.meshBoundsMin, geom.meshBoundsMax);

	geom.partitionedMesh.PartitionMesh(geom.mesh.verts.data(), geom.mesh.tris.data(), geom.mesh.getTriCount(), 256);

	float src[3];
	float dst[3];
	for (int i = 0; i < 3; ++i)
	{
		src[i] = geom.meshBoundsMin[i];
		dst[i] = geom.meshBoundsMax[i];
	}
	float tmin = 0;
	geom.raycastMesh(src, dst, tmin);
	// Straight down through the middle of the bounds.
	src[0] = dst[0] = 0.5f * (geom.meshBoundsMin[0] + geom.meshBoundsMax[0]);
	src[2] = dst[2] = 0.5f * (geom.meshBoundsMin[2] + geom.meshBoundsMax[2]);
	src[1] = geom.meshBoundsMax[1] + 1.0f;
	dst[1] = geom.meshBoundsMin[1] - 1.0f;
	geom.raycastMesh(src, dst, tmin);
	return 0;
}
