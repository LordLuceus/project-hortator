#include "roadnetwork.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/loadltex.hpp>
#include <components/misc/constants.hpp>

#include "../mwbase/environment.hpp"
#include "../mwworld/esmstore.hpp"
#include "foyadas.hpp"
#include "roadroutes.hpp"

namespace MWAccessibility
{
    std::unique_ptr<RoadRoutePlanner> loadRoadRoutePlanner()
    {
        const auto& store = *MWBase::Environment::get().getESMStore();
        const auto& textures = store.get<ESM::LandTexture>();
        const auto& cells = store.get<ESM::Cell>();
        struct Material
        {
            bool mRoad = false;
            FoyadaMaterial mFoyada = FoyadaMaterial::None;
        };
        std::map<std::pair<int, std::uint16_t>, Material> textureCache;
        std::map<std::string, std::vector<RoadTile>> places;
        std::vector<RoadTile> tiles;
        std::set<std::pair<std::int32_t, std::int32_t>> roadTiles;
        std::vector<FoyadaTile> candidates;
        std::map<std::pair<std::int32_t, std::int32_t>, std::string> tileNames;
        std::set<std::pair<int, int>> heightCells;

        for (const ESM::Land& land : store.get<ESM::Land>())
        {
            if (land.mX < std::numeric_limits<std::int32_t>::min() / 16
                || land.mX > std::numeric_limits<std::int32_t>::max() / 16
                || land.mY < std::numeric_limits<std::int32_t>::min() / 16
                || land.mY > std::numeric_limits<std::int32_t>::max() / 16)
                continue;
            // Read into a temporary buffer, not the record's persistent cache:
            // visiting the province must not retain thousands of height grids
            // or mutate terrain data being read by background rendering jobs.
            ESM::Land::LandData data;
            land.loadData(ESM::Land::DATA_VTEX, data);
            if (!(data.mDataLoaded & ESM::Land::DATA_VTEX))
                continue;
            const ESM::Cell* cell = cells.search(land.mX, land.mY);
            const std::string name = cell ? roadPlaceName(cell->mName) : std::string();
            const int plugin = land.getPlugin();
            for (int y = 0; y < 16; ++y)
            {
                for (int x = 0; x < 16; ++x)
                {
                    const std::uint16_t index = data.mTextures[y * 16 + x];
                    if (!index)
                        continue;
                    const auto key = std::make_pair(plugin, index);
                    auto found = textureCache.find(key);
                    if (found == textureCache.end())
                    {
                        const std::string* filename = textures.search(index - 1, plugin);
                        const auto foyada = filename ? classifyFoyadaTexture(*filename) : FoyadaMaterial::None;
                        // Exact material identity outranks generic road words in
                        // mod directory names (e.g. roads/tx_ma_lavaflow.dds).
                        const Material material{ foyada == FoyadaMaterial::None && filename && isRoadTexture(*filename),
                            foyada };
                        found = textureCache.emplace(key, material).first;
                    }
                    const Material& material = found->second;
                    if (!material.mRoad && material.mFoyada == FoyadaMaterial::None)
                        continue;
                    const RoadTile tile{ land.mX * 16 + x, land.mY * 16 + y };
                    if (material.mRoad)
                    {
                        tiles.push_back(tile);
                        roadTiles.emplace(tile.mX, tile.mY);
                    }
                    else
                    {
                        candidates.push_back({ tile, material.mFoyada });
                        // Include neighbouring height grids for links across a
                        // cell edge and joins to ordinary painted roads.
                        for (int dx = -1; dx <= 1; ++dx)
                            for (int dy = -1; dy <= 1; ++dy)
                                heightCells.emplace(land.mX + dx, land.mY + dy);
                    }
                    // Reaching this anchor means entering the named area's
                    // actual cell, not merely stopping somewhere near it.
                    if (!name.empty())
                        tileNames[{ tile.mX, tile.mY }] = name;
                }
            }
        }

        // Temporary native vertex grids: avoid both a persistent terrain cache
        // and the false assumption that a gentle change along an edge implies
        // a walkable surface (a level traverse of a cliff is still a cliff).
        using Heights = std::array<float, ESM::LandRecordData::sLandNumVerts>;
        std::map<std::pair<int, int>, Heights> heights;
        for (const auto& [x, y] : heightCells)
        {
            const ESM::Land* land = store.get<ESM::Land>().search(x, y);
            if (!land)
                continue;
            ESM::Land::LandData data;
            land->loadData(ESM::Land::DATA_VHGT, data);
            if (data.mDataLoaded & ESM::Land::DATA_VHGT)
                heights.emplace(std::make_pair(x, y), data.mHeights);
        }
        const TerrainVertexHeightFn height = [&heights](std::int64_t x, std::int64_t y) -> std::optional<float> {
            const auto cx = static_cast<int>(std::floor(double(x) / 64));
            const auto cy = static_cast<int>(std::floor(double(y) / 64));
            const auto found = heights.find({ cx, cy });
            if (found == heights.end())
                return std::nullopt;
            return found->second[(y - std::int64_t(cy) * 64) * 65 + x - std::int64_t(cx) * 64];
        };
        const float maxGrade = std::tan(Constants::sMaxSlope * float(std::acos(-1.0)) / 180.f);
        using EdgeKey = std::pair<std::pair<std::int32_t, std::int32_t>, std::pair<std::int32_t, std::int32_t>>;
        std::map<EdgeKey, bool> connections;
        const auto canConnect = [&](const RoadTile& a, const RoadTile& b) {
            auto first = std::make_pair(a.mX, a.mY), second = std::make_pair(b.mX, b.mY);
            if (roadTiles.count(first) && roadTiles.count(second))
                return true; // Preserve the tested painted-road network unchanged.
            if (second < first)
                std::swap(first, second);
            const EdgeKey key{ first, second };
            auto found = connections.find(key);
            if (found == connections.end())
                found = connections.emplace(key, foyadaEdgeGrade(a, b, height) < maxGrade).first;
            return found->second;
        };
        RoadNetworkRules rules;
        rules.mFoyadaTiles = collectFoyadaTiles(candidates, canConnect);
        rules.mCanConnect = canConnect;
        tiles.insert(tiles.end(), rules.mFoyadaTiles.begin(), rules.mFoyadaTiles.end());
        for (const RoadTile& tile : tiles)
            if (const auto found = tileNames.find({ tile.mX, tile.mY }); found != tileNames.end())
                places[found->second].push_back(tile);

        std::vector<RoadDestination> destinations;
        for (auto& [name, anchors] : places)
            destinations.push_back({ name, std::move(anchors) });
        return std::make_unique<RoadRoutePlanner>(std::move(tiles), std::move(destinations), rules);
    }
}
