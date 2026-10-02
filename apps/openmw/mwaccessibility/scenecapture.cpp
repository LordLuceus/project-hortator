#include "scenecapture.hpp"

#include "capturefiles.hpp"
#include "captureframing.hpp"
#include "scanner.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <utility>

#include <SDL.h>
#include <osg/ComputeBoundsVisitor>
#include <osg/Image>
#include <osgViewer/Viewer>

#include <components/debug/debuglog.hpp>
#include <components/l10n/manager.hpp>
#include <components/misc/timeconvert.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/screencapture.hpp>
#include <components/settings/values.hpp>
#include <components/stereo/stereomanager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwgui/accessibility/speech.hpp"
#include "../mwphysics/raycasting.hpp"
#include "../mwrender/renderingmanager.hpp"
#include "../mwrender/vismask.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/refdata.hpp"

namespace MWAccessibility
{
    namespace
    {
        bool sPending = false;
        bool sBusy = false;

        std::string text(const char* key)
        {
            return MWBase::Environment::get().getL10nManager()->getContext("Interface")->formatMessage(key, {});
        }

        int choose(const char* prompt, const std::vector<std::string>& buttons, int focus = 0, int cancel = -1)
        {
            if (cancel < 0)
                cancel = static_cast<int>(buttons.size()) - 1;
            auto wm = MWBase::Environment::get().getWindowManager();
            wm->interactiveMessageBox(text(prompt), buttons, true, focus, cancel);
            if (MWBase::Environment::get().getStateManager()->hasQuitRequest())
                return -1;
            const int pressed = wm->readPressedButton();
            if (pressed == cancel && std::string_view(prompt) != "CaptureSaved")
                MWGui::A11y::say(text("CaptureCancelled"), true);
            return pressed;
        }

        void openFolder(const std::filesystem::path& path)
        {
            std::filesystem::create_directories(path);
            const auto bytes = std::filesystem::absolute(path).generic_u8string();
            const std::string utf8(bytes.begin(), bytes.end());
            if (SDL_OpenURL(captureFolderUri(utf8).c_str()) != 0)
            {
                Log(Debug::Warning) << "[a11y] capture open-folder failed: " << SDL_GetError();
                MWGui::A11y::say(text("CaptureFolderFailed"), true);
            }
        }

        osg::BoundingBox targetBounds(const MWWorld::Ptr& target)
        {
            osg::BoundingBox result;
            if (auto* node = target.getRefData().getBaseNode())
            {
                osg::ComputeBoundsVisitor visitor;
                visitor.setTraversalMask(~(MWRender::Mask_ParticleSystem | MWRender::Mask_Effect));
                node->accept(visitor);
                result = visitor.getBoundingBox();
            }
            return result;
        }

        // Rays test rendered objects, not just collision shapes. They cannot prove
        // visibility through transparency, foliage, effects or darkness.
        bool targetVisible(MWBase::World& world, const MWWorld::Ptr& target, const CaptureFraming::View& view,
            const osg::BoundingBox& bounds, bool closeUp, bool& partial)
        {
            unsigned visible = 0;
            unsigned blocked = 0;
            for (float z : { closeUp ? 0.6f : 0.25f, 0.75f })
                for (float x : { 0.3f, 0.5f, 0.7f })
                {
                    const osg::Vec3f sample(bounds.xMin() + x * (bounds.xMax() - bounds.xMin()), bounds.center().y(),
                        bounds.zMin() + z * (bounds.zMax() - bounds.zMin()));
                    MWPhysics::RayCastingResult hit;
                    world.castRenderingRay(hit, view.eye, sample, true, false, {});
                    if (hit.mHit && hit.mHitObject == target)
                        ++visible;
                    else if (hit.mHit)
                        ++blocked;
                }
            partial = blocked > 0;
            return visible > 0;
        }

        struct Shot
        {
            CaptureFraming::View view;
            std::string name;
            osg::Vec3f up{ 0, 0, 1 };
        };

