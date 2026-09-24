#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

#include <apps/openmw/mwaccessibility/roadnetwork.hpp>
#include <apps/openmw/mwaccessibility/roadroutes.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/loadltex.hpp>
#include <components/loadinglistener/loadinglistener.hpp>
#include <components/testing/util.hpp>

namespace
{
    class MWAccessibilityRoadNetworkSynthetic : public ::testing::Test
    {
    protected:
        MWWorld::ESMStore mStore;
        std::vector<std::filesystem::path> mFiles;

        void load(bool channelHeights)
        {
            Loading::Listener listener;
            ESM::Dialogue* dialogue = nullptr;
            for (int plugin = 0; plugin < 2; ++plugin)
            {
                const auto path = TestingOpenMW::outputFilePath("road-network-" + std::to_string(plugin) + ".esm");
                mFiles.push_back(path);
                {
                    std::ofstream file(path, std::ios::binary);
                    ASSERT_TRUE(file.is_open());
                    ESM::ESMWriter writer;
                    writer.setRecordCount(plugin == 0 ? 5 : 3);
                    writer.save(file);
                    const auto save = [&writer](const auto& record) {
                        writer.startRecord(record.sRecordId);
                        record.save(writer, false);
                        writer.endRecord(record.sRecordId);
                    };
                    ESM::LandTexture texture;
                    // Same local palette index, different materials in each file.
                    texture.mIndex = 0;
                    texture.mId = ESM::RefId::stringRefId(plugin == 0 ? "test-road" : "test-flow");
                    texture.mTexture = plugin == 0 ? "tx_test_road.tga" : "tx_ma_lavaflow.tga";
                    save(texture);
                    for (int x = plugin; x < 3; x += 2)
                    {
                        ESM::Cell cell;
                        cell.mData.mX = x;
                        cell.mName = x == 0 ? "Origin" : x == 2 ? "Destination" : "";
                        save(cell);
                        ESM::Land land;
                        land.blank();
                        land.mFlags
                            = ESM::Land::Flag_HeightsNormals | ESM::Land::Flag_Textures | ESM::Land::Flag_Colors;
                        land.mX = x;
                        land.mLandData->mHeights.fill(0.f);
                        // The later record in plugin 0 deliberately differs:
                        // failing to seek back to it must change the graph.
                        for (int tile = 0; tile < (x == 2 ? 1 : 16); ++tile)
                            land.mLandData->mTextures[8 * 16 + tile] = 1;
                        if (plugin == 1 && !channelHeights)
                            land.mDataTypes &= ~ESM::Land::DATA_VHGT;
                        save(land);
                    }
                    writer.close();
                    ASSERT_TRUE(file.good());
                }
                ESM::ESMReader reader;
                reader.setIndex(plugin);
                reader.open(path);
                mStore.load(reader, &listener, dialogue);
            }
            mStore.setUp();
        }

        void TearDown() override
        {
            for (const auto& path : mFiles)
            {
                std::error_code error;
                std::filesystem::remove(path, error);
                EXPECT_FALSE(error) << error.message();
            }
        }
    };

    TEST_F(MWAccessibilityRoadNetworkSynthetic, RestoresRecordContextsAndPluginPalettes)
    {
        ASSERT_NO_FATAL_FAILURE(load(true));
        for (int x = 0; x < 3; ++x)
        {
            const auto* land = mStore.get<ESM::Land>().search(x, 0);
            ASSERT_NE(land, nullptr);
            EXPECT_EQ(land->getPlugin(), x == 1 ? 1 : 0);
            ESM::Land::LandData data;
            land->loadData(ESM::Land::DATA_VTEX | ESM::Land::DATA_VHGT, data);
            ASSERT_EQ(data.mDataLoaded & (ESM::Land::DATA_VTEX | ESM::Land::DATA_VHGT),
                ESM::Land::DATA_VTEX | ESM::Land::DATA_VHGT);
            EXPECT_EQ(data.mTextures[8 * 16], 1);
            EXPECT_EQ(data.mHeights[34 * 65 + 2], 0.f);
            const auto* texture = mStore.get<ESM::LandTexture>().search(0, land->getPlugin());
            ASSERT_NE(texture, nullptr);
            EXPECT_EQ(*texture, x == 1 ? "tx_ma_lavaflow.tga" : "tx_test_road.tga");
            const auto* cell = mStore.get<ESM::Cell>().search(x, 0);
            ASSERT_NE(cell, nullptr);
            EXPECT_EQ(cell->mName, x == 0 ? "Origin" : x == 2 ? "Destination" : "");
        }
        const auto planner = MWAccessibility::loadRoadRoutePlanner(mStore);
        const MWAccessibility::RoadTile start{ 0, 8 };
        const auto routes = planner->routes({ start }, MWAccessibility::roadTileCentre(start), "Origin");
        ASSERT_EQ(routes.size(), 1u);
        EXPECT_EQ(routes[0].mDestination, "Destination");
        EXPECT_TRUE(routes[0].mUsesFoyada);
        ASSERT_EQ(routes[0].mTiles.size(), 33u);
        for (std::size_t i = 0; i < routes[0].mTiles.size(); ++i)
            EXPECT_EQ(routes[0].mTiles[i], (MWAccessibility::RoadTile{ static_cast<std::int32_t>(i), 8 }));
        const MWAccessibility::RoadTile absent{ 47, 8 };
        EXPECT_TRUE(planner->routes({ absent }, MWAccessibility::roadTileCentre(absent), "").empty());
        // Snapshot construction must not populate shared mutable LAND caches.
        for (const ESM::Land& land : mStore.get<ESM::Land>())
            EXPECT_EQ(land.getLandData(), nullptr);
    }

