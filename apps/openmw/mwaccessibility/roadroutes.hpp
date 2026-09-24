#ifndef GAME_MWACCESSIBILITY_ROADROUTES_H
#define GAME_MWACCESSIBILITY_ROADROUTES_H

#include "roads.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

#include <components/misc/hash.hpp>

namespace MWAccessibility
{
    // Strip comma-suffixed districts and surrounding ASCII whitespace, preserving display case.
    std::string roadPlaceName(std::string_view name);

    // Admit the tile's area and nearby navmesh proxy, not a remote partial path.
    bool roadCheckpointReached(const RoadTile& tile, const osg::Vec2f& position);

    struct RoadDestination
    {
        std::string mName;
        std::vector<RoadTile> mTiles;
    };

    struct RoadRoute
    {
        std::string mDestination;
        // Complete ordered path, including entry and destination tiles.
        std::vector<RoadTile> mTiles;
        float mLength = 0;
        float mApproachLength = 0;
        bool mUsesFoyada = false;
    };

    // Bearing to the destination approach, not the entrance or initial heading.
    // Read from the player's current position; no bearing at the endpoint itself.
    std::optional<float> roadDestinationBearing(const RoadRoute& route, const osg::Vec2f& playerPosition);

    struct RoadNetworkRules
    {
        std::vector<RoadTile> mFoyadaTiles;
        // Applied while building adjacency; never retained beyond construction.
        std::function<bool(const RoadTile&, const RoadTile&)> mCanConnect;
    };

    // Pure tile topology, NOT proof of physical traversability: corner adjacency
    // may cross obstacles. No bridges or links across missing tiles are invented. The caller
    // must retain the autowalker's collision/obstruction handling.
    class RoadRoutePlanner
    {
    public:
        RoadRoutePlanner(
            std::vector<RoadTile> tiles, std::vector<RoadDestination> destinations, const RoadNetworkRules& rules = {});

        // Nearest entry per connected patch within the usual 3x3-cell scan.
        // Uses the same allowed links as routing, including foyada slope limits.
        std::vector<RoadTile> nearbyEntrances(const osg::Vec2f& playerPosition) const;

        // Minimise approach + road distance across all valid starts and anchors.
        // Missing starts/anchors are ignored. Return no zero-road-distance routes.
        std::vector<RoadRoute> routes(
            const std::vector<RoadTile>& starts, const osg::Vec2f& playerPosition, std::string_view origin) const;

    private:
        using TileKey = std::pair<std::int32_t, std::int32_t>;
        struct TileKeyHash
        {
            std::size_t operator()(const TileKey& key) const { return Misc::hash2dCoord(key.first, key.second); }
        };
        struct Destination
        {
            std::string mName;
            std::string mKey;
            std::vector<std::size_t> mAnchors;
        };
        std::vector<RoadTile> mTiles;
        // Lookup only: IDs, adjacency and tie-breaking still use sorted mTiles,
        // never hash iteration order.
        std::unordered_map<TileKey, std::size_t, TileKeyHash> mIndex;
        std::vector<std::vector<std::pair<std::size_t, double>>> mEdges;
        std::vector<bool> mFoyada;
        std::vector<Destination> mDestinations;
    };
}

#endif