        void capture(osgViewer::Viewer& viewer, const std::filesystem::path& root)
        {
            auto& world = *MWBase::Environment::get().getWorld();
            const auto selected = Scanner::instance().selectedObject();
            // Snapshot the actual rendered eye, including the first-person neck
            // correction; no subsequent menu or batch image changes this pose.
            osg::Vec3d eye, centre, up;
            viewer.getCamera()->getViewMatrixAsLookAt(eye, centre, up);
            const auto* viewport = viewer.getCamera()->getViewport();
            if (!viewport || viewport->width() < 1 || viewport->height() < 1)
                throw std::runtime_error("No capture viewport");
            const int width = static_cast<int>(viewport->width());
            const int height = static_cast<int>(viewport->height());
            const float aspect = static_cast<float>(width) / height;
            const float yaw = world.getPlayerPtr().getRefData().getPosition().rot[2];
            const float fov = static_cast<float>(osg::RadiansToDegrees(
                2.0 * std::atan(1.0 / std::abs(viewer.getCamera()->getProjectionMatrix()(1, 1)))));
            const auto folder = root / "Hortator Captures";
            const auto targetLabel = selected.isEmpty()
                ? text("CaptureTarget")
                : MWBase::Environment::get()
                      .getL10nManager()
                      ->getContext("Interface")
                      ->formatMessage(
                          "CaptureNamedTarget", { "name" }, { L10n::toUnicode(selected.getClass().getName(selected)) });

            const int mode = choose("CaptureMenu",
                { text("CaptureCurrent"), targetLabel, text("CaptureSurroundings"), text("CaptureLandscape"),
                    text("CaptureObstruction"), text("CaptureOpenFolder"), text("Cancel") });
            if (mode < 0 || mode == 6)
                return;
            if (mode == 5)
            {
                openFolder(folder);
                return;
            }
            if (Stereo::getStereo())
            {
                MWGui::A11y::say(text("CaptureStereoUnavailable"), true);
                return;
            }

            std::vector<Shot> shots;
            std::string subject;
            if (mode == 1)
            {
                if (selected.isEmpty())
                {
                    MWGui::A11y::say(text("CaptureNoTarget"), true);
                    return;
                }
                subject = selected.getClass().getName(selected);
                const int framing
                    = choose("CaptureTargetMenu", { text("CaptureWhole"), text("CaptureClose"), text("Cancel") });
                if (framing < 0 || framing == 2)
                    return;
                const auto bounds = targetBounds(selected);
                const auto view = CaptureFraming::frameTarget(eye, bounds, aspect, framing == 1);
                if (!view)
                {
                    MWGui::A11y::say(text("CaptureCannotFrame"), true);
                    return;
                }
                osg::Vec3f forward = view->centre - view->eye;
                forward.normalize();
                for (unsigned i = 0; i < 8; ++i)
                    if ((bounds.corner(i) - view->eye) * forward <= Settings::camera().mNearClip.get() + 1.f)
                    {
                        MWGui::A11y::say(text("CaptureCannotFrame"), true);
                        return;
                    }
                bool partial = false;
                if (!targetVisible(world, selected, *view, bounds, framing == 1, partial))
                {
                    MWGui::A11y::say(text("CaptureTargetHidden"), true);
                    return;
                }
                if (partial && choose("CapturePartial", { text("Cancel"), text("CaptureAnyway") }, 0, 0) != 1)
                    return;
                shots.push_back({ *view, framing == 1 ? "target-close-up" : "target-whole" });
            }
            else if (mode == 2)
            {
                const int directions = choose(
                    "CaptureSurroundingsMenu", { text("CaptureAhead"), text("CaptureFourDirections"), text("Cancel") });
                if (directions < 0 || directions == 2)
                    return;
                constexpr std::array names{ "surroundings-front", "surroundings-right", "surroundings-back",
                    "surroundings-left" };
                for (int i = 0; i < (directions == 1 ? 4 : 1); ++i)
                    shots.push_back({ CaptureFraming::surroundings(eye, yaw, aspect, i), names[i] });
            }
            else if (mode == 3)
                shots.push_back({ CaptureFraming::landscape(eye, yaw, aspect), "landscape" });
            else if (mode == 4)
            {
                // Keep both the level approach and the immediate footing: neither
                // image requires turning or moving the player to recreate a blockage.
                shots.push_back({ CaptureFraming::surroundings(eye, yaw, aspect), "obstruction-ahead" });
                auto ground = CaptureFraming::obstruction(eye, yaw, aspect);
                // In third person the camera is behind the player. Aim near the
                // player's footing, not at the ground underneath that camera.
                ground.centre = world.getPlayerPtr().getRefData().getPosition().asVec3()
                    + osg::Vec3f(std::sin(yaw) * 64.f, std::cos(yaw) * 64.f, 8.f);
                shots.push_back({ ground, "obstruction-ground" });
            }
            else
                shots.push_back({ { eye, centre, fov }, "current-view", up });

            if (mode == 2 || mode == 3)
            {
                bool nearWall = false;
                for (const auto& shot : shots)
                {
                    auto forward = shot.view.centre - shot.view.eye;
                    forward.normalize();
                    MWPhysics::RayCastingResult hit;
                    world.castRenderingRay(hit, shot.view.eye, shot.view.eye + forward * 64.f, true, false, {});
                    nearWall |= hit.mHit;
                }
                if (nearWall && choose("CaptureNearWall", { text("Cancel"), text("CaptureAnyway") }, 0, 0) != 1)
                    return;
            }

            // Modal input processes resize events. The fit and readback must use
            // the same viewport; retry rather than silently crop a stale plan.
            if (viewer.getCamera()->getViewport()->width() != width
                || viewer.getCamera()->getViewport()->height() != height)
            {
                MWGui::A11y::say(text("CaptureResized"), true);
                return;
            }
            MWGui::A11y::say(text("CaptureWorking"), true);
            std::filesystem::create_directories(folder);
            const auto stamp = Misc::timeToString(std::chrono::system_clock::now(), "%Y-%m-%d_%H-%M-%S");
            std::filesystem::path session;
            for (unsigned suffix = 0;; ++suffix)
            {
                session = folder / ("capture-" + stamp + (suffix == 0 ? "" : "-" + std::to_string(suffix)));
                if (std::filesystem::create_directory(session))
                    break;
            }
            for (const auto& shot : shots)
            {
                osg::ref_ptr<osg::Image> image = new osg::Image;
                const osg::Matrix view = osg::Matrix::lookAt(shot.view.eye, shot.view.centre, shot.up);
                world.getRenderingManager()->screenshot(image, width, height, view, shot.view.verticalFov);
                if (image->s() != width || image->t() != height)
                    throw std::runtime_error("Incomplete captured image");
                const auto saved = SceneUtil::writeScreenshotToFile(session, "png", *image);
                if (saved.empty())
                    throw std::runtime_error("Could not save captured image");
                std::filesystem::rename(session / saved, session / (shot.name + ".png"));
            }
            // A short local companion identifies the pictures without exporting
            // quest state, nearby unseen objects, or the user's machine paths.
            std::ofstream notes(session / "capture.txt", std::ios::binary);
            notes << text("CaptureNotes") << "\n" << world.getCellName() << "\n";
            if (!subject.empty())
                notes << subject << "\n";
            for (const auto& shot : shots)
                notes << shot.name << ".png\n";
            notes.close();
            if (!notes)
                throw std::runtime_error("Could not write capture description");
            if (choose("CaptureSaved", { text("CaptureOpenSaved"), text("CaptureReturn") }, 1) == 0)
                openFolder(session);
        }
    }

    bool requestSceneCapture()
    {
        // EXPERIMENTAL (2026-10-02): framing presets need feedback on usefulness,
        // not just geometric correctness. Ordinary Shift-screenshots are unchanged.
        if (sBusy)
            return true;
        if (!Scanner::isGameplayActive())
            return false;
        sPending = true;
        return true;
    }

    void updateSceneCapture(osgViewer::Viewer& viewer, const std::filesystem::path& screenshotPath)
    {
        if (!std::exchange(sPending, false) || sBusy || !Scanner::isGameplayActive())
            return;
        sBusy = true;
        try
        {
            capture(viewer, screenshotPath);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "[a11y] scene capture failed: " << e.what();
            MWGui::A11y::say(text("CaptureFailed"), true);
        }
        sBusy = false;
    }
}
