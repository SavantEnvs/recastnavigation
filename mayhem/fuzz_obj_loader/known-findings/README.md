# PartitionedMesh: float-to-int conversion of a NaN/infinite bound (UBSan float-cast-overflow)

`compareMinX/Y` in RecastDemo/Source/PartitionedMesh.cpp:40 (and the `Z` twin) return
`static_cast<int>(a->bmin[i] - b->bmin[i])`. An OBJ whose vertex coordinates overflow to inf or produce
NaN (inf - inf) makes that conversion undefined (and truncates any difference above INT_MAX, so the sort
comparator is also inconsistent for huge coordinates).

Reproducer: `crash.bin` (`./fuzz_obj_loader-standalone crash.bin`) ->
`PartitionedMesh.cpp:40:26: runtime error: nan is outside the range of representable values of type 'int'`,
reached through `InputGeom`'s OBJ load -> `PartitionMesh`.

Impact: UB on a malformed (or hostile) OBJ in the demo's mesh loader; benign on x86 but the qsort
comparator can violate strict weak ordering.

Suggested fix: compare with `(a > b) - (a < b)` on the floats instead of casting the difference to int.
