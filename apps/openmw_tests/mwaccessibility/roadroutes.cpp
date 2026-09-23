#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>

#include <apps/openmw/mwaccessibility/foyadas.hpp>
#include <apps/openmw/mwaccessibility/roadroutes.hpp>
#include <components/misc/constants.hpp>

namespace
{
    using namespace MWAccessibility;

    TEST(MWAccessibilityRoadRoutes, NearbyEntrancesPreservePaintedRoadDiscovery)
    {
        const std::vector<RoadTile> tiles{ { -17, 0 }, { -16, 0 }, { 0, 0 }, { 1, 0 }, { 31, 0 }, { 32, 0 } };
        const RoadTile query{ -1, 0 };
        std::vector<RoadTile> visible;
        for (const auto& tile : tiles)
            if (tile.mX < 16)
                visible.push_back(tile);
        std::set<std::pair<int, int>> expected, actual;
        for (const auto& stretch : groupRoadTiles(visible, query))
            expected.emplace(stretch.mNearest.mX, stretch.mNearest.mY);
        const RoadRoutePlanner planner(tiles, {});
        for (const auto& tile : planner.nearbyEntrances(roadTileCentre(query)))
            actual.emplace(tile.mX, tile.mY);
        EXPECT_EQ(actual, expected);
        EXPECT_TRUE(planner.nearbyEntrances(osg::Vec2f(std::numeric_limits<float>::infinity(), 0)).empty());
        EXPECT_TRUE(planner.nearbyEntrances(osg::Vec2f(std::numeric_limits<float>::max(), 0)).empty());
    }

