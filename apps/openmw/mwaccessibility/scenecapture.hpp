#ifndef OPENMW_MWACCESSIBILITY_SCENECAPTURE_H
#define OPENMW_MWACCESSIBILITY_SCENECAPTURE_H

#include <filesystem>

namespace osgViewer
{
    class Viewer;
}

namespace MWAccessibility
{
    // Input dispatch only queues; modal rendering must not re-enter SDL dispatch.
    bool requestSceneCapture();
    // Called after Lua synchronization, before scanner movement and world simulation.
    void updateSceneCapture(osgViewer::Viewer& viewer, const std::filesystem::path& screenshotPath);
}

#endif
