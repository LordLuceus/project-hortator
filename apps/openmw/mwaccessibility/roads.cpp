#include "roads.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <map>
#include <set>

#include <components/esm/refid.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/loadltex.hpp>
#include <components/misc/constants.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/ptr.hpp"

#include "spokenformat.hpp"

namespace MWAccessibility
{
    // Keep the pure module's tile size honest against the engine's own numbers. If
    // either ever changes, this fails to compile rather than silently mis-placing
    // every road in the world by a fraction of a cell.
    static_assert(kRoadTileSize
            == static_cast<float>(Constants::CellSizeInUnits) / ESM::LandRecordData::sLandTextureSize,
        "road tile size must stay in step with the cell size and land texture grid");

    namespace
    {
        // Case-insensitive ASCII substring test. The land texture names are plain
        // ASCII identifiers from the ESM, so a locale-aware fold would be overkill
        // (and would drag a dependency into an otherwise pure module).
        bool containsNoCase(std::string_view haystack, std::string_view needle)
        {
            if (needle.empty() || needle.size() > haystack.size())
                return false;
            const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
            for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
            {
                std::size_t j = 0;
                for (; j < needle.size(); ++j)
                {
                    if (lower(static_cast<unsigned char>(haystack[i + j])) != lower(static_cast<unsigned char>(needle[j])))
                        break;
                }
                if (j == needle.size())
                    return true;
            }
            return false;
        }

        // Squared distance between two tiles, in tile units. Squared so the
        // comparisons stay in integers -- these are used only for ordering.
        std::int64_t tileDist2(const RoadTile& a, const RoadTile& b)
        {
            const std::int64_t dx = static_cast<std::int64_t>(a.mX) - b.mX;
            const std::int64_t dy = static_cast<std::int64_t>(a.mY) - b.mY;
            return dx * dx + dy * dy;
        }

        // Ordering for the tile set / map keys.
        struct TileLess
        {
            bool operator()(const RoadTile& a, const RoadTile& b) const
            {
                if (a.mX != b.mX)
                    return a.mX < b.mX;
                return a.mY < b.mY;
            }
        };
    }

    bool isRoadTexture(std::string_view textureName)
    {
        // Daedric ruin flagstones (Tx_Daed_road_01 / Tx_Daed_road_02, and the
        // "Daedric_scrubruins" / "Daedric Stone" ids that use them) are named
        // "road" but are decoration inside ruin courtyards -- they lead nowhere and
        // listing them as a road to follow would be a confident wrong answer.
        // Checked FIRST so the generic "road" match below can't claim them.
        if (containsNoCase(textureName, "daed"))
            return false;

        // "road" catches Road Dirt, AL_road_01, WG_road, *_dirtroad_*, *_mainroad_*
        // -- every regional road set in Morrowind.esm. "cobble" catches the paved
        // town approaches (AI_Grass_Cobbles, WG_cobblestones). "path" is included
        // for mods that name their trails that way; nothing in vanilla uses it.
        return containsNoCase(textureName, "road") || containsNoCase(textureName, "cobble")
            || containsNoCase(textureName, "path");
    }

    std::vector<RoadStretch> groupRoadTiles(const std::vector<RoadTile>& tiles, const RoadTile& queryTile)
    {
        std::vector<RoadStretch> out;
        if (tiles.empty())
            return out;

        std::set<RoadTile, TileLess> remaining(tiles.begin(), tiles.end());

        while (!remaining.empty())
        {
            // Flood-fill one connected component, 8-neighbour. A diagonal road is a
            // corner-touching staircase of tiles, so excluding diagonals would
            // shatter it into single-tile fragments.
            const RoadTile seed = *remaining.begin();
            remaining.erase(remaining.begin());

            RoadStretch stretch;
            std::deque<RoadTile> queue{ seed };
            while (!queue.empty())
            {
                const RoadTile cur = queue.front();
                queue.pop_front();
                stretch.mTiles.push_back(cur);

                for (std::int32_t dx = -1; dx <= 1; ++dx)
                {
                    for (std::int32_t dy = -1; dy <= 1; ++dy)
                    {
                        if (dx == 0 && dy == 0)
                            continue;
                        const RoadTile nb{ cur.mX + dx, cur.mY + dy };
                        const auto it = remaining.find(nb);
                        if (it != remaining.end())
                        {
                            remaining.erase(it);
                            queue.push_back(nb);
                        }
                    }
                }
            }

            // Order this stretch's tiles by distance from the query point, so the
            // first is the way onto the road and callers need not re-sort.
            std::sort(stretch.mTiles.begin(), stretch.mTiles.end(),
                [&queryTile](const RoadTile& a, const RoadTile& b) {
                    const std::int64_t da = tileDist2(a, queryTile);
                    const std::int64_t db = tileDist2(b, queryTile);
                    if (da != db)
                        return da < db;
                    // Deterministic tie-break: two tiles equidistant from the player
                    // must always order the same way, or the spoken list could
                    // reshuffle between identical scans.
                    return TileLess{}(a, b);
                });

            stretch.mNearest = stretch.mTiles.front();
            stretch.mDirection = fitRoadDirection(stretch.mTiles, stretch.mNearest);
            stretch.mHasDirection = stretch.mDirection.length2() > 0.f;
            out.push_back(std::move(stretch));
        }

        // Nearest stretch first, matching every other category's ordering.
        std::sort(out.begin(), out.end(), [&queryTile](const RoadStretch& a, const RoadStretch& b) {
            const std::int64_t da = tileDist2(a.mNearest, queryTile);
            const std::int64_t db = tileDist2(b.mNearest, queryTile);
            if (da != db)
                return da < db;
            return TileLess{}(a.mNearest, b.mNearest);
        });

        return out;
    }

