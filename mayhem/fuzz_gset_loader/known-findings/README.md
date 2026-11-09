# InputGeom::loadGeomSet: convex volume `nverts` is not bounded by MAX_CONVEXVOL_PTS

In the `v` row handler (RecastDemo/Source/InputGeom.cpp:455-461) `vol.nverts` comes straight from
`sscanf("%d")` and then drives `for (i < vol.nverts)` writing `vol.verts[i*3+k]`, while
`ConvexVolume::verts` holds `MAX_CONVEXVOL_PTS * 3 = 36` floats. A `.gset` row such as `v 31 2 0 1` followed by
vertex rows writes past the array (negative `nverts` is harmless; large ones corrupt the heap-allocated
vector element and beyond).

Reproducer: `crash.bin` (`./fuzz_gset_loader-standalone crash.bin`) ->
`InputGeom.cpp:460:53: runtime error: index 37 out of bounds for type 'float[36]'`.

Impact: out-of-bounds write of attacker-chosen floats when loading an untrusted `.gset`.

Suggested fix: reject (or clamp) `nverts` outside `[0, MAX_CONVEXVOL_PTS]` before the loop.
