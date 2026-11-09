# dtObstacleAvoidanceQuery::sampleVelocityAdaptive writes one slot past its pattern buffer

With an even `adaptiveDivs` (up to the documented `DT_MAX_PATTERN_DIVS = 32`) and `adaptiveRings` up to
`DT_MAX_PATTERN_RINGS = 4`, the even-divs tail of the ring loop
(DetourCrowd/Source/DetourObstacleAvoidance.cpp:572-576) writes `pat[npat*2+2]`/`pat[npat*2+3]`, i.e. slot
`npat+1`, after the inner loop already advanced `npat` past the last pair. On the last ring that is
`pat[258]`, one float beyond `float pat[(DT_MAX_PATTERN_DIVS*DT_MAX_PATTERN_RINGS+1)*2]`, and the slot at
`npat` is left unwritten (uninitialized sample).

Reproducer: `crash.bin` (`./fuzz_detour_crowd-standalone crash.bin`) ->
`DetourObstacleAvoidance.cpp:574:4: runtime error: index 258 out of bounds for type 'float[258]'`
(setObstacleAvoidanceParams with divs=32, rings=4, then crowd updates with an agent that has avoidance on).

Impact: one-float stack out-of-bounds write plus an uninitialized velocity sample in the default crowd
avoidance mode for any params with even adaptiveDivs.

Suggested fix: write the even-divs tail sample at `pat[npat*2+0]`/`pat[npat*2+1]` (slot `npat`).
