#include "captureframing.hpp"

#include <algorithm>
#include <cmath>

#include <osg/Vec3d>

namespace MWAccessibility::CaptureFraming
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr double Margin = 1.1;
        constexpr double MaxFov = 110.0;
        constexpr double MinFov = 1.0;
        constexpr double MinDepth = 1.0;

        template <class Vector>
        bool finite(const Vector& value)
        {
            return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
        }

        double tangent(double degrees)
        {
            return std::tan(degrees * Pi / 360.0);
        }

        float degrees(double halfTangent)
        {
            return static_cast<float>(std::atan(halfTangent) * 360.0 / Pi);
        }

        View preset(const osg::Vec3f& eye, double yaw, float aspect, double vertical, double horizontal, bool down)
        {
            if (!std::isfinite(yaw))
                yaw = 0;
            if (!std::isfinite(aspect) || aspect <= 0)
                aspect = 1;
            const double scale = down ? std::sqrt(0.5) : 1.0;
            const osg::Vec3f direction(static_cast<float>(std::sin(yaw) * scale),
                static_cast<float>(std::cos(yaw) * scale), down ? -static_cast<float>(scale) : 0.f);
            return { eye, eye + direction * 1000.f,
                degrees(std::min(tangent(vertical), tangent(horizontal) / aspect)) };
        }
    }

    std::optional<View> frameTarget(const osg::Vec3f& eye, const osg::BoundingBox& bounds, float aspect, bool closeUp)
    {
        if (!finite(eye) || !std::isfinite(aspect) || aspect <= 0 || !bounds.valid() || !finite(bounds._min)
            || !finite(bounds._max))
            return std::nullopt;
        for (int axis = 0; axis < 3; ++axis)
            if (bounds._min[axis] >= bounds._max[axis])
                return std::nullopt;
        if (bounds.contains(eye))
            return std::nullopt;

        // Work in double precision so finite float bounds cannot overflow during averaging.
        osg::Vec3d low(bounds._min);
        const osg::Vec3d high(bounds._max);
        if (closeUp)
            low.z() = (low.z() + high.z()) * 0.5;
        const osg::Vec3f centre((low + high) * 0.5);
        osg::Vec3d forward = osg::Vec3d(centre) - osg::Vec3d(eye);
        if (forward.normalize() <= MinDepth)
            return std::nullopt;
        osg::Vec3d right = forward ^ osg::Vec3d(0, 0, 1);
        // A world-Z look-at is unstable when aiming straight up or down.
        if (right.normalize() < 1e-4)
            return std::nullopt;
        const osg::Vec3d up = right ^ forward;
        double required = tangent(MinFov);
        for (unsigned int corner = 0; corner < 8; ++corner)
        {
            const osg::Vec3d point((corner & 1) ? high.x() : low.x(), (corner & 2) ? high.y() : low.y(),
                (corner & 4) ? high.z() : low.z());
            const osg::Vec3d delta = point - osg::Vec3d(eye);
            const double depth = delta * forward;
            if (depth <= MinDepth)
                return std::nullopt;
            required
                = std::max(required, Margin * std::max(std::abs(delta * up), std::abs(delta * right) / aspect) / depth);
        }
        if (!std::isfinite(required) || required > tangent(MaxFov) || required * aspect > tangent(MaxFov))
            return std::nullopt;
        return View{ eye, centre, degrees(required) };
    }

    View surroundings(const osg::Vec3f& eye, float yaw, float aspect, int quarterTurn)
    {
        const double angle = (std::isfinite(yaw) ? static_cast<double>(yaw) : 0.0) + (quarterTurn % 4) * Pi * 0.5;
        return preset(eye, angle, aspect, 70, 85, false);
    }

    View landscape(const osg::Vec3f& eye, float yaw, float aspect)
    {
        return preset(eye, yaw, aspect, 90, 110, false);
    }

    View obstruction(const osg::Vec3f& eye, float yaw, float aspect)
    {
        return preset(eye, yaw, aspect, 70, 100, true);
    }
}