    osg::Vec2f fitRoadDirection(const std::vector<RoadTile>& tiles, const RoadTile& at)
    {
        // Look only at the tiles NEAR the sample point. A long road bends, so
        // fitting an axis through the whole stretch would average a curve into
        // nonsense; the local run is what the player is standing on.
        constexpr std::int64_t kLocalRadius2 = 3 * 3;

        std::vector<RoadTile> local;
        for (const RoadTile& t : tiles)
        {
            if (tileDist2(t, at) <= kLocalRadius2)
                local.push_back(t);
        }

        // One or two tiles cannot establish an axis worth speaking.
        if (local.size() < 3)
            return osg::Vec2f(0.f, 0.f);

        // Principal axis of the local tiles, via the 2x2 covariance matrix. This is
        // a proper line fit rather than "first tile to last tile": it is immune to
        // which tile happened to be enumerated first, and it degrades gracefully on
        // a ragged road edge where the extremes are noise.
        double meanX = 0.0;
        double meanY = 0.0;
        for (const RoadTile& t : local)
        {
            meanX += t.mX;
            meanY += t.mY;
        }
        meanX /= static_cast<double>(local.size());
        meanY /= static_cast<double>(local.size());

        double sxx = 0.0;
        double syy = 0.0;
        double sxy = 0.0;
        for (const RoadTile& t : local)
        {
            const double dx = t.mX - meanX;
            const double dy = t.mY - meanY;
            sxx += dx * dx;
            syy += dy * dy;
            sxy += dx * dy;
        }

        // Largest-eigenvalue eigenvector of [[sxx, sxy], [sxy, syy]].
        const double trace = sxx + syy;
        const double det = sxx * syy - sxy * sxy;
        const double disc = std::max(0.0, trace * trace / 4.0 - det);
        const double eigen = trace / 2.0 + std::sqrt(disc);

        double vx = 0.0;
        double vy = 0.0;
        if (std::abs(sxy) > 1e-9)
        {
            vx = eigen - syy;
            vy = sxy;
        }
        else
        {
            // Axis-aligned: no cross-correlation to read the direction from, so pick
            // whichever axis actually carries the spread.
            if (sxx >= syy)
            {
                vx = 1.0;
                vy = 0.0;
            }
            else
            {
                vx = 0.0;
                vy = 1.0;
            }
        }

        const double len = std::sqrt(vx * vx + vy * vy);
        // A blob of tiles with no dominant axis (a junction, a widened plaza) has
        // near-equal eigenvalues; reporting a direction there would be invented
        // precision, so say nothing instead.
        if (len < 1e-9 || trace <= 1e-9)
            return osg::Vec2f(0.f, 0.f);
        const double ratio = eigen / trace;
        if (ratio < 0.60)
            return osg::Vec2f(0.f, 0.f);

        return osg::Vec2f(static_cast<float>(vx / len), static_cast<float>(vy / len));
    }

    std::string describeRoadAxis(const osg::Vec2f& direction)
    {
        if (direction.length2() <= 0.f)
            return {};

        // World bearing convention used throughout the mod: 0 = +Y = north, and
        // yaw increases clockwise (east = +90 degrees), which is what compassLabel
        // expects. atan2(x, y) -- not the usual (y, x) -- gives exactly that.
        const float yaw = std::atan2(direction.x(), direction.y());
        const char* a = compassLabel(yaw);
        const char* b = compassLabel(yaw + kPi);
        if (a == b)
            return {};
        return std::string(a) + " to " + b;
    }

