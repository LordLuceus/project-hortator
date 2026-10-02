#include "apps/openmw/mwrender/camera.hpp"

#include <gtest/gtest.h>
#include <osg/Camera>

namespace MWAccessibility
{
    TEST(MWAccessibilityCaptureCamera, RenderOverrideRestoresWithoutChangingGameplayCamera)
    {
        osg::ref_ptr<osg::Camera> renderCamera = new osg::Camera;
        MWRender::Camera camera(renderCamera);
        camera.setFirstPersonOffset(osg::Vec3f(5, 7, 120));
        renderCamera->setProjectionMatrixAsPerspective(60, 1.5, 1, 10000);
        camera.updateCamera(renderCamera);
        const auto originalView = camera.getViewMatrix();
        const auto originalProjection = camera.getProjectionMatrix();
        const auto mode = camera.getMode();
        const auto yaw = camera.getYaw();
        const auto pitch = camera.getPitch();
        const auto offset = camera.getFirstPersonOffset();

        for (int i = 0; i < 4; ++i)
        {
            const auto view = osg::Matrix::lookAt(
                osg::Vec3f(0, 0, 100), osg::Vec3f(100, 50.f * static_cast<float>(i), 30), osg::Vec3f(0, 0, 1));
            camera.setScreenshotView(view);
            camera.updateCamera(renderCamera);
            EXPECT_EQ(camera.getViewMatrix(), view);
            EXPECT_EQ(renderCamera->getViewMatrix(), view);
            EXPECT_EQ(camera.getMode(), mode);
            EXPECT_EQ(camera.getYaw(), yaw);
            EXPECT_EQ(camera.getPitch(), pitch);
            EXPECT_EQ(camera.getFirstPersonOffset(), offset);
            EXPECT_FALSE(camera.getQueuedMode());
        }
        camera.setScreenshotView(std::nullopt);
        camera.updateCamera(renderCamera);
        EXPECT_EQ(renderCamera->getViewMatrix(), originalView);
        EXPECT_EQ(camera.getViewMatrix(), originalView);
        EXPECT_EQ(camera.getProjectionMatrix(), originalProjection);
        EXPECT_EQ(camera.getMode(), mode);
    }
}
