#include "roadroutes.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <queue>

namespace MWAccessibility
{
    namespace
    {
        std::string folded(std::string_view value)
        {
            std::string result(value);
            for (char& c : result)
                if (c >= 'A' && c <= 'Z')
                    c += 'a' - 'A';
            return result;
        }
    }

    std::string roadPlaceName(std::string_view name)
    {
        name = name.substr(0, name.find(','));
        constexpr std::string_view whitespace = " \t\r\n\f\v";
        const auto first = name.find_first_not_of(whitespace);
        if (first == std::string_view::npos)
            return {};
        return std::string(name.substr(first, name.find_last_not_of(whitespace) - first + 1));
    }

    bool roadCheckpointReached(const RoadTile& tile, const osg::Vec2f& position)
    {
        return std::isfinite(position.x()) && std::isfinite(position.y())
            && (position - roadTileCentre(tile)).length() <= kRoadTileSize * 0.75f;
    }

    std::optional<float> roadDestinationBearing(const RoadRoute& route, const osg::Vec2f& playerPosition)
    {
        if (route.mTiles.empty() || !std::isfinite(playerPosition.x()) || !std::isfinite(playerPosition.y()))
            return std::nullopt;
        const osg::Vec2f delta = roadTileCentre(route.mTiles.back()) - playerPosition;
        if (delta.x() == 0.f && delta.y() == 0.f)
            return std::nullopt;
        return std::atan2(delta.x(), delta.y());
    }

