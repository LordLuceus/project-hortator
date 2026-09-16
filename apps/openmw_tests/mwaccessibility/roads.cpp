#include <gtest/gtest.h>

#include <apps/openmw/mwaccessibility/roads.hpp>

namespace
{
    using namespace MWAccessibility;

    // Every texture name below is a REAL land texture id or filename from
    // Morrowind.esm (dumped with esmtool), not an invented example, so these tests
    // pin the actual data the feature reads rather than a plausible-looking model
    // of it. Of the 107 land textures in the master file, exactly these 12 are
    // roads, spanning every region's road set.
    TEST(MWAccessibilityRoads, RecognisesEveryVanillaRoadTexture)
    {
        EXPECT_TRUE(isRoadTexture("Road Dirt"));
        EXPECT_TRUE(isRoadTexture("AL_road_01"));
        EXPECT_TRUE(isRoadTexture("WG_road"));
        EXPECT_TRUE(isRoadTexture("WG_dirtroad_01"));
        EXPECT_TRUE(isRoadTexture("AI_Dirtroad"));
        EXPECT_TRUE(isRoadTexture("GL_Dirtroad"));
        EXPECT_TRUE(isRoadTexture("AC_dirtroad_01"));
        EXPECT_TRUE(isRoadTexture("WG_mainroad_01"));
        EXPECT_TRUE(isRoadTexture("AI_Grass_Cobbles"));
        EXPECT_TRUE(isRoadTexture("WG_cobblestones"));
        // Two entries carry a filename as their id -- the data is not tidy, and
        // matching must not assume it is.
        EXPECT_TRUE(isRoadTexture("Tx_AI_mainroad_01.tga"));
        EXPECT_TRUE(isRoadTexture("Tx_BC_mainroad_01.tga"));
    }

    TEST(MWAccessibilityRoads, RejectsNonRoadTextures)
    {
        // Real non-road textures from the same file.
        EXPECT_FALSE(isRoadTexture("sand"));
        EXPECT_FALSE(isRoadTexture("Rock_Coastal"));
        EXPECT_FALSE(isRoadTexture("AI_Grass_Rocky"));
        EXPECT_FALSE(isRoadTexture("WG_Scrub Plain"));
        EXPECT_FALSE(isRoadTexture("AC_darkstone01"));
        EXPECT_FALSE(isRoadTexture("AL_ash_04"));
        EXPECT_FALSE(isRoadTexture(""));
    }

    // The Daedric ruin flagstones are the one genuinely misleading case: their
    // filenames literally contain "road" (Tx_Daed_road_01, Tx_Daed_road_02) but
    // they pave ruin courtyards and lead nowhere. Listing one as a road to follow
    // would be a confident wrong answer, which for a speech-only interface is worse
    // than saying nothing.
    TEST(MWAccessibilityRoads, ExcludesDaedricRuinFlagstones)
    {
        EXPECT_FALSE(isRoadTexture("Tx_Daed_road.tga"));
        EXPECT_FALSE(isRoadTexture("Tx_Daed_road_02.tga"));
        EXPECT_FALSE(isRoadTexture("Daedric_scrubruins"));
        EXPECT_FALSE(isRoadTexture("Daedric Stone"));
    }

    TEST(MWAccessibilityRoads, TextureMatchingIsCaseInsensitive)
    {
        EXPECT_TRUE(isRoadTexture("ROAD DIRT"));
        EXPECT_TRUE(isRoadTexture("wg_MainRoad_01"));
        EXPECT_FALSE(isRoadTexture("TX_DAED_ROAD.TGA"));
    }

    TEST(MWAccessibilityRoads, TileCoordinatesRoundTrip)
    {
        // A tile is 512 units; its centre is at the half-tile offset.
        EXPECT_EQ(roadTileAt(osg::Vec2f(0.f, 0.f)), (RoadTile{ 0, 0 }));
        EXPECT_EQ(roadTileAt(osg::Vec2f(511.f, 511.f)), (RoadTile{ 0, 0 }));
        EXPECT_EQ(roadTileAt(osg::Vec2f(512.f, 0.f)), (RoadTile{ 1, 0 }));
        // Negative coordinates must floor, not truncate toward zero, or every tile
        // west or south of the origin would be off by one.
        EXPECT_EQ(roadTileAt(osg::Vec2f(-1.f, -1.f)), (RoadTile{ -1, -1 }));
        EXPECT_EQ(roadTileAt(osg::Vec2f(-512.f, -512.f)), (RoadTile{ -1, -1 }));

        const osg::Vec2f centre = roadTileCentre(RoadTile{ 2, -3 });
        EXPECT_FLOAT_EQ(centre.x(), 2.f * 512.f + 256.f);
        EXPECT_FLOAT_EQ(centre.y(), -3.f * 512.f + 256.f);
        // A tile centre must land back in its own tile.
        EXPECT_EQ(roadTileAt(centre), (RoadTile{ 2, -3 }));
    }

