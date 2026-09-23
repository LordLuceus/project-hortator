#ifndef GAME_MWACCESSIBILITY_FOYADAS_H
#define GAME_MWACCESSIBILITY_FOYADAS_H

#include "roads.hpp"

#include <optional>

namespace MWAccessibility
{
    enum class FoyadaMaterial
    {
        None,
        Flow,
        Rock,
    };

    FoyadaMaterial classifyFoyadaTexture(std::string_view filename);

    // Coordinates are native terrain vertices (four steps per road tile).
    using TerrainVertexHeightFn = std::function<std::optional<float>(std::int64_t x, std::int64_t y)>;

    // Rise/run, including full triangle slopes, not just rise along the edge.
    // Invalid edges, absent callbacks and missing/nonfinite samples fail closed.
    // Compare against tan(engine maximum slope); this does not establish clearance
    // or absence of damaging activators. The live walker remains authoritative.
    float foyadaEdgeGrade(const RoadTile& from, const RoadTile& to, const TerrainVertexHeightFn& height);

    struct FoyadaTile
    {
        RoadTile mTile;
        FoyadaMaterial mMaterial;
    };

    // Eight-neighbour flood fill from Flow through Flow/Rock, sorted by (x,y).
    // Duplicate materials merge with Flow taking precedence. Empty canConnect
    // fails closed: seeds remain, but no edges are traversed.
    std::vector<RoadTile> collectFoyadaTiles(const std::vector<FoyadaTile>& candidates,
        const std::function<bool(const RoadTile&, const RoadTile&)>& canConnect);
}

#endif
