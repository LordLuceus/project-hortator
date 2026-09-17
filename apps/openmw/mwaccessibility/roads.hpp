#ifndef GAME_MWACCESSIBILITY_ROADS_H
#define GAME_MWACCESSIBILITY_ROADS_H

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Vec2f>

namespace MWWorld
{
    class Ptr;
}

namespace MWAccessibility
{
    // ROAD DETECTION from the land texture layer.
    //
    // NPCs give directions in terms of the landscape -- "follow the road east of
    // Balmora", "take the road to Pelagiad" -- and until now a blind player could
    // only head in the general direction and hope. Roads are not objects and they
    // are not in the pathgrid; they are PAINTED INTO THE TERRAIN as land textures,
    // which is the level authors' own statement of where a road runs.
    //
    // Reading the texture layer rather than guessing from geometry matters for the
    // same reason the shaft detector reads the architecture: it is authored data,
    // it is exact, and it works for any mod that paints roads with road textures,
    // with no per-cell knowledge on our part. Measured against Morrowind.esm there
    // are 5,305 road tiles across 242 of the 1,292 exterior cells, and under
    // 8-neighbour adjacency they form 90 connected components with only 10 isolated
    // single tiles -- i.e. a genuinely continuous network worth following, with the
    // largest component spanning roughly a kilometre.
    //
    // NOTE ON THE DATA. Each exterior cell carries a 16x16 grid of texture indices
    // (ESM::LandRecordData::sLandTextureSize), so one "tile" here is 1/16th of a
    // cell = 512 units ~= 7.3 metres. The ESM stores that grid SWIZZLED in 4x4
    // blocks of 4x4; ESM::Land::loadData already untangles it via
    // transposeTextureData, so mTextures is plain row-major by the time we see it.
    // Any offline analysis of the raw record must transpose first or it will
    // measure a shredded, meaningless network.
    //
    // This module is deliberately pure -- it takes texture names and tile
    // coordinates and returns plain data, with no MWWorld/MWBase dependencies -- so
    // the fiddly parts (which textures count as road, how tiles are clustered into
    // stretches, which way a stretch runs) are unit-testable rather than only
    // checkable in game. See apps/openmw_tests/mwaccessibility/roads.cpp.

    // Side length of one land-texture tile, in game units.
    //
    // An exterior cell is 8192 units across and carries a 16x16 texture grid, so a
    // tile is 512 units (~7.3 metres) -- roughly the width of a road, which is why
    // a road reads as a clean one- or two-tile ribbon rather than a smear. Defined
    // here as a named constant so the pure functions stay free of engine headers;
    // roads.cpp static_asserts it against Constants::CellSizeInUnits and
    // ESM::LandRecordData::sLandTextureSize so the two can never drift apart.
    inline constexpr float kRoadTileSize = 512.f;

    // True if a land texture names a walkable ROAD surface.
    //
    // Matched on the texture's name because that is what the authors named it
    // after: "road", "dirtroad", "mainroad", "cobblestones". In Morrowind.esm this
    // selects 12 of the 107 land textures, spanning every region's road set
    // (Road Dirt, AL_road_01, WG_mainroad_01, AI_Dirtroad, GL_Dirtroad, ...), and
    // a mod that paints its roads with similarly-named textures is picked up for
    // free.
    //
    // Deliberately EXCLUDES the Daedric ruin textures (Tx_Daed_road*): despite the
    // filename those are decorative flagstones inside ruin courtyards, not a road
    // anyone can follow anywhere. Matching is case-insensitive on ASCII and looks
    // at both the texture id and its filename, since the two disagree for several
    // entries (index 105's id is literally "Tx_AI_mainroad_01.tga").
    bool isRoadTexture(std::string_view textureName);

    // A tile of road, in TILE coordinates: whole-world indices where one unit is
    // one 512-unit texture cell. Using tile coordinates (rather than world units)
    // keeps clustering exact and integer, and matches how the data is stored.
    struct RoadTile
    {
        std::int32_t mX = 0;
        std::int32_t mY = 0;

        friend bool operator==(const RoadTile& a, const RoadTile& b) { return a.mX == b.mX && a.mY == b.mY; }
    };

    // A connected run of road tiles -- one continuous stretch of road near the
    // player, which is the unit a player actually cares about ("the road", not
    // "seventy-one road tiles").
    struct RoadStretch
    {
        // Tiles making up this stretch, nearest-first relative to the query point.
        std::vector<RoadTile> mTiles;
        // The tile of this stretch closest to the query point: the spot to walk to
        // in order to GET ON the road, and the one worth measuring and facing.
        RoadTile mNearest;
        // Heading of the road where it passes mNearest, as a unit vector in world
        // XY. Fitted across the local run of tiles, so a straight stretch gives a
        // clean axis and a bend gives the average through it. Zero-length when the
        // stretch is too small to have a meaningful direction (a lone tile).
        osg::Vec2f mDirection;
        // True when mDirection was fitted from enough tiles to be meaningful.
        bool mHasDirection = false;
    };

    // Group road tiles into connected stretches, nearest-first.
    //
    // Adjacency is 8-neighbour (diagonals included), because a road painted
    // diagonally across the texture grid is a staircase of tiles that touch only at
    // their corners -- treating those as disconnected would shatter every diagonal
    // road into unusable fragments.
    //
    // \a queryTile is the player's own tile: stretches are ordered by their nearest
    // tile's distance from it, and each stretch's own tile list is ordered the same
    // way, so callers can take the first of each without re-sorting.
    std::vector<RoadStretch> groupRoadTiles(const std::vector<RoadTile>& tiles, const RoadTile& queryTile);

