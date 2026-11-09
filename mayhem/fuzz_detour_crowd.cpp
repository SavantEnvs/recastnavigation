// DetourCrowd's dtCrowd agent simulation over a fixed navigation mesh (navfix.h): the input adds
// agents with fuzzer-chosen parameters, issues move-target / move-velocity requests, edits filters and
// obstacle-avoidance profiles, and steps the simulation (path queue, corridors, local boundary,
// proximity grid, collision avoidance, off-mesh animation).
#include "DetourCommon.h"
#include "DetourCrowd.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "navfix.h"

#include <fuzzer/FuzzedDataProvider.h>

#include <cstdint>

namespace
{
constexpr float kMaxAgentRadius = 1.5f;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	FuzzedDataProvider fdp(data, size);

	// dtCrowd keeps a non-const navmesh pointer but only reads it; the fixture is never modified.
	dtNavMesh* nav = const_cast<dtNavMesh*>(navfixSolo());
	dtCrowd* crowd = dtAllocCrowd();
	if (!crowd->init(fdp.ConsumeIntegralInRange<int>(1, 12), kMaxAgentRadius, nav))
	{
		dtFreeCrowd(crowd);
		return 0;
	}
	dtNavMeshQuery* query = dtAllocNavMeshQuery();
	query->init(nav, 512);

	auto point = [&](float* p)
	{
		p[0] = fdp.ConsumeFloatingPointInRange<float>(-11.0f, 11.0f);
		p[1] = fdp.ConsumeFloatingPointInRange<float>(-0.5f, 2.0f);
		p[2] = fdp.ConsumeFloatingPointInRange<float>(-11.0f, 11.0f);
	};
	auto agentParams = [&](dtCrowdAgentParams& ap)
	{
		memset(&ap, 0, sizeof(ap));
		ap.radius = fdp.ConsumeFloatingPointInRange<float>(0.05f, kMaxAgentRadius);
		ap.height = fdp.ConsumeFloatingPointInRange<float>(0.5f, 3.0f);
		ap.maxAcceleration = fdp.ConsumeFloatingPointInRange<float>(0.0f, 20.0f);
		ap.maxSpeed = fdp.ConsumeFloatingPointInRange<float>(0.0f, 8.0f);
		ap.collisionQueryRange = ap.radius * fdp.ConsumeFloatingPointInRange<float>(1.0f, 12.0f);
		ap.pathOptimizationRange = ap.radius * fdp.ConsumeFloatingPointInRange<float>(1.0f, 30.0f);
		ap.separationWeight = fdp.ConsumeFloatingPointInRange<float>(0.0f, 20.0f);
		ap.updateFlags = fdp.ConsumeIntegralInRange<int>(0, 31);
		ap.obstacleAvoidanceType = fdp.ConsumeIntegralInRange<int>(0, DT_CROWD_MAX_OBSTAVOIDANCE_PARAMS - 1);
		ap.queryFilterType = fdp.ConsumeIntegralInRange<int>(0, DT_CROWD_MAX_QUERY_FILTER_TYPE - 1);
	};

	const float ext[3] = {2.0f, 4.0f, 2.0f};
	dtQueryFilter nearestFilter;
	int agents[16];
	int nagents = 0;

