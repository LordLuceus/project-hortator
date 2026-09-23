#ifndef OPENMW_MWACCESSIBILITY_ROADNETWORK_H
#define OPENMW_MWACCESSIBILITY_ROADNETWORK_H

#include <memory>

namespace MWAccessibility
{
    class RoadRoutePlanner;

    // Snapshot painted roads, terrain-checked dry channels and named exterior
    // cells from winning records. No Ptrs or proximity-based place guesses.
    std::unique_ptr<RoadRoutePlanner> loadRoadRoutePlanner();
}

#endif
