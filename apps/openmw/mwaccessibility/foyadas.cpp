#include "foyadas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace MWAccessibility
{
    namespace
    {
        constexpr double VertexSize = kRoadTileSize / 4.0;
        constexpr float Infinity = std::numeric_limits<float>::infinity();
        using Key = std::pair<std::int32_t, std::int32_t>;

        // Doubled native coordinates preserve exact half-steps even at int32 tile
        // extremes. Division must floor rather than truncate at negative positions.
        std::int64_t floorHalf(std::int64_t value)
        {
            return value / 2 - (value % 2 < 0 ? 1 : 0);
        }

        double surfaceGrade(std::int64_t x2, std::int64_t y2, const TerrainVertexHeightFn& height)
        {
            const auto x = floorHalf(x2);
            const auto y = floorHalf(y2);
            double result = Infinity;
            for (auto qx = x - (x2 % 2 == 0 ? 1 : 0); qx <= x; ++qx)
            {
                for (auto qy = y - (y2 % 2 == 0 ? 1 : 0); qy <= y; ++qy)
                {
                    const auto z0 = height(qx, qy);
                    const auto z1 = height(qx + 1, qy);
                    const auto z2 = height(qx + 1, qy + 1);
                    const auto z3 = height(qx, qy + 1);
                    if (!z0 || !z1 || !z2 || !z3 || !std::isfinite(*z0) || !std::isfinite(*z1) || !std::isfinite(*z2)
                        || !std::isfinite(*z3))
                        return Infinity;
                    const auto side2 = (x2 - 2 * qx) + (y2 - 2 * qy);
                    // Storage::getHeightAt: v0-v1-v3 / v1-v2-v3.
                    // On shared edges either incident triangle can support walking.
                    if (side2 <= 2)
                        result = std::min(result, std::hypot(double(*z1) - *z0, double(*z3) - *z0) / VertexSize);
                    if (side2 >= 2)
                        result = std::min(result, std::hypot(double(*z2) - *z3, double(*z2) - *z1) / VertexSize);
                }
            }
            return result;
        }
    }

    FoyadaMaterial classifyFoyadaTexture(std::string_view filename)
    {
        const auto slash = filename.find_last_of("/\\");
        if (slash != std::string_view::npos)
            filename.remove_prefix(slash + 1);
        std::string name(filename);
        for (char& c : name)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        const auto dot = name.find_last_of('.');
        if (dot != std::string::npos)
        {
            const auto extension = std::string_view(name).substr(dot);
            if (extension != ".tga" && extension != ".dds")
                return FoyadaMaterial::None;
            name.resize(dot);
        }
        if (name == "tx_ma_lavaflow")
            return FoyadaMaterial::Flow;
        if (name == "tx_ma_rock04")
            return FoyadaMaterial::Rock;
        return FoyadaMaterial::None;
    }

    float foyadaEdgeGrade(const RoadTile& from, const RoadTile& to, const TerrainVertexHeightFn& height)
    {
        const auto dx = std::int64_t(to.mX) - from.mX;
        const auto dy = std::int64_t(to.mY) - from.mY;
        if (!height || dx < -1 || dx > 1 || dy < -1 || dy > 1 || (dx == 0 && dy == 0))
            return Infinity;
        const auto x = std::int64_t(from.mX) * 4 + 2;
        const auto y = std::int64_t(from.mY) * 4 + 2;
        const double distance = VertexSize * std::hypot(double(dx), double(dy));
        double result = 0;
        std::optional<float> previous;
        for (int i = 0; i <= 4; ++i)
        {
            const auto z = height(x + i * dx, y + i * dy);
            if (!z || !std::isfinite(*z))
                return Infinity;
            if (previous)
                result = std::max(result, std::abs(double(*z) - *previous) / distance);
            previous = z;
            if (i < 4)
                result = std::max(result, surfaceGrade(2 * x + (2 * i + 1) * dx, 2 * y + (2 * i + 1) * dy, height));
        }
        return static_cast<float>(result);
    }

    std::vector<RoadTile> collectFoyadaTiles(const std::vector<FoyadaTile>& candidates,
        const std::function<bool(const RoadTile&, const RoadTile&)>& canConnect)
    {
        std::map<Key, FoyadaMaterial> materials;
        for (const auto& candidate : candidates)
        {
            if (candidate.mMaterial != FoyadaMaterial::Flow && candidate.mMaterial != FoyadaMaterial::Rock)
                continue;
            auto [it, inserted] = materials.emplace(Key{ candidate.mTile.mX, candidate.mTile.mY }, candidate.mMaterial);
            if (!inserted && candidate.mMaterial == FoyadaMaterial::Flow)
                it->second = FoyadaMaterial::Flow;
        }
        std::set<Key> reached;
        std::vector<RoadTile> queue;
        for (const auto& [key, material] : materials)
            if (material == FoyadaMaterial::Flow)
            {
                reached.insert(key);
                queue.push_back({ key.first, key.second });
            }
        for (std::size_t i = 0; canConnect && i < queue.size(); ++i)
        {
            const RoadTile at = queue[i];
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                {
                    if (dx == 0 && dy == 0)
                        continue;
                    const auto x = std::int64_t(at.mX) + dx;
                    const auto y = std::int64_t(at.mY) + dy;
                    if (x < std::numeric_limits<std::int32_t>::min() || x > std::numeric_limits<std::int32_t>::max()
                        || y < std::numeric_limits<std::int32_t>::min() || y > std::numeric_limits<std::int32_t>::max())
                        continue;
                    const Key key{ static_cast<std::int32_t>(x), static_cast<std::int32_t>(y) };
                    if (materials.find(key) == materials.end() || reached.count(key))
                        continue;
                    const RoadTile next{ key.first, key.second };
                    if (canConnect(at, next))
                    {
                        reached.insert(key);
                        queue.push_back(next);
                    }
                }
        }
        std::vector<RoadTile> result;
        result.reserve(reached.size());
        for (const auto& key : reached)
            result.push_back({ key.first, key.second });
        return result;
    }
}