	const int nops = fdp.ConsumeIntegralInRange<int>(1, 40);
	for (int op = 0; op < nops && fdp.remaining_bytes() > 0; ++op)
	{
		switch (fdp.ConsumeIntegralInRange<int>(0, 8))
		{
		case 0:
		{
			float p[3];
			point(p);
			dtCrowdAgentParams ap;
			agentParams(ap);
			const int idx = crowd->addAgent(p, &ap);
			if (idx >= 0 && nagents < 16)
			{
				agents[nagents++] = idx;
			}
			break;
		}
		case 1:
		case 2:
		{
			if (nagents == 0)
			{
				break;
			}
			float p[3];
			point(p);
			dtPolyRef ref = 0;
			float nearest[3];
			query->findNearestPoly(p, ext, &nearestFilter, &ref, nearest);
			if (ref)
			{
				crowd->requestMoveTarget(agents[fdp.ConsumeIntegralInRange<int>(0, nagents - 1)], ref, nearest);
			}
			break;
		}
		case 3:
		{
			if (nagents == 0)
			{
				break;
			}
			float v[3] = {fdp.ConsumeFloatingPointInRange<float>(-8.0f, 8.0f), 0.0f, fdp.ConsumeFloatingPointInRange<float>(-8.0f, 8.0f)};
			crowd->requestMoveVelocity(agents[fdp.ConsumeIntegralInRange<int>(0, nagents - 1)], v);
			break;
		}
		case 4:
		{
			if (nagents == 0)
			{
				break;
			}
			const int a = fdp.ConsumeIntegralInRange<int>(0, nagents - 1);
			if (fdp.ConsumeBool())
			{
				crowd->resetMoveTarget(agents[a]);
			}
			else
			{
				dtCrowdAgentParams ap;
				agentParams(ap);
				crowd->updateAgentParameters(agents[a], &ap);
			}
			break;
		}
		case 5:
		{
			if (nagents > 0 && fdp.ConsumeBool())
			{
				const int a = fdp.ConsumeIntegralInRange<int>(0, nagents - 1);
				crowd->removeAgent(agents[a]);
				agents[a] = agents[--nagents];
			}
			break;
		}
		case 6:
		{
			dtObstacleAvoidanceParams oa;
			oa.velBias = fdp.ConsumeFloatingPointInRange<float>(0.0f, 1.0f);
			oa.weightDesVel = fdp.ConsumeFloatingPointInRange<float>(0.0f, 4.0f);
			oa.weightCurVel = fdp.ConsumeFloatingPointInRange<float>(0.0f, 4.0f);
			oa.weightSide = fdp.ConsumeFloatingPointInRange<float>(0.0f, 4.0f);
			oa.weightToi = fdp.ConsumeFloatingPointInRange<float>(0.0f, 4.0f);
			oa.horizTime = fdp.ConsumeFloatingPointInRange<float>(0.1f, 8.0f);
			oa.gridSize = fdp.ConsumeIntegralInRange<int>(1, 33);
			oa.adaptiveDivs = fdp.ConsumeIntegralInRange<int>(1, DT_MAX_PATTERN_DIVS);
			oa.adaptiveRings = fdp.ConsumeIntegralInRange<int>(1, DT_MAX_PATTERN_RINGS);
			oa.adaptiveDepth = fdp.ConsumeIntegralInRange<int>(1, 5);
			crowd->setObstacleAvoidanceParams(fdp.ConsumeIntegralInRange<int>(0, DT_CROWD_MAX_OBSTAVOIDANCE_PARAMS - 1), &oa);
			break;
		}
		case 7:
		{
			dtQueryFilter* f = crowd->getEditableFilter(fdp.ConsumeIntegralInRange<int>(0, DT_CROWD_MAX_QUERY_FILTER_TYPE - 1));
			f->setIncludeFlags(fdp.ConsumeIntegral<unsigned short>());
			f->setExcludeFlags(fdp.ConsumeIntegral<unsigned short>());
			f->setAreaCost(fdp.ConsumeIntegralInRange<int>(0, DT_MAX_AREAS - 1), fdp.ConsumeFloatingPointInRange<float>(1.0f, 20.0f));
			break;
		}
		default:
		{
			const int steps = fdp.ConsumeIntegralInRange<int>(1, 20);
			const float dt = fdp.ConsumeFloatingPointInRange<float>(0.01f, 0.5f);
			for (int i = 0; i < steps; ++i)
			{
				crowd->update(dt, nullptr);
			}
			break;
		}
		}
	}

	dtFreeNavMeshQuery(query);
	dtFreeCrowd(crowd);
	return 0;
}