    TEST(MWAccessibilityRoadRoutes, FoyadaRoutesAreLabelledAndBlockedLinksSplitEntrances)
    {
        RoadNetworkRules rules;
        rules.mFoyadaTiles = { { 2, 0 }, { 4, 0 } };
        rules.mCanConnect = [](const RoadTile& a, const RoadTile& b) { return a.mX != 3 && b.mX != 3; };
        RoadRoutePlanner planner({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 } },
            { { "Road town", { { 1, 0 } } }, { "Ravine town", { { 2, 0 } } }, { "Blocked", { { 4, 0 } } } }, rules);
        const auto routes = planner.routes({ { 0, 0 } }, roadTileCentre({ 0, 0 }), "");
        ASSERT_EQ(routes.size(), 2u);
        EXPECT_FALSE(routes[0].mUsesFoyada);
        EXPECT_TRUE(routes[1].mUsesFoyada);
        EXPECT_EQ(planner.nearbyEntrances(roadTileCentre({ 0, 0 })).size(), 3u);
        // Starting in the ravine itself must work without a painted road nearby.
        EXPECT_EQ(planner.nearbyEntrances(roadTileCentre({ 4, 0 })).back(), (RoadTile{ 4, 0 }));
    }

    TEST(MWAccessibilityRoadRoutes, RealMamaeaConnectsBalmoraToGhostgateWhenGameDataProvided)
    {
        const char* path = std::getenv("OPENMW_FOYADA_TEST_DATA");
        if (!path)
            GTEST_SKIP() << "Set OPENMW_FOYADA_TEST_DATA to audit_foyada_routes.py --test-data output";
        std::ifstream input(path);
        ASSERT_TRUE(input.is_open());
        std::vector<RoadTile> roads;
        std::vector<FoyadaTile> candidates;
        std::map<std::pair<int, int>, std::string> names;
        std::map<std::pair<int, int>, std::array<float, 4225>> heights;
        std::set<std::pair<int, int>> roadSet;
        char record;
        while (input >> record)
        {
            int x, y;
            ASSERT_TRUE(bool(input >> x >> y));
            if (record == 'T')
            {
                std::string texture, name;
                ASSERT_TRUE(bool(input >> std::quoted(texture) >> std::quoted(name)));
                names[{ x, y }] = name;
                const auto material = classifyFoyadaTexture(texture);
                if (material == FoyadaMaterial::None && isRoadTexture(texture))
                {
                    roads.push_back({ x, y });
                    roadSet.emplace(x, y);
                }
                else
                    candidates.push_back({ { x, y }, material });
            }
            else
            {
                ASSERT_EQ(record, 'H');
                for (float& z : heights[{ x, y }])
                    ASSERT_TRUE(bool(input >> z));
            }
        }
        ASSERT_TRUE(input.eof());
        ASSERT_GT(candidates.size(), 1000u);
        const TerrainVertexHeightFn height = [&](std::int64_t x, std::int64_t y) -> std::optional<float> {
            const auto cx = static_cast<int>(std::floor(double(x) / 64));
            const auto cy = static_cast<int>(std::floor(double(y) / 64));
            const auto found = heights.find({ cx, cy });
            if (found == heights.end())
                return std::nullopt;
            return found->second[(y - std::int64_t(cy) * 64) * 65 + x - std::int64_t(cx) * 64];
        };
        const float limit = std::tan(Constants::sMaxSlope * float(std::acos(-1.0)) / 180.f);
        RoadNetworkRules rules;
        rules.mCanConnect = [&](const RoadTile& a, const RoadTile& b) {
            return (roadSet.count({ a.mX, a.mY }) && roadSet.count({ b.mX, b.mY }))
                || foyadaEdgeGrade(a, b, height) < limit;
        };
        rules.mFoyadaTiles = collectFoyadaTiles(candidates, rules.mCanConnect);
        std::vector<RoadTile> tiles = roads;
        tiles.insert(tiles.end(), rules.mFoyadaTiles.begin(), rules.mFoyadaTiles.end());
        std::map<std::string, std::vector<RoadTile>> anchors;
        for (const auto& tile : tiles)
            if (const auto name = names.find({ tile.mX, tile.mY }); name != names.end() && !name->second.empty())
                anchors[name->second].push_back(tile);
        std::vector<RoadDestination> destinations;
        for (const auto& [name, points] : anchors)
            destinations.push_back({ name, points });
        RoadRoutePlanner planner(tiles, destinations, rules);
        const RoadTile start{ -35, -16 };
        const auto routes = planner.routes({ start }, roadTileCentre(start), "Balmora");
        const auto goal = std::find_if(
            routes.begin(), routes.end(), [](const auto& route) { return route.mDestination == "Ghostgate"; });
        ASSERT_NE(goal, routes.end());
        EXPECT_TRUE(goal->mUsesFoyada);
        EXPECT_GT(goal->mLength / 70, 1000);
        EXPECT_LT(goal->mLength / 70, 2000);
        EXPECT_EQ(names[std::make_pair(goal->mTiles.back().mX, goal->mTiles.back().mY)], "Ghostgate");
        for (std::size_t i = 1; i < goal->mTiles.size(); ++i)
            EXPECT_TRUE(rules.mCanConnect(goal->mTiles[i - 1], goal->mTiles[i]));
        const auto foothold = std::find_if(goal->mTiles.begin(), goal->mTiles.end(),
            [&](const RoadTile& tile) { return !roadSet.count({ tile.mX, tile.mY }); });
        ASSERT_NE(foothold, goal->mTiles.end());
        const auto entrances = planner.nearbyEntrances(roadTileCentre(*foothold));
        EXPECT_NE(std::find(entrances.begin(), entrances.end(), *foothold), entrances.end());
        // No invented painted-road connection: the original graph cannot get here.
        const RoadRoutePlanner original(roads, destinations);
        for (const auto& route : original.routes({ start }, roadTileCentre(start), "Balmora"))
            EXPECT_NE(route.mDestination, "Ghostgate");
    }

    TEST(MWAccessibilityRoadRoutes, DestinationBearingIsNotTheRoadEntranceBearing)
    {
        RoadRoute route;
        // Entrance east of the player; the road bends north to its destination.
        route.mTiles = { { 2, 0 }, { 2, 1 }, { 2, 2 }, { 1, 2 }, { 0, 2 } };
        const auto bearing = roadDestinationBearing(route, roadTileCentre({ 0, 0 }));
        ASSERT_TRUE(bearing);
        EXPECT_FLOAT_EQ(*bearing, 0.f);
    }

    TEST(MWAccessibilityRoadRoutes, DestinationBearingUsesCurrentPlayerPosition)
    {
        RoadRoute route;
        route.mTiles = { { 2, 0 }, { 0, 2 } };
        const auto bearing = roadDestinationBearing(route, roadTileCentre({ -2, 2 }));
        ASSERT_TRUE(bearing);
        EXPECT_FLOAT_EQ(*bearing, std::atan2(1.f, 0.f));
    }

    TEST(MWAccessibilityRoadRoutes, DestinationBearingDoesNotInventNorthForMissingOrCoincidentPoints)
    {
        RoadRoute route;
        EXPECT_FALSE(roadDestinationBearing(route, osg::Vec2f(0, 0)));
        route.mTiles = { { 0, 0 } };
        EXPECT_FALSE(roadDestinationBearing(route, roadTileCentre({ 0, 0 })));
        EXPECT_FALSE(roadDestinationBearing(route, osg::Vec2f(std::numeric_limits<float>::quiet_NaN(), 0)));
    }

    TEST(MWAccessibilityRoadRoutes, CheckpointsAcceptTheirAreaButNotRemoteProxyArrival)
    {
        const RoadTile tile{ -4, 8 };
        const auto centre = roadTileCentre(tile);
        EXPECT_TRUE(roadCheckpointReached(tile, centre));
        EXPECT_TRUE(roadCheckpointReached(tile, centre + osg::Vec2f(256, 256)));
        EXPECT_TRUE(roadCheckpointReached(tile, centre + osg::Vec2f(210, 210)));
        EXPECT_FALSE(roadCheckpointReached(tile, centre + osg::Vec2f(512, 0)));
        EXPECT_FALSE(roadCheckpointReached(tile, osg::Vec2f(std::numeric_limits<float>::quiet_NaN(), 0)));
    }

    TEST(MWAccessibilityRoadRoutes, RealBalmoraStartReachesCalderaWhenGameDataProvided)
    {
        // Generated from the player's own files by audit_road_routes.py
        // --test-data. CI needs no licensed game assets; local validation can
        // exercise the actual previously reported Balmora start against vanilla
        // and a winning modded load order, rather than a simplified fixture.
        const char* path = std::getenv("OPENMW_ROAD_TEST_DATA");
        if (!path)
            GTEST_SKIP() << "Set OPENMW_ROAD_TEST_DATA to audit_road_routes.py --test-data output";
        std::ifstream input(path);
        ASSERT_TRUE(input.is_open());
        std::vector<RoadTile> tiles;
        std::map<std::string, std::vector<RoadTile>> names;
        RoadTile tile;
        std::string name;
        while (input >> tile.mX >> tile.mY >> std::quoted(name))
        {
            tiles.push_back(tile);
            if (!name.empty())
                names[name].push_back(tile);
        }
        ASSERT_TRUE(input.eof());
        ASSERT_GT(tiles.size(), 1000u);
        std::vector<RoadDestination> places;
        for (const auto& [place, anchors] : names)
            places.push_back({ place, anchors });
        RoadRoutePlanner planner(tiles, places);
        const RoadTile start{ -35, -16 };
        const auto routes = planner.routes({ start }, roadTileCentre(start), "Balmora");
        const auto caldera = std::find_if(
            routes.begin(), routes.end(), [](const auto& route) { return route.mDestination == "Caldera"; });
        ASSERT_NE(caldera, routes.end());
        EXPECT_EQ(caldera->mTiles.front(), start);
        EXPECT_NE(std::find(names["Caldera"].begin(), names["Caldera"].end(), caldera->mTiles.back()),
            names["Caldera"].end());
        for (const auto& route : routes)
            EXPECT_NE(route.mDestination, "Balmora");
        std::set<std::pair<int, int>> visited;
        for (std::size_t i = 0; i < caldera->mTiles.size(); ++i)
        {
            const auto& point = caldera->mTiles[i];
            EXPECT_TRUE(visited.emplace(point.mX, point.mY).second);
            if (i)
            {
                EXPECT_LE(std::abs(point.mX - caldera->mTiles[i - 1].mX), 1);
                EXPECT_LE(std::abs(point.mY - caldera->mTiles[i - 1].mY), 1);
            }
        }
    }

    TEST(MWAccessibilityRoadRoutes, NormalisesDistrictsWithoutChangingDisplayCase)
    {
        EXPECT_EQ(roadPlaceName(" \tBal Foyen , Docks"), "Bal Foyen");
        EXPECT_EQ(roadPlaceName("Caldera"), "Caldera");
        EXPECT_EQ(roadPlaceName(" \r\n"), "");
        EXPECT_EQ(roadPlaceName(", Docks"), "");
        EXPECT_EQ(roadPlaceName(""), "");
    }

    TEST(MWAccessibilityRoadRoutes, SeparatesApproachFromCompleteRoadPath)
    {
        RoadRoutePlanner planner({ { 0, 0 }, { 1, 0 }, { 2, 0 } }, { { "Caldera", { { 2, 0 } } } });
        auto routes = planner.routes({ { 0, 0 } }, roadTileCentre({ 0, 0 }) + osg::Vec2f(0, 120), "");
        ASSERT_EQ(routes.size(), 1u);
        EXPECT_EQ(routes[0].mDestination, "Caldera");
        EXPECT_EQ(routes[0].mTiles, (std::vector<RoadTile>{ { 0, 0 }, { 1, 0 }, { 2, 0 } }));
        EXPECT_FLOAT_EQ(routes[0].mLength, 1024.f);
        EXPECT_FLOAT_EQ(routes[0].mApproachLength, 120.f);
    }

    TEST(MWAccessibilityRoadRoutes, AllowsRealDiagonalTilesButDoesNotInventBridges)
    {
        RoadRoutePlanner planner({ { 0, 0 }, { 1, 1 }, { 2, 2 }, { 4, 2 } },
            { { "Diagonal", { { 2, 2 } } }, { "Across gap", { { 4, 2 } } }, { "Off road", { { 3, 2 } } } });
        auto routes = planner.routes({ { 0, 0 } }, roadTileCentre({ 0, 0 }), "");
        ASSERT_EQ(routes.size(), 1u);
        EXPECT_EQ(routes[0].mDestination, "Diagonal");
        EXPECT_NEAR(routes[0].mLength, 1024 * std::sqrt(2.f), 0.001f);
    }

    TEST(MWAccessibilityRoadRoutes, SearchesBranchesAndCornersRatherThanContinuingStraight)
    {
        RoadRoutePlanner planner({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 1, 1 }, { 1, 2 }, { 0, 3 }, { -1, 3 } },
            { { "Side branch", { { -1, 3 } } }, { "Straight", { { 3, 0 } } } });
        auto routes = planner.routes({ { 0, 0 } }, roadTileCentre({ 0, 0 }), "");
        ASSERT_EQ(routes.size(), 2u);
        EXPECT_EQ(routes[0].mDestination, "Straight");
        EXPECT_EQ(routes[1].mTiles, (std::vector<RoadTile>{ { 0, 0 }, { 1, 1 }, { 1, 2 }, { 0, 3 }, { -1, 3 } }));
    }

    TEST(MWAccessibilityRoadRoutes, GroupsDistrictAnchorsAndExcludesOriginCaseInsensitively)
    {
        RoadRoutePlanner planner({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } },
            { { "Balmora, Docks", { { 1, 0 } } }, { "BALMORA", { { 2, 0 } } }, { "Bal Foyen, Docks", { { 3, 0 } } },
                { "Bal Foyen", { { 2, 0 } } }, { "bal foyen, Market", { { 3, 0 } } }, { " , Empty", { { 1, 0 } } } });
        auto routes = planner.routes({ { 0, 0 } }, roadTileCentre({ 0, 0 }), " balmora, Market ");
        ASSERT_EQ(routes.size(), 1u);
        EXPECT_EQ(routes[0].mDestination, "Bal Foyen");
        EXPECT_EQ(routes[0].mTiles.back(), (RoadTile{ 2, 0 }));
    }

    TEST(MWAccessibilityRoadRoutes, MultiSourceCostsIncludeApproachNotJustRemainingRoad)
    {
        RoadRoutePlanner planner({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } }, { { "End", { { 2, 0 } } } });
        auto routes = planner.routes({ { 3, 0 }, { 0, 0 } }, roadTileCentre({ 0, 0 }), "");
        ASSERT_EQ(routes.size(), 1u);
        EXPECT_EQ(routes[0].mTiles.front(), (RoadTile{ 0, 0 }));
        EXPECT_FLOAT_EQ(routes[0].mLength, 1024.f);
        EXPECT_FLOAT_EQ(routes[0].mApproachLength, 0.f);
        routes = planner.routes({ { 0, 0 }, { 3, 0 } }, roadTileCentre({ 3, 0 }), "");
        ASSERT_EQ(routes.size(), 1u);
        EXPECT_EQ(routes[0].mTiles.front(), (RoadTile{ 3, 0 }));
        EXPECT_FLOAT_EQ(routes[0].mLength, 512.f);
    }

    TEST(MWAccessibilityRoadRoutes, SortsByTotalThenCaseInsensitiveName)
    {
        RoadRoutePlanner planner({ { 0, 0 }, { 1, 0 }, { 10, 0 }, { 11, 0 } },
            { { "zebra", { { 1, 0 } } }, { "Alpha", { { 1, 0 } } }, { "Far", { { 11, 0 } } } });
        auto routes = planner.routes({ { 10, 0 }, { 0, 0 } }, roadTileCentre({ 0, 0 }), "");
        ASSERT_EQ(routes.size(), 3u);
        EXPECT_EQ(routes[0].mDestination, "Alpha");
        EXPECT_EQ(routes[1].mDestination, "zebra");
        EXPECT_EQ(routes[2].mDestination, "Far");
        EXPECT_FLOAT_EQ(routes[2].mLength, 512.f);
        EXPECT_FLOAT_EQ(routes[2].mApproachLength, 5120.f);
    }

    TEST(MWAccessibilityRoadRoutes, IgnoresMissingInputsAndDropsZeroRoadDistance)
    {
        RoadRoutePlanner empty({}, { { "Nowhere", { { 0, 0 } } } });
        EXPECT_TRUE(empty.routes({ { 0, 0 } }, osg::Vec2f(), "").empty());
        RoadRoutePlanner planner({ { 0, 0 }, { 1, 0 }, { 1, 0 } },
            { { "Here", { { 0, 0 }, { 1, 0 } } }, { "There", { { 1, 0 }, { 1, 0 }, { 99, 99 } } } });
        EXPECT_TRUE(planner.routes({}, osg::Vec2f(), "").empty());
        EXPECT_TRUE(planner.routes({ { 99, 99 } }, osg::Vec2f(), "").empty());
        auto routes = planner.routes({ { 0, 0 }, { 0, 0 }, { 99, 99 } }, roadTileCentre({ 0, 0 }), "");
        ASSERT_EQ(routes.size(), 1u);
        EXPECT_EQ(routes[0].mDestination, "There");
        EXPECT_EQ(routes[0].mTiles.size(), 2u);
        EXPECT_TRUE(planner.routes({ { 0, 0 } }, osg::Vec2f(std::numeric_limits<float>::infinity(), 0), "").empty());
        EXPECT_TRUE(planner.routes({ { 0, 0 } }, osg::Vec2f(0, std::numeric_limits<float>::quiet_NaN()), "").empty());
    }

    TEST(MWAccessibilityRoadRoutes, LoopsHaveNoRepeatedTilesAndTiesIgnoreInputOrder)
    {
        std::vector<RoadTile> tiles{ { 0, 0 }, { 1, 1 }, { 2, 0 }, { 1, -1 } };
        RoadRoutePlanner first(tiles, { { "End", { { 2, 0 } } } });
        std::reverse(tiles.begin(), tiles.end());
        RoadRoutePlanner second(tiles, { { "End", { { 2, 0 } } } });
        auto a = first.routes({ { 0, 0 } }, roadTileCentre({ 0, 0 }), "");
        auto b = second.routes({ { 0, 0 } }, roadTileCentre({ 0, 0 }), "");
        ASSERT_EQ(a.size(), 1u);
        ASSERT_EQ(b.size(), 1u);
        EXPECT_EQ(a[0].mTiles, b[0].mTiles);
        EXPECT_EQ(a[0].mTiles, (std::vector<RoadTile>{ { 0, 0 }, { 1, -1 }, { 2, 0 } }));
        std::set<std::pair<int, int>> seen;
        for (const auto& tile : a[0].mTiles)
            EXPECT_TRUE(seen.emplace(tile.mX, tile.mY).second);
    }

    TEST(MWAccessibilityRoadRoutes, HasNoFixedStepCeiling)
    {
        std::vector<RoadTile> tiles;
        for (int x = 0; x <= 2000; ++x)
            tiles.push_back({ x, -4 });
        RoadRoutePlanner planner(tiles, { { "Remote", { tiles.back() } } });
        auto routes = planner.routes({ tiles.front() }, roadTileCentre(tiles.front()), "");
        ASSERT_EQ(routes.size(), 1u);
        EXPECT_EQ(routes[0].mTiles, tiles);
        EXPECT_FLOAT_EQ(routes[0].mLength, 2000 * kRoadTileSize);
    }

    TEST(MWAccessibilityRoadRoutes, IntegerBoundaryNeighboursDoNotWrap)
    {
        constexpr auto low = std::numeric_limits<std::int32_t>::min();
        constexpr auto high = std::numeric_limits<std::int32_t>::max();
        RoadRoutePlanner planner({ { low, low }, { high, high }, { high - 1, high } },
            { { "Across integer wrap", { { low, low } } }, { "Adjacent", { { high - 1, high } } } });
        auto routes = planner.routes({ { high, high } }, roadTileCentre({ high, high }), "");
        ASSERT_EQ(routes.size(), 1u);
        EXPECT_EQ(routes[0].mDestination, "Adjacent");
        EXPECT_FLOAT_EQ(routes[0].mLength, 512.f);
    }
}