    TEST_F(MWAccessibilityRoadNetworkSynthetic, MissingHeightsDoNotReusePreviousTerrain)
    {
        ASSERT_NO_FATAL_FAILURE(load(false));
        const auto planner = MWAccessibility::loadRoadRoutePlanner(mStore);
        const MWAccessibility::RoadTile start{ 0, 8 };
        EXPECT_TRUE(planner->routes({ start }, MWAccessibility::roadTileCentre(start), "Origin").empty());
    }

    // Opt-in: a UTF-8 file with one absolute plugin path per line, in load order.
    // No save, window, player state, or game log is touched.
    TEST(MWAccessibilityRoadNetwork, RealContentConstruction)
    {
        const char* path = std::getenv("OPENMW_ROAD_CONTENT_FILES");
        if (!path)
            GTEST_SKIP() << "Set OPENMW_ROAD_CONTENT_FILES to an ordered plugin-path list";
        std::ifstream input(path);
        ASSERT_TRUE(input.is_open());
        MWWorld::ESMStore store;
        Loading::Listener listener;
        ESM::Dialogue* dialogue = nullptr;
        int index = 0;
        std::string filename;
        while (std::getline(input, filename))
        {
            if (!filename.empty() && filename.back() == '\r')
                filename.pop_back();
            if (filename.empty())
                continue;
            ESM::ESMReader reader;
            reader.setIndex(index++);
            reader.open(std::filesystem::u8path(filename));
            store.load(reader, &listener, dialogue);
        }
        ASSERT_GT(index, 0);
        store.setUp();
        const char* report = std::getenv("OPENMW_ROAD_NETWORK_REPORT");
        std::ofstream output;
        if (report)
        {
            output.open(report);
            ASSERT_TRUE(output.is_open());
            output << std::setprecision(9);
        }
        // Repeat to separate OS file-cache effects from construction itself.
        for (int repetition = 0; repetition < 3; ++repetition)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto planner = MWAccessibility::loadRoadRoutePlanner(store);
            const auto built = std::chrono::steady_clock::now();
            std::size_t count = 0;
            // Sample Vvardenfell and nearby cells, including roads and Mamaea.
            for (int y = -18; y <= 22; y += 4)
                for (int x = -20; x <= 18; x += 4)
                {
                    const auto position = MWAccessibility::roadTileCentre({ x * 16 + 8, y * 16 + 8 });
                    const auto entrances = planner->nearbyEntrances(position);
                    const auto routes = planner->routes(entrances, position, "");
                    count += routes.size();
                    if (report && repetition == 0)
                    {
                        output << "start " << x << ' ' << y << '\n';
                        for (const auto& route : routes)
                        {
                            output << std::quoted(route.mDestination) << ' ' << route.mUsesFoyada << ' '
                                   << route.mLength << ' ' << route.mApproachLength;
                            for (const auto& tile : route.mTiles)
                                output << ' ' << tile.mX << ',' << tile.mY;
                            output << '\n';
                        }
                    }
                }
            const auto done = std::chrono::steady_clock::now();
            EXPECT_GT(count, 0u);
            std::cout << "[a11y] road-benchmark repeat=" << repetition << " routes=" << count
                      << " build_ms=" << std::chrono::duration<double, std::milli>(built - start).count()
                      << " queries_ms=" << std::chrono::duration<double, std::milli>(done - built).count() << '\n';
        }
    }
}
