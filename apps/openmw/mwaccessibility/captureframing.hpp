#ifndef OPENMW_MWACCESSIBILITY_CAPTUREFRAMING_H
#define OPENMW_MWACCESSIBILITY_CAPTUREFRAMING_H

#include <optional>

#include <osg/BoundingBox>
#include <osg/Vec3f>

namespace MWAccessibility::CaptureFraming
{
    struct View
    {
        osg::Vec3f eye;
        osg::Vec3f centre;
        float verticalFov; // Degrees; use world +Z as the look-at up vector.
    };

    // Fixed-eye perspective fit, with 10% tangent-space padding. Returns no view for
    // invalid/flat bounds, an eye in/on the original bounds, near-vertical aim, corner
    // depths <= 1 world unit, or a required vertical/horizontal FOV above 110 degrees.
    // closeUp fits only the upper half in world Z, not an anatomical head.
    // This does not test visibility, occlusion, or renderer near/far clipping planes.
    std::optional<View> frameTarget(const osg::Vec3f& eye, const osg::BoundingBox& bounds, float aspect, bool closeUp);

    // yaw is actor position.rot[2], in radians: zero = +Y, positive = toward +X.
    // MWRender::Camera::getYaw() has the opposite sign (camera.cpp::rotateCameraToTrackingPtr).
    // quarterTurn rotates clockwise, modulo four. Eye is never translated.
    // Presets require a finite eye; invalid yaw defaults to zero and invalid aspect
    // to 1. Surroundings: <=70 vertical/85 horizontal; landscape: <=90/110;
    // obstruction: <=70/100, pitched down 45 degrees. FOV values are degrees.
    View surroundings(const osg::Vec3f& eye, float yaw, float aspect, int quarterTurn = 0);
    View landscape(const osg::Vec3f& eye, float yaw, float aspect);
    View obstruction(const osg::Vec3f& eye, float yaw, float aspect);
}

#endif