    // Fit the local heading of a road at \a at, using the tiles around it.
    //
    // Returns a unit vector in world XY, or a zero vector when there is not enough
    // local structure to call it. A road's direction is what makes "follow the road
    // east" actionable: it lets us say which way the road runs rather than only
    // where it is. The fit is symmetric (it looks both ways along the stretch), so
    // the result is an AXIS -- callers decide which of the two ends to speak about.
    osg::Vec2f fitRoadDirection(const std::vector<RoadTile>& tiles, const RoadTile& at);

    // Describe a road axis as a spoken two-ended bearing, e.g. "east to west" or
    // "northeast to southwest". Empty when \a direction is degenerate.
    //
    // Two-ended rather than one-ended deliberately: an axis has no inherent
    // forward, and saying "the road runs east" would be a coin-flip that is wrong
    // half the time. The compass wording matches the rest of the mod's output.
    std::string describeRoadAxis(const osg::Vec2f& direction);

    // World-XY centre of a tile, in game units.
    osg::Vec2f roadTileCentre(const RoadTile& tile);

    // Which tile a world-XY position falls in.
    RoadTile roadTileAt(const osg::Vec2f& worldPos);

    // Pick the next tile when FOLLOWING a road, given where we are and which way
    // we are already going.
    //
    // \a candidates are the road tiles adjacent to \a from (any subset of the 8
    // neighbours); \a heading is the direction of travel so far, as a world-XY
    // vector, and need not be normalised.
    //
    // Chooses the candidate whose bearing from \a from is closest to \a heading --
    // i.e. keep going as straight as the road allows. This is what "follow the
    // road" means literally, and it is the only rule that behaves sanely on
    // Morrowind's actual road data, where 46% of road tiles have four or more road
    // neighbours: most "junctions" are just a road two or three tiles wide, so a
    // policy that stopped or asked at every branching tile would stop constantly.
    // Going straightest carries you along the road you are on and through the wide
    // patches without comment.
    //
    // Candidates more than 90 degrees off the heading are REJECTED, so the walk can
    // never double back on itself and oscillate between two tiles -- at a dead end
    // it returns nothing and the caller stops honestly. Returns false when there is
    // no acceptable next tile.
    bool chooseStraightestStep(
        const std::vector<RoadTile>& candidates, const RoadTile& from, const osg::Vec2f& heading, RoadTile& out);

    // --- Engine-facing ------------------------------------------------------
    // Everything above is pure and unit-tested. The functions below read the live
    // ESM store, so they can only be exercised in game.

    // True if \a tile is a road tile, reading the land record for whichever cell
    // contains it.
    //
    // Works for cells that are NOT currently loaded: ESM::Land::loadData restores a
    // saved file context and reads on demand, caching the result. That is what lets
    // a road be followed across the province rather than only within the 3x3 cell
    // grid the engine keeps active -- Balmora's road stretch alone is 840 tiles
    // spanning roughly 970 by 1390 metres across nine cells by twelve.
    bool isRoadTileAt(const RoadTile& tile);

    // The road tiles adjacent to \a tile (of the 8 neighbours).
    std::vector<RoadTile> roadNeighboursOf(const RoadTile& tile);

    // Where following a road from \a from in \a heading would actually END UP.
    //
    // The local axis a road runs along says nothing about where it GOES: a road
    // leaving Balmora "northwest" bends round and returns to Balmora, so the
    // player who picks northwest to reach Caldera is sent home instead. Walking
    // the whole route up front is what makes the choice informed, and it is cheap
    // -- the same straightest-step rule the follower uses, over data already
    // memoised by Land::loadData, at roughly 0.02 ms per route.
    struct RoadPreview
    {
        // The last tile of the route.
        RoadTile mEnd;
        // Tiles stepped through (1 means the road goes nowhere from here).
        std::size_t mSteps = 0;
        // Distance ALONG the road, in world units -- what the player will walk,
        // not the straight line to the end.
        float mLength = 0.0f;
        // Net direction from start to end. Deliberately NOT the heading chosen:
        // when a road doubles back these disagree, and that disagreement is
        // exactly what the player needs to hear.
        osg::Vec2f mNetBearing;
        // True when the route ends within kLoopBackTiles of where it began, i.e.
        // the road loops back on itself.
        bool mLoopsBack = false;
    };

    // How close the end must be to the start to count as looping back. Two tiles
    // (~14 m): far enough not to trip on a road that merely curves, close enough
    // that "you end up where you started" is honest.
    constexpr std::int32_t kLoopBackTiles = 2;

    // Pre-walk the route. \a maxSteps bounds the walk; routes are short in
    // practice (mean ~10 tiles, 90th percentile 26) but a province-spanning road
    // needs a ceiling.
    //
    // \a neighbours supplies the adjacent road tiles. It is injected purely so the
    // walk can be tested against hand-built roads: the engine-backed default reads
    // land data and needs a live world, which would put this logic beyond the
    // reach of unit tests -- and route-shape bugs (loops, doubling back) are
    // exactly what tests catch and play-testing does not.
    using RoadNeighbourFn = std::function<std::vector<RoadTile>(const RoadTile&)>;
    RoadPreview previewRoad(const RoadTile& from, const osg::Vec2f& heading, std::size_t maxSteps = 600,
        const RoadNeighbourFn& neighbours = {});

    // Find the road stretches near \a player, nearest-first.
    //
    // Samples the land texture grid over a square of cells around the player and
    // returns the connected stretches found, each with the tile nearest the player
    // and the local heading of the road there. Empty in interiors (no land record)
    // and in any worldspace that isn't the main exterior.
    std::vector<RoadStretch> collectNearbyRoads(const MWWorld::Ptr& player);
}

#endif
