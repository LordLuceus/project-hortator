#include <gtest/gtest.h>

#include "apps/openmw/mwaccessibility/foyadas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    using namespace MWAccessibility;
    constexpr float VertexSize = kRoadTileSize / 4;
    constexpr auto Low = std::numeric_limits<std::int32_t>::min();
    constexpr auto High = std::numeric_limits<std::int32_t>::max();

    TEST(Foyadas, ExactTextureNames)
    {
        for (const auto name : { "tx_ma_lavaflow", "Textures/TX_MA_LAVAFLOW.DDS", "foo\\tx_MA_lavaflow.tga" })
            EXPECT_EQ(classifyFoyadaTexture(name), FoyadaMaterial::Flow);
        for (const auto name : { "tx_ma_rock04", "Textures/TX_MA_ROCK04.TGA", "foo\\tx_ma_rock04.dds" })
            EXPECT_EQ(classifyFoyadaTexture(name), FoyadaMaterial::Rock);
        for (const auto name : { "", "lava", "ash", "rock", "tx_lava_01.dds", "tx_ma_lava.dds", "tx_ma_rock040.dds",
                 "tx_ma_rock04_extra.tga", "other_tx_ma_lavaflow.dds", "tx_ma_lavaflow.dds.bak", "tx_ma_lavaflow.png",
                 "tx_ma_lavaflow/other.dds" })
            EXPECT_EQ(classifyFoyadaTexture(name), FoyadaMaterial::None) << name;
    }

    TEST(Foyadas, FlatAndPlanarGradesAreSymmetric)
    {
        const TerrainVertexHeightFn flat = [](auto, auto) { return 42.f; };
        const TerrainVertexHeightFn plane = [](auto x, auto y) { return float(x + 2 * y) * VertexSize; };
        for (const RoadTile a : { RoadTile{ 0, 0 }, RoadTile{ -1, -1 }, RoadTile{ 15, 15 }, RoadTile{ -17, -17 } })
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                {
                    if (!dx && !dy)
                        continue;
                    const RoadTile b{ a.mX + dx, a.mY + dy };
                    EXPECT_FLOAT_EQ(foyadaEdgeGrade(a, b, flat), 0.f);
                    EXPECT_FLOAT_EQ(foyadaEdgeGrade(a, b, plane), std::sqrt(5.f));
                    EXPECT_EQ(foyadaEdgeGrade(a, b, plane), foyadaEdgeGrade(b, a, plane));
                }
    }

    TEST(Foyadas, LevelPathAcrossSteepHillsideIsNotFlat)
    {
        const TerrainVertexHeightFn hillside = [](auto, auto y) { return float(y) * 2 * VertexSize; };
        EXPECT_FLOAT_EQ(foyadaEdgeGrade({ 0, 0 }, { 1, 0 }, hillside), 2.f);
    }

    TEST(Foyadas, SharedGridAndTriangleEdgesAllowEitherIncidentFace)
    {
        // Along y=2, the south face is level and the north face is steep.
        const TerrainVertexHeightFn gridEdge
            = [](auto, auto y) { return float(std::max<std::int64_t>(0, y - 2)) * 8 * VertexSize; };
        EXPECT_FLOAT_EQ(foyadaEdgeGrade({ 0, 0 }, { 1, 0 }, gridEdge), 0.f);
        // Along x+y=8 (an anti-diagonal triangle edge), one face is level.
        const TerrainVertexHeightFn triangleEdge
            = [](auto x, auto y) { return float(std::max<std::int64_t>(0, x + y - 8)) * 8 * VertexSize; };
        EXPECT_FLOAT_EQ(foyadaEdgeGrade({ 0, 1 }, { 1, 0 }, triangleEdge), 0.f);
        EXPECT_FLOAT_EQ(foyadaEdgeGrade({ 1, 0 }, { 0, 1 }, triangleEdge), 0.f);
    }

    TEST(Foyadas, UnknownAndInvalidTerrainFailsClosed)
    {
        const TerrainVertexHeightFn flat = [](auto, auto) { return 0.f; };
        EXPECT_TRUE(std::isinf(foyadaEdgeGrade({ 0, 0 }, { 0, 0 }, flat)));
        EXPECT_TRUE(std::isinf(foyadaEdgeGrade({ 0, 0 }, { 2, 0 }, flat)));
        EXPECT_TRUE(std::isinf(foyadaEdgeGrade({ Low, Low }, { High, High }, flat)));
        EXPECT_TRUE(std::isinf(foyadaEdgeGrade({ 0, 0 }, { 1, 0 }, {})));
        const TerrainVertexHeightFn missing = [](auto, auto) { return std::optional<float>{}; };
        EXPECT_TRUE(std::isinf(foyadaEdgeGrade({ 0, 0 }, { 1, 0 }, missing)));
        for (const auto bad : { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
        {
            const TerrainVertexHeightFn invalid = [bad](auto, auto) { return bad; };
            EXPECT_TRUE(std::isinf(foyadaEdgeGrade({ 0, 0 }, { 1, 0 }, invalid)));
        }
        const TerrainVertexHeightFn missingFace
            = [](auto, auto y) -> std::optional<float> { return y == 2 ? std::optional<float>(0.f) : std::nullopt; };
        EXPECT_TRUE(std::isinf(foyadaEdgeGrade({ 0, 0 }, { 1, 0 }, missingFace)));
    }

    TEST(Foyadas, LargeCoordinatesRetainNativeVertexPrecision)
    {
        for (const RoadTile a : { RoadTile{ Low, Low }, RoadTile{ High - 1, High - 1 } })
        {
            const auto origin = std::int64_t(a.mX) * 4;
            const TerrainVertexHeightFn localPlane = [origin](auto x, auto) { return float(x - origin) * VertexSize; };
            const RoadTile b{ a.mX + 1, a.mY + 1 };
            EXPECT_FLOAT_EQ(foyadaEdgeGrade(a, b, localPlane), 1.f);
            EXPECT_EQ(foyadaEdgeGrade(a, b, localPlane), foyadaEdgeGrade(b, a, localPlane));
        }
    }

    TEST(Foyadas, OnlyFlowConnectedRockIsCollected)
    {
        const auto allow = [](const auto&, const auto&) { return true; };
        std::vector<FoyadaTile> candidates{ { { 0, 0 }, FoyadaMaterial::Rock }, { { 1, 1 }, FoyadaMaterial::Rock },
            { { 0, 0 }, FoyadaMaterial::Flow }, { { 0, 0 }, FoyadaMaterial::None }, { { 1, 1 }, FoyadaMaterial::Rock },
            { { 8, 8 }, FoyadaMaterial::Rock }, { { 2, 2 }, FoyadaMaterial::None } };
        const std::vector<RoadTile> expected{ { 0, 0 }, { 1, 1 } };
        EXPECT_EQ(collectFoyadaTiles(candidates, allow), expected);
        std::reverse(candidates.begin(), candidates.end());
        EXPECT_EQ(collectFoyadaTiles(candidates, allow), expected);
        const std::vector<RoadTile> seed{ { 0, 0 } };
        EXPECT_EQ(collectFoyadaTiles(candidates, {}), seed);
        EXPECT_EQ(collectFoyadaTiles(candidates, [](const auto&, const auto&) { return false; }), seed);
        EXPECT_TRUE(collectFoyadaTiles({ { { 0, 0 }, FoyadaMaterial::Rock } }, allow).empty());
        EXPECT_TRUE(collectFoyadaTiles({}, allow).empty());
    }

    TEST(Foyadas, FloodFillDoesNotWrapAtCoordinateLimits)
    {
        const std::vector<FoyadaTile> candidates{ { { High, High }, FoyadaMaterial::Flow },
            { { High - 1, High - 1 }, FoyadaMaterial::Rock }, { { Low, Low }, FoyadaMaterial::Rock },
            { { Low, High }, FoyadaMaterial::Rock }, { { High, Low }, FoyadaMaterial::Rock } };
        const std::vector<RoadTile> expected{ { High - 1, High - 1 }, { High, High } };
        EXPECT_EQ(collectFoyadaTiles(candidates, [](const auto&, const auto&) { return true; }), expected);
    }
}
