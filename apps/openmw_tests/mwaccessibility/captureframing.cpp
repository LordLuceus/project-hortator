#include "apps/openmw/mwaccessibility/captureframing.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>
#include <osg/Vec3d>

namespace MWAccessibility::CaptureFraming
{
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;

        void expectFit(const View& view, const osg::BoundingBox& bounds, float aspect)
        {
            osg::Vec3d forward = osg::Vec3d(view.centre) - osg::Vec3d(view.eye);
            forward.normalize();
            osg::Vec3d right = forward ^ osg::Vec3d(0, 0, 1);
            right.normalize();
            const osg::Vec3d up = right ^ forward;
            const double halfHeight = std::tan(view.verticalFov * Pi / 360);
            for (unsigned int i = 0; i < 8; ++i)
            {
                const osg::Vec3d delta = osg::Vec3d(bounds.corner(i)) - osg::Vec3d(view.eye);
                const double depth = delta * forward;
                EXPECT_GT(depth, 1);
                EXPECT_LE(std::abs(delta * right) / (depth * halfHeight * aspect), 1.0 / 1.1 + 1e-5);
                EXPECT_LE(std::abs(delta * up) / (depth * halfHeight), 1.0 / 1.1 + 1e-5);
            }
        }

        double horizontalFov(const View& view, float aspect)
        {
            return 360 / Pi * std::atan(std::tan(view.verticalFov * Pi / 360) * aspect);
        }
    }

    TEST(MWAccessibilityCaptureFraming, AllCornersFitActorsWideAndTallObjects)
    {
        const osg::Vec3f eye(30, -600, 150);
        for (const auto& bounds : { osg::BoundingBox(-20, -20, 0, 20, 20, 180),
                 osg::BoundingBox(-180, -30, 0, 180, 30, 40), osg::BoundingBox(-15, -15, -100, 15, 15, 300) })
        {
            for (float aspect : { 0.5f, 1.f, 16.f / 9.f, 3.f })
            {
                const auto view = frameTarget(eye, bounds, aspect, false);
                ASSERT_TRUE(view);
                EXPECT_EQ(view->eye, eye);
                EXPECT_EQ(view->centre, bounds.center());
                expectFit(*view, bounds, aspect);
            }
        }
    }

    TEST(MWAccessibilityCaptureFraming, CloseUpFitsGenericUpperHalf)
    {
        const osg::Vec3f eye(0, -400, 140);
        const osg::BoundingBox bounds(-20, -20, 0, 20, 20, 180);
        const auto full = frameTarget(eye, bounds, 1, false);
        const auto close = frameTarget(eye, bounds, 1, true);
        ASSERT_TRUE(full);
        ASSERT_TRUE(close);
        EXPECT_EQ(close->eye, eye);
        EXPECT_FLOAT_EQ(close->centre.z(), 135);
        EXPECT_LT(close->verticalFov, full->verticalFov);
        expectFit(*close, osg::BoundingBox(-20, -20, 90, 20, 20, 180), 1);
    }

    TEST(MWAccessibilityCaptureFraming, RejectsInvalidAndUnframeableTargets)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const osg::BoundingBox bounds(-10, -10, -10, 10, 10, 10);
        const osg::Vec3f eye(0, -100, 0);
        for (float aspect : { 0.f, -1.f, nan, inf })
            EXPECT_FALSE(frameTarget(eye, bounds, aspect, false));
        for (bool closeUp : { false, true })
        {
            EXPECT_FALSE(frameTarget(osg::Vec3f(), bounds, 1, closeUp));
            EXPECT_FALSE(frameTarget(osg::Vec3f(0, -10, 0), bounds, 1, closeUp));
            EXPECT_FALSE(frameTarget(osg::Vec3f(0, -10.5f, 0), bounds, 1, closeUp));
            EXPECT_FALSE(frameTarget(osg::Vec3f(nan, 0, 0), bounds, 1, closeUp));
            EXPECT_FALSE(frameTarget(osg::Vec3f(0, inf, 0), bounds, 1, closeUp));
            EXPECT_FALSE(frameTarget(eye, osg::BoundingBox(), 1, closeUp));
            EXPECT_FALSE(frameTarget(eye, osg::BoundingBox(0, 0, 0, 1, 1, 0), 1, closeUp));
            EXPECT_FALSE(frameTarget(eye, osg::BoundingBox(-1, -1, -1, nan, 1, 1), 1, closeUp));
            EXPECT_FALSE(frameTarget(eye, osg::BoundingBox(-1, -1, -1, 1, inf, 1), 1, closeUp));
        }
        EXPECT_FALSE(frameTarget(osg::Vec3f(0, 0, 100), bounds, 1, false));
        // Eye is outside the box, but some corners are behind the newly aimed camera.
        EXPECT_FALSE(frameTarget(osg::Vec3f(11, 9, 0), bounds, 1, false));
        EXPECT_FALSE(frameTarget(osg::Vec3f(0, -12, 0), bounds, 1, false));
    }

    TEST(MWAccessibilityCaptureFraming, PortraitNeedsLargerVerticalFov)
    {
        const osg::Vec3f eye(0, -400, 20);
        const osg::BoundingBox bounds(-100, -10, 0, 100, 10, 40);
        const auto portrait = frameTarget(eye, bounds, 0.5f, false);
        const auto wide = frameTarget(eye, bounds, 2, false);
        ASSERT_TRUE(portrait);
        ASSERT_TRUE(wide);
        EXPECT_GT(portrait->verticalFov, wide->verticalFov);
    }

    TEST(MWAccessibilityCaptureFraming, ActorYawAndQuarterTurnsAreClockwise)
    {
        const osg::Vec3f eye(10, 20, 30);
        const osg::Vec3f expected[] = { { 0, 1, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { -1, 0, 0 } };
        for (int turn = -4; turn <= 7; ++turn)
        {
            const auto view = surroundings(eye, 0, 1, turn);
            EXPECT_EQ(view.eye, eye);
            EXPECT_LT(((view.centre - eye) / 1000.f - expected[(turn % 4 + 4) % 4]).length(), 1e-5);
            const auto yawView = surroundings(eye, static_cast<float>(turn * Pi / 2), 1);
            EXPECT_LT((view.centre - yawView.centre).length(), 0.001);
        }
    }

    TEST(MWAccessibilityCaptureFraming, PresetsPreserveEyeAndLimitAspectAwareFov)
    {
        const osg::Vec3f eye(10, 20, 150);
        for (float aspect : { 0.5f, 1.f, 16.f / 9.f, 4.f })
        {
            const auto level = surroundings(eye, 0, aspect);
            const auto wide = landscape(eye, 0, aspect);
            const auto down = obstruction(eye, 0, aspect);
            EXPECT_EQ(level.eye, eye);
            EXPECT_EQ(wide.eye, eye);
            EXPECT_EQ(down.eye, eye);
            EXPECT_FLOAT_EQ(level.centre.z(), eye.z());
            EXPECT_FLOAT_EQ(wide.centre.z(), eye.z());
            EXPECT_NEAR(down.centre.y() - eye.y(), eye.z() - down.centre.z(), 1e-4);
            EXPECT_GT(wide.verticalFov, level.verticalFov);
            EXPECT_LE(level.verticalFov, 70.00001);
            EXPECT_LE(horizontalFov(level, aspect), 85.00001);
            EXPECT_LE(horizontalFov(wide, aspect), 110.00001);
            EXPECT_LE(horizontalFov(down, aspect), 100.00001);
        }
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(surroundings(eye, nan, nan).centre, surroundings(eye, 0, 1).centre);
        EXPECT_EQ(landscape(eye, 0, -1).verticalFov, landscape(eye, 0, 1).verticalFov);
    }
}