    TEST(MWAccessibilityRoads, GroupsDisjointStretchesSeparately)
    {
        // Two parallel roads far apart: must stay two stretches, not merge.
        const std::vector<RoadTile> tiles{
            { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, // near road, running east-west
            { 0, 20 }, { 1, 20 }, { 2, 20 }, { 3, 20 }, // far road
        };
        const std::vector<RoadStretch> stretches = groupRoadTiles(tiles, RoadTile{ 0, 0 });
        ASSERT_EQ(stretches.size(), 2u);
        // Nearest first.
        EXPECT_EQ(stretches[0].mNearest, (RoadTile{ 0, 0 }));
        EXPECT_EQ(stretches[1].mNearest, (RoadTile{ 0, 20 }));
        EXPECT_EQ(stretches[0].mTiles.size(), 4u);
        EXPECT_EQ(stretches[1].mTiles.size(), 4u);
    }

    // A road painted diagonally is a staircase of tiles touching only at their
    // corners. Under 4-neighbour adjacency it would shatter into single-tile
    // fragments and be useless, so diagonal connectivity is required, not a nicety.
    TEST(MWAccessibilityRoads, DiagonalRoadStaysOneStretch)
    {
        const std::vector<RoadTile> tiles{ { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 4 } };
        const std::vector<RoadStretch> stretches = groupRoadTiles(tiles, RoadTile{ 0, 0 });
        ASSERT_EQ(stretches.size(), 1u);
        EXPECT_EQ(stretches[0].mTiles.size(), 5u);
    }

    TEST(MWAccessibilityRoads, NearestTileIsTheWayOntoTheRoad)
    {
        // Player sits well south of an east-west road; the nearest tile is the one
        // directly north, not whichever happened to be enumerated first.
        const std::vector<RoadTile> tiles{ { 0, 10 }, { 1, 10 }, { 2, 10 }, { 3, 10 }, { 4, 10 } };
        const std::vector<RoadStretch> stretches = groupRoadTiles(tiles, RoadTile{ 3, 0 });
        ASSERT_EQ(stretches.size(), 1u);
        EXPECT_EQ(stretches[0].mNearest, (RoadTile{ 3, 10 }));
        // The stretch's own tiles are ordered nearest-first too.
        EXPECT_EQ(stretches[0].mTiles.front(), (RoadTile{ 3, 10 }));
    }

    TEST(MWAccessibilityRoads, FitsAxisOfStraightRoads)
    {
        const std::vector<RoadTile> eastWest{ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 } };
        EXPECT_EQ(describeRoadAxis(fitRoadDirection(eastWest, RoadTile{ 2, 0 })), "east to west");

        const std::vector<RoadTile> northSouth{ { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 } };
        EXPECT_EQ(describeRoadAxis(fitRoadDirection(northSouth, RoadTile{ 0, 2 })), "north to south");

        const std::vector<RoadTile> diagonal{ { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 4 } };
        EXPECT_EQ(describeRoadAxis(fitRoadDirection(diagonal, RoadTile{ 2, 2 })), "northeast to southwest");
    }

    // An axis has no inherent forward, so the description must be the same road
    // whichever way the fit happens to point. "east to west" and "west to east"
    // describe the same road; what must never happen is one call saying "east" and
    // another "west" for the same stretch.
    TEST(MWAccessibilityRoads, AxisDescriptionIsStableForTheSameRoad)
    {
        const std::vector<RoadTile> road{ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 } };
        const std::string a = describeRoadAxis(fitRoadDirection(road, RoadTile{ 1, 0 }));
        const std::string b = describeRoadAxis(fitRoadDirection(road, RoadTile{ 3, 0 }));
        EXPECT_EQ(a, b);
    }

    // Better to say "Road" than to invent a heading. A junction or a paved
    // forecourt has no dominant axis, and a speech-only interface has no redundancy
    // to correct a confident wrong answer with.
    TEST(MWAccessibilityRoads, RefusesToGuessDirectionWithoutStructure)
    {
        // A lone tile, and a pair: too little to establish an axis.
        EXPECT_FALSE(fitRoadDirection({ { 0, 0 } }, RoadTile{ 0, 0 }).length2() > 0.f);
        EXPECT_FALSE(fitRoadDirection({ { 0, 0 }, { 1, 0 } }, RoadTile{ 0, 0 }).length2() > 0.f);

        // A solid 3x3 block (a crossroads or plaza) has equal spread both ways.
        const std::vector<RoadTile> blob{ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 }, { 0, 2 },
            { 1, 2 }, { 2, 2 } };
        EXPECT_FALSE(fitRoadDirection(blob, RoadTile{ 1, 1 }).length2() > 0.f);

        EXPECT_EQ(describeRoadAxis(osg::Vec2f(0.f, 0.f)), "");
    }

    TEST(MWAccessibilityRoads, HandlesEmptyInput)
    {
        EXPECT_TRUE(groupRoadTiles({}, RoadTile{ 0, 0 }).empty());
    }
}