    osg::Vec2f roadTileCentre(const RoadTile& tile)
    {
        return osg::Vec2f((static_cast<float>(tile.mX) + 0.5f) * kRoadTileSize,
            (static_cast<float>(tile.mY) + 0.5f) * kRoadTileSize);
    }

    RoadTile roadTileAt(const osg::Vec2f& worldPos)
    {
        return RoadTile{ static_cast<std::int32_t>(std::floor(worldPos.x() / kRoadTileSize)),
            static_cast<std::int32_t>(std::floor(worldPos.y() / kRoadTileSize)) };
    }

    std::vector<RoadStretch> collectNearbyRoads(const MWWorld::Ptr& player)
    {
        if (player.isEmpty())
            return {};
        const MWWorld::CellStore* cell = player.getCell();
        if (!cell || !cell->getCell()->isExterior())
            return {};

        // Land texture data only exists for the main Morrowind exterior; an ESM4
        // worldspace has a different cell size and no ESM::Land record, so bail
        // rather than sampling nonsense.
        const ESM::RefId worldspace = cell->getCell()->getWorldSpace();
        if (worldspace != ESM::Cell::sDefaultWorldspaceId)
            return {};

        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        const MWWorld::Store<ESM::Land>& landStore = store.get<ESM::Land>();
        const MWWorld::Store<ESM::LandTexture>& textureStore = store.get<ESM::LandTexture>();

        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        const RoadTile playerTile = roadTileAt(osg::Vec2f(playerPos.x(), playerPos.y()));

        // Search a square of cells centred on the player. One cell either side
        // covers a 3x3 block (~350 metres across), which comfortably exceeds the
        // loaded-cell grid the rest of the scanner works within, so a road the
        // player could plausibly walk to is found without scanning the province.
        constexpr int kCellRadius = 1;
        const int playerCellX = static_cast<int>(std::floor(playerPos.x() / Constants::CellSizeInUnits));
        const int playerCellY = static_cast<int>(std::floor(playerPos.y() / Constants::CellSizeInUnits));

        // One texture index resolves to one name; cache the verdict so a cell of
        // 256 tiles costs a handful of string comparisons rather than 256.
        std::map<std::pair<std::uint16_t, int>, bool> roadVerdict;

        std::vector<RoadTile> tiles;
        for (int cx = playerCellX - kCellRadius; cx <= playerCellX + kCellRadius; ++cx)
        {
            for (int cy = playerCellY - kCellRadius; cy <= playerCellY + kCellRadius; ++cy)
            {
                const ESM::Land* land = landStore.search(cx, cy);
                if (!land)
                    continue;
                const ESM::Land::LandData* data = land->getLandData(ESM::Land::DATA_VTEX);
                if (!data)
                    continue;

                const int plugin = land->getPlugin();
                for (unsigned ty = 0; ty < ESM::Land::LAND_TEXTURE_SIZE; ++ty)
                {
                    for (unsigned tx = 0; tx < ESM::Land::LAND_TEXTURE_SIZE; ++tx)
                    {
                        // mTextures is row-major by the time we see it: ESM::Land's
                        // loader has already undone the file's 4x4-of-4x4 swizzle.
                        const std::uint16_t vtex = data->mTextures[ty * ESM::Land::LAND_TEXTURE_SIZE + tx];
                        // 0 means "the default base texture", which is never a road
                        // and must not be fed to the store (the index is 1-based).
                        if (vtex == 0)
                            continue;

                        const auto key = std::make_pair(vtex, plugin);
                        auto cached = roadVerdict.find(key);
                        if (cached == roadVerdict.end())
                        {
                            // The vtex index is per-plugin, so resolve it against the
                            // plugin that supplied this land record -- that is what
                            // makes a mod's own road textures work, and what stops a
                            // load-order shift from turning roads into sand.
                            const std::string* name = textureStore.search(vtex - 1, plugin);
                            cached = roadVerdict.emplace(key, name && isRoadTexture(*name)).first;
                        }
                        if (!cached->second)
                            continue;

                        tiles.push_back(RoadTile{ cx * static_cast<int>(ESM::Land::LAND_TEXTURE_SIZE)
                                + static_cast<int>(tx),
                            cy * static_cast<int>(ESM::Land::LAND_TEXTURE_SIZE) + static_cast<int>(ty) });
                    }
                }
            }
        }

        return groupRoadTiles(tiles, playerTile);
    }
}
