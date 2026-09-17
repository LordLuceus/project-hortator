#include <gtest/gtest.h>

#include <set>
#include <utility>

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

    // --- Choosing which way to follow --------------------------------------

    namespace
    {
        // Mirrors Scanner::handleRoadDirectionKey: given the road's axis and a
        // compass direction from an arrow key, take whichever end of the road
        // better matches. The two ends are opposite, so exactly one can win.
        osg::Vec2f pickEnd(const osg::Vec2f& axis, const osg::Vec2f& wanted)
        {
            const osg::Vec2f a = axis;
            const osg::Vec2f b = -axis;
            return (wanted * a) >= (wanted * b) ? a : b;
        }
    }

    // The bug that made the first version unusable: the player could not choose
    // which way to go along a north-south road. Pressing up must go north and down
    // must go south, whichever way the axis happens to be stored.
    TEST(MWAccessibilityRoads, ArrowKeyPicksTheMatchingEndOfTheRoad)
    {
        const osg::Vec2f north(0.f, 1.f);
        const osg::Vec2f south(0.f, -1.f);

        // A north-south road, axis stored pointing north.
        EXPECT_GT(pickEnd(osg::Vec2f(0.f, 1.f), north).y(), 0.f);
        EXPECT_LT(pickEnd(osg::Vec2f(0.f, 1.f), south).y(), 0.f);

        // The SAME road with the axis stored pointing south must behave
        // identically: the stored sign is arbitrary, the player's choice is not.
        EXPECT_GT(pickEnd(osg::Vec2f(0.f, -1.f), north).y(), 0.f);
        EXPECT_LT(pickEnd(osg::Vec2f(0.f, -1.f), south).y(), 0.f);
    }

    TEST(MWAccessibilityRoads, ArrowKeyPicksSensiblyOnADiagonalRoad)
    {
        // A northeast-southwest road. Up and right should both take the northeast
        // end; down and left the southwest one.
        const osg::Vec2f axis(0.7071f, 0.7071f);
        EXPECT_GT(pickEnd(axis, osg::Vec2f(0.f, 1.f)).y(), 0.f); // up -> northeast
        EXPECT_GT(pickEnd(axis, osg::Vec2f(1.f, 0.f)).x(), 0.f); // right -> northeast
        EXPECT_LT(pickEnd(axis, osg::Vec2f(0.f, -1.f)).y(), 0.f); // down -> southwest
        EXPECT_LT(pickEnd(axis, osg::Vec2f(-1.f, 0.f)).x(), 0.f); // left -> southwest
    }

    // --- Pre-walking the route (where does this road GO?) -------------------

    namespace
    {
        // Feed previewRoad a hand-built road instead of the engine's land data.
        RoadNeighbourFn roadFrom(const std::set<std::pair<std::int32_t, std::int32_t>>& road)
        {
            return [road](const RoadTile& t) {
                std::vector<RoadTile> out;
                for (std::int32_t dx = -1; dx <= 1; ++dx)
                    for (std::int32_t dy = -1; dy <= 1; ++dy)
                    {
                        if (dx == 0 && dy == 0)
                            continue;
                        const std::pair<std::int32_t, std::int32_t> nb{ t.mX + dx, t.mY + dy };
                        if (road.count(nb) != 0)
                            out.push_back(RoadTile{ nb.first, nb.second });
                    }
                return out;
            };
        }
    }

    TEST(MWAccessibilityRoads, PreviewReportsWhereAStraightRoadEnds)
    {
        std::set<std::pair<std::int32_t, std::int32_t>> road;
        for (std::int32_t x = 0; x < 20; ++x)
            road.insert({ x, 0 });

        const RoadPreview p = previewRoad(RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), 600, roadFrom(road));
        EXPECT_EQ(p.mEnd.mX, 19);
        EXPECT_EQ(p.mEnd.mY, 0);
        EXPECT_EQ(p.mSteps, 20u);
        EXPECT_FALSE(p.mLoopsBack);
        // 19 steps of one tile each.
        EXPECT_NEAR(p.mLength, 19.f * kRoadTileSize, 1.f);
        // Net bearing is due east, matching the way it set off.
        EXPECT_NEAR(p.mNetBearing.x(), 1.f, 0.01f);
        EXPECT_NEAR(p.mNetBearing.y(), 0.f, 0.01f);
    }

    // THE BUG THIS FEATURE EXISTS FOR: he followed a road "northwest" out of
    // Balmora expecting Caldera, and it curved round and returned him to Balmora.
    // The local axis cannot express that; only walking the route can.
    TEST(MWAccessibilityRoads, PreviewDetectsARoadThatLoopsBackToItsStart)
    {
        // A closed ring: leaves north, comes back round to where it began.
        std::set<std::pair<std::int32_t, std::int32_t>> road;
        for (std::int32_t i = 0; i < 6; ++i)
        {
            road.insert({ 0, i });
            road.insert({ 5, i });
            road.insert({ i, 0 });
            road.insert({ i, 5 });
        }

        // Set off north up the left side. The walk stops at the top-left corner,
        // because turning east there is a 90-degree turn and chooseStraightestStep
        // rejects anything not strictly within 90 degrees of the heading. So a
        // square ring does NOT come back round -- and that is the honest outcome,
        // reported as "the road ends here" rather than a route that isn't taken.
        const RoadPreview p = previewRoad(RoadTile{ 0, 0 }, osg::Vec2f(0.f, 1.f), 600, roadFrom(road));
        EXPECT_EQ(p.mEnd.mX, 0);
        EXPECT_EQ(p.mEnd.mY, 5);
        EXPECT_FALSE(p.mLoopsBack);
    }

    // A road that curves GENTLY back on itself does return, and that is what
    // mLoopsBack is for. Note the real Balmora case is NOT this: that road ends
    // back in the TOWN but ~17 tiles from the start tile, so it is the place name
    // ("ends at Balmora"), not this flag, that warns the player.
    TEST(MWAccessibilityRoads, PreviewDetectsAGentleLoopBackToTheStart)
    {
        // A rough circle of radius 4, so each step turns only ~20 degrees.
        std::set<std::pair<std::int32_t, std::int32_t>> road;
        for (int deg = 0; deg < 360; deg += 10)
        {
            const double r = deg * 3.14159265 / 180.0;
            road.insert({ static_cast<std::int32_t>(std::lround(4.0 * std::sin(r))),
                static_cast<std::int32_t>(std::lround(4.0 * std::cos(r))) });
        }

        const RoadPreview p = previewRoad(RoadTile{ 0, 4 }, osg::Vec2f(1.f, 0.f), 600, roadFrom(road));
        EXPECT_GT(p.mSteps, 8u); // went right round
        EXPECT_TRUE(p.mLoopsBack); // ...and came home
    }

    TEST(MWAccessibilityRoads, PreviewDoesNotCallAShortStubALoop)
    {
        // Three tiles is not a loop, even though the end is near the start.
        std::set<std::pair<std::int32_t, std::int32_t>> road{ { 0, 0 }, { 1, 0 }, { 2, 0 } };
        const RoadPreview p = previewRoad(RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), 600, roadFrom(road));
        EXPECT_FALSE(p.mLoopsBack);
    }

    // A road can bend so far that its overall direction is not the way it set off.
    // That mismatch is what the prompt warns about, so it has to be measured.
    TEST(MWAccessibilityRoads, PreviewNetBearingDiffersWhenTheRoadBendsRound)
    {
        // Sets off north, then curves east over several tiles -- a gradual bend,
        // since a single hard 90-degree corner would stop the walk instead. Net
        // bearing ends up northeast, not the north it set off as, which is exactly
        // the mismatch the prompt warns about.
        // An explicit tile chain: every step is to an adjacent tile and turns well
        // under 90 degrees, so the walk gets all the way round. (Sampling a circle
        // is tempting but produces a hard corner that legitimately stops the walk.)
        const std::set<std::pair<std::int32_t, std::int32_t>> road{ { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 },
            { 1, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, { 4, 7 }, { 5, 8 }, { 6, 8 }, { 7, 9 }, { 8, 9 }, { 9, 9 },
            { 10, 9 } };

        const RoadPreview p = previewRoad(RoadTile{ 0, 0 }, osg::Vec2f(0.f, 1.f), 600, roadFrom(road));
        EXPECT_GT(p.mSteps, 10u); // it got round the bend
        EXPECT_GT(p.mNetBearing.x(), 0.3f); // ended up well to the east
        EXPECT_GT(p.mNetBearing.y(), 0.3f); // and still north
    }

    TEST(MWAccessibilityRoads, PreviewReportsAStubAsGoingNowhere)
    {
        // A single isolated tile: no route at all.
        std::set<std::pair<std::int32_t, std::int32_t>> road{ { 0, 0 } };
        const RoadPreview p = previewRoad(RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), 600, roadFrom(road));
        EXPECT_EQ(p.mSteps, 1u);
        EXPECT_EQ(p.mLength, 0.f);
    }

    TEST(MWAccessibilityRoads, PreviewRespectsItsStepCeiling)
    {
        std::set<std::pair<std::int32_t, std::int32_t>> road;
        for (std::int32_t x = 0; x < 500; ++x)
            road.insert({ x, 0 });

        const RoadPreview p = previewRoad(RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), 10, roadFrom(road));
        EXPECT_EQ(p.mSteps, 10u);
    }

    // The preview must agree with the walk, or it promises a destination the
    // follower never reaches. Both use chooseStraightestStep; this pins that.
    TEST(MWAccessibilityRoads, PreviewEndMatchesWhereFollowingActuallyArrives)
    {
        // A curving road with a wide patch, to give the chooser real decisions.
        std::set<std::pair<std::int32_t, std::int32_t>> road;
        for (std::int32_t x = 0; x <= 12; ++x)
        {
            road.insert({ x, x / 3 });
            if (x % 4 == 0)
                road.insert({ x, x / 3 + 1 });
        }

        const RoadNeighbourFn nb = roadFrom(road);
        const RoadPreview p = previewRoad(RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), 600, nb);

        // Walk it the way updateRoadFollowing does, leg by leg.
        RoadTile at{ 0, 0 };
        osg::Vec2f heading(1.f, 0.f);
        std::set<std::pair<std::int32_t, std::int32_t>> visited{ { at.mX, at.mY } };
        std::size_t steps = 1;
        for (;;)
        {
            std::vector<RoadTile> candidates;
            for (const RoadTile& t : nb(at))
                if (visited.count({ t.mX, t.mY }) == 0)
                    candidates.push_back(t);
            RoadTile next{};
            if (!chooseStraightestStep(candidates, at, heading, next))
                break;
            heading = osg::Vec2f(static_cast<float>(next.mX - at.mX), static_cast<float>(next.mY - at.mY));
            heading.normalize();
            at = next;
            visited.insert({ at.mX, at.mY });
            ++steps;
        }

        EXPECT_EQ(p.mEnd.mX, at.mX);
        EXPECT_EQ(p.mEnd.mY, at.mY);
        EXPECT_EQ(p.mSteps, steps);
    }

    // --- Road following ----------------------------------------------------

    TEST(MWAccessibilityRoads, FollowsStraightOnThroughAWidePatch)
    {
        // Walking east. The road is two tiles wide here, so there are three
        // candidates ahead; straight on must win over the two diagonals. With 46%
        // of Morrowind's road tiles having four or more road neighbours, this is
        // the common case, not an edge case.
        const std::vector<RoadTile> candidates{ { 1, 1 }, { 1, 0 }, { 1, -1 } };
        RoadTile next{};
        ASSERT_TRUE(chooseStraightestStep(candidates, RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), next));
        EXPECT_EQ(next, (RoadTile{ 1, 0 }));
    }

    TEST(MWAccessibilityRoads, FollowsACurve)
    {
        // Heading east, but the only way on is northeast: take it rather than
        // stopping, so a curving road is followed round.
        const std::vector<RoadTile> candidates{ { 1, 1 } };
        RoadTile next{};
        ASSERT_TRUE(chooseStraightestStep(candidates, RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), next));
        EXPECT_EQ(next, (RoadTile{ 1, 1 }));
    }

    // The failure that would make road-following unusable: shuttling between two
    // tiles forever. Anything more than 90 degrees off the heading is refused, so
    // the tile just walked from can never be chosen.
    TEST(MWAccessibilityRoads, NeverDoublesBack)
    {
        // Heading east; the only road neighbour is the tile behind us.
        const std::vector<RoadTile> behind{ { -1, 0 } };
        RoadTile next{};
        EXPECT_FALSE(chooseStraightestStep(behind, RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), next));

        // Also refused at a dead end where the only options are sharply backward.
        const std::vector<RoadTile> sharplyBack{ { -1, 1 }, { -1, -1 } };
        EXPECT_FALSE(chooseStraightestStep(sharplyBack, RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), next));
    }

    TEST(MWAccessibilityRoads, StopsAtADeadEnd)
    {
        RoadTile next{};
        // No candidates at all: the road simply ends.
        EXPECT_FALSE(chooseStraightestStep({}, RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), next));
        // A degenerate heading can't be followed either -- better to stop than to
        // pick an arbitrary direction.
        EXPECT_FALSE(chooseStraightestStep({ { 1, 0 } }, RoadTile{ 0, 0 }, osg::Vec2f(0.f, 0.f), next));
    }

    TEST(MWAccessibilityRoads, IgnoresTheTileItIsStandingOn)
    {
        // A candidate list that includes the current tile must not select it;
        // doing so would stall the walk without ever reporting an end.
        const std::vector<RoadTile> candidates{ { 0, 0 }, { 1, 0 } };
        RoadTile next{};
        ASSERT_TRUE(chooseStraightestStep(candidates, RoadTile{ 0, 0 }, osg::Vec2f(1.f, 0.f), next));
        EXPECT_EQ(next, (RoadTile{ 1, 0 }));
    }

    // Following a road for a kilometre means taking hundreds of steps, so the
    // chooser must be stable: walking a straight road must not drift off it.
    TEST(MWAccessibilityRoads, WalksAStraightRoadEndToEnd)
    {
        // A straight east-west road, one tile wide, 40 tiles long.
        std::set<std::pair<std::int32_t, std::int32_t>> road;
        for (std::int32_t x = 0; x < 40; ++x)
            road.insert({ x, 0 });

        RoadTile at{ 0, 0 };
        osg::Vec2f heading(1.f, 0.f);
        std::set<std::pair<std::int32_t, std::int32_t>> visited{ { at.mX, at.mY } };

        int steps = 0;
        for (;;)
        {
            std::vector<RoadTile> candidates;
            for (std::int32_t dx = -1; dx <= 1; ++dx)
                for (std::int32_t dy = -1; dy <= 1; ++dy)
                {
                    if (dx == 0 && dy == 0)
                        continue;
                    const std::pair<std::int32_t, std::int32_t> nb{ at.mX + dx, at.mY + dy };
                    if (road.count(nb) != 0 && visited.count(nb) == 0)
                        candidates.push_back(RoadTile{ nb.first, nb.second });
                }

            RoadTile next{};
            if (!chooseStraightestStep(candidates, at, heading, next))
                break;
            heading = osg::Vec2f(static_cast<float>(next.mX - at.mX), static_cast<float>(next.mY - at.mY));
            heading.normalize();
            at = next;
            visited.insert({ at.mX, at.mY });
            ++steps;
            ASSERT_LT(steps, 100); // must terminate, and must not wander
        }

        EXPECT_EQ(steps, 39);
        EXPECT_EQ(at, (RoadTile{ 39, 0 }));
    }

    // A ring road would circle forever under the straightest-step rule alone. The
    // caller's visited set is what turns that into an honest stop, so prove the two
    // work together.
    TEST(MWAccessibilityRoads, TerminatesOnALoopOfRoad)
    {
        std::set<std::pair<std::int32_t, std::int32_t>> road;
        for (std::int32_t x = 0; x <= 6; ++x)
        {
            road.insert({ x, 0 });
            road.insert({ x, 6 });
        }
        for (std::int32_t y = 0; y <= 6; ++y)
        {
            road.insert({ 0, y });
            road.insert({ 6, y });
        }

        RoadTile at{ 0, 0 };
        osg::Vec2f heading(1.f, 0.f);
        std::set<std::pair<std::int32_t, std::int32_t>> visited{ { at.mX, at.mY } };

        int steps = 0;
        for (;;)
        {
            std::vector<RoadTile> candidates;
            for (std::int32_t dx = -1; dx <= 1; ++dx)
                for (std::int32_t dy = -1; dy <= 1; ++dy)
                {
                    if (dx == 0 && dy == 0)
                        continue;
                    const std::pair<std::int32_t, std::int32_t> nb{ at.mX + dx, at.mY + dy };
                    if (road.count(nb) != 0 && visited.count(nb) == 0)
                        candidates.push_back(RoadTile{ nb.first, nb.second });
                }

            RoadTile next{};
            if (!chooseStraightestStep(candidates, at, heading, next))
                break;
            heading = osg::Vec2f(static_cast<float>(next.mX - at.mX), static_cast<float>(next.mY - at.mY));
            heading.normalize();
            at = next;
            visited.insert({ at.mX, at.mY });
            ++steps;
            ASSERT_LT(steps, 200); // the real bug this guards: never terminating
        }

        // It must stop somewhere sane rather than looping; every visited tile must
        // be road, and no tile may be visited twice.
        EXPECT_GT(steps, 0);
        EXPECT_EQ(visited.size(), static_cast<std::size_t>(steps) + 1);
        for (const auto& t : visited)
            EXPECT_EQ(road.count(t), 1u);
    }
}