    RoadRoutePlanner::RoadRoutePlanner(
        std::vector<RoadTile> tiles, std::vector<RoadDestination> destinations, const RoadNetworkRules& rules)
        : mTiles(std::move(tiles))
    {
        const auto key = [](const RoadTile& tile) { return TileKey(tile.mX, tile.mY); };
        std::sort(mTiles.begin(), mTiles.end(), [&](const auto& a, const auto& b) { return key(a) < key(b); });
        mTiles.erase(std::unique(mTiles.begin(), mTiles.end()), mTiles.end());
        mIndex.reserve(mTiles.size());
        for (std::size_t i = 0; i < mTiles.size(); ++i)
            mIndex.emplace(key(mTiles[i]), i);
        mFoyada.resize(mTiles.size(), false);
        for (const RoadTile& tile : rules.mFoyadaTiles)
            if (const auto found = mIndex.find(key(tile)); found != mIndex.end())
                mFoyada[found->second] = true;
        mEdges.resize(mTiles.size());
        for (std::size_t i = 0; i < mTiles.size(); ++i)
        {
            mEdges[i].reserve(8);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                {
                    if (dx == 0 && dy == 0)
                        continue;
                    // All int32 tile coordinates are valid; do not overflow or wrap at their limits.
                    const std::int64_t x = std::int64_t(mTiles[i].mX) + dx;
                    const std::int64_t y = std::int64_t(mTiles[i].mY) + dy;
                    if (x < std::numeric_limits<std::int32_t>::min() || x > std::numeric_limits<std::int32_t>::max()
                        || y < std::numeric_limits<std::int32_t>::min() || y > std::numeric_limits<std::int32_t>::max())
                        continue;
                    const auto found = mIndex.find({ static_cast<std::int32_t>(x), static_cast<std::int32_t>(y) });
                    if (found != mIndex.end()
                        && (!rules.mCanConnect || rules.mCanConnect(mTiles[i], mTiles[found->second])))
                        mEdges[i].emplace_back(found->second, kRoadTileSize * (dx && dy ? std::sqrt(2.0) : 1.0));
                }
        }
        std::map<std::string, Destination> grouped;
        for (const auto& destination : destinations)
        {
            const std::string name = roadPlaceName(destination.mName);
            if (name.empty())
                continue;
            const std::string nameKey = folded(name);
            auto& group = grouped[nameKey];
            // A deterministic original spelling even if input contains case variants.
            if (group.mName.empty() || name < group.mName)
                group.mName = name;
            group.mKey = nameKey;
            for (const auto& tile : destination.mTiles)
            {
                const auto found = mIndex.find(key(tile));
                if (found != mIndex.end())
                    group.mAnchors.push_back(found->second);
            }
        }
        for (auto& [name, destination] : grouped)
        {
            auto& anchors = destination.mAnchors;
            std::sort(anchors.begin(), anchors.end());
            anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());
            if (!anchors.empty())
                mDestinations.push_back(std::move(destination));
        }
    }

    std::vector<RoadTile> RoadRoutePlanner::nearbyEntrances(const osg::Vec2f& playerPosition) const
    {
        if (!std::isfinite(playerPosition.x()) || !std::isfinite(playerPosition.y()))
            return {};
        const double tx = std::floor(double(playerPosition.x()) / kRoadTileSize);
        const double ty = std::floor(double(playerPosition.y()) / kRoadTileSize);
        if (tx < std::numeric_limits<std::int32_t>::min() || tx > std::numeric_limits<std::int32_t>::max()
            || ty < std::numeric_limits<std::int32_t>::min() || ty > std::numeric_limits<std::int32_t>::max())
            return {};
        const RoadTile query{ static_cast<std::int32_t>(tx), static_cast<std::int32_t>(ty) };
        const auto cell = [](std::int32_t value) { return static_cast<std::int64_t>(std::floor(double(value) / 16)); };
        std::vector<bool> remaining(mTiles.size(), false);
        for (std::size_t i = 0; i < mTiles.size(); ++i)
            remaining[i] = std::abs(cell(mTiles[i].mX) - cell(query.mX)) <= 1
                && std::abs(cell(mTiles[i].mY) - cell(query.mY)) <= 1;
        const auto distance = [&query](const RoadTile& tile) {
            const double dx = double(tile.mX) - query.mX, dy = double(tile.mY) - query.mY;
            return dx * dx + dy * dy;
        };
        std::vector<RoadTile> result;
        for (std::size_t seed = 0; seed < mTiles.size(); ++seed)
        {
            if (!remaining[seed])
                continue;
            remaining[seed] = false;
            std::deque<std::size_t> queue{ seed };
            std::size_t nearest = seed;
            while (!queue.empty())
            {
                const auto at = queue.front();
                queue.pop_front();
                if (distance(mTiles[at]) < distance(mTiles[nearest])
                    || (distance(mTiles[at]) == distance(mTiles[nearest]) && at < nearest))
                    nearest = at;
                for (const auto& [next, weight] : mEdges[at])
                {
                    if (!remaining[next])
                        continue;
                    remaining[next] = false;
                    queue.push_back(next);
                }
            }
            result.push_back(mTiles[nearest]);
        }
        return result;
    }

    std::vector<RoadRoute> RoadRoutePlanner::routes(
        const std::vector<RoadTile>& starts, const osg::Vec2f& playerPosition, std::string_view origin) const
    {
        if (!std::isfinite(playerPosition.x()) || !std::isfinite(playerPosition.y()))
            return {};
        const auto none = mTiles.size();
        std::vector<double> distance(none, std::numeric_limits<double>::infinity());
        std::vector<double> approach(none, 0);
        std::vector<std::size_t> previous(none, none);
        using Entry = std::pair<double, std::size_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
        for (const auto& tile : starts)
        {
            const auto found = mIndex.find({ tile.mX, tile.mY });
            if (found == mIndex.end())
                continue;
            const auto centre = roadTileCentre(tile);
            const double cost
                = std::hypot(double(centre.x()) - playerPosition.x(), double(centre.y()) - playerPosition.y());
            const auto i = found->second;
            if (cost < distance[i])
            {
                distance[i] = approach[i] = cost;
                queue.emplace(cost, i);
            }
        }
        while (!queue.empty())
        {
            const auto [cost, i] = queue.top();
            queue.pop();
            if (cost != distance[i])
                continue;
            for (const auto& [next, weight] : mEdges[i])
                if (cost + weight < distance[next])
                {
                    distance[next] = cost + weight;
                    approach[next] = approach[i];
                    previous[next] = i;
                    queue.emplace(distance[next], next);
                }
            // Equal costs retain the first predecessor: sorted node IDs and
            // adjacency make this independent of caller input order.
        }

        const std::string originKey = folded(roadPlaceName(origin));
        std::vector<RoadRoute> result;
        for (const auto& destination : mDestinations)
        {
            if (destination.mKey == originKey)
                continue;
            std::size_t best = none;
            for (const auto anchor : destination.mAnchors)
                if (std::isfinite(distance[anchor]) && (best == none || distance[anchor] < distance[best]))
                    best = anchor;
            if (best == none || previous[best] == none)
                continue;
            RoadRoute route;
            route.mDestination = destination.mName;
            route.mApproachLength = static_cast<float>(approach[best]);
            double length = 0;
            for (auto i = best; i != none; i = previous[i])
            {
                route.mTiles.push_back(mTiles[i]);
                route.mUsesFoyada = route.mUsesFoyada || mFoyada[i];
                if (previous[i] != none)
                {
                    const auto& a = mTiles[i];
                    const auto& b = mTiles[previous[i]];
                    length += kRoadTileSize * (a.mX != b.mX && a.mY != b.mY ? std::sqrt(2.0) : 1.0);
                }
            }
            route.mLength = static_cast<float>(length);
            std::reverse(route.mTiles.begin(), route.mTiles.end());
            result.push_back(std::move(route));
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            const double aTotal = double(a.mLength) + a.mApproachLength;
            const double bTotal = double(b.mLength) + b.mApproachLength;
            if (aTotal != bTotal)
                return aTotal < bTotal;
            return folded(a.mDestination) < folded(b.mDestination);
        });
        return result;
    }
}
