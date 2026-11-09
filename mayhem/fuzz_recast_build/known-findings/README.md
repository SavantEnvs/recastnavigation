# rcBuildPolyMeshDetail with `detailSampleDist == 0` triangulates an empty hull

`rcConfig::detailSampleDist` is documented "[Limits: 0 or >= 0.9]" (Recast.h), and RecastDemo passes 0 when
the detail slider is below 0.9. With 0, `buildPolyDetail()` skips the outline tessellation loop
(`if (sampleDist > 0)`, RecastMeshDetail.cpp:701), so `nhull` stays 0, and then calls
`triangulateHull(nverts, verts, 0, hull, nin, tris)` (line 816). That function starts with
`left = 1, right = nhull-1 = -1` and reads `hull[start]`, `hull[left]`, `hull[right]` from an
uninitialized (and, for `hull[-1]`, out-of-bounds) stack array.

Reproducer: `sampledist0-empty-hull.bin` (`./fuzz_recast_build-standalone sampledist0-empty-hull.bin`) ->
ASan stack-buffer-overflow in `rcVectorBase<int>::construct` <- `triangulateHull` <- `buildPolyDetail`.

Impact: out-of-bounds stack read and garbage detail triangles whenever a poly mesh is built with the
detail mesh enabled but sample distance 0 (the "no height detail" setting).

Suggested fix: in `buildPolyDetail`, when `sampleDist <= 0` fill `hull` with `0..nin-1` (the polygon's own
vertices) instead of leaving it empty.
