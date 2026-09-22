#include "videodescription.hpp"

#include <exception>
#include <string>

#include <components/accessibility/accessibilitymanager.hpp>
#include <components/debug/debuglog.hpp>
#include <components/vfs/manager.hpp>

#include "speech.hpp"

namespace MWGui::A11y
{
    VideoDescription::VideoDescription(const VFS::Manager& vfs, std::string_view video)
    {
        if (!Accessibility::AccessibilityManager::instance().isInitialised())
            return;
        const VFS::Path::Normalized path(std::string(video) + ".ad.srt");
        try
        {
            const auto input = vfs.find(path);
            if (!input)
                return;
            mTrack = MWAccessibility::VideoDescriptionTrack(MWAccessibility::parseVideoDescriptions(*input));
            if (!mTrack.empty())
                Log(Debug::Info) << "[a11y] Loaded video description: " << path;
        }
        catch (const std::exception& e)
        {
            logWarn("Could not load video description " + std::string(path.value()) + ": " + e.what());
            say("#{Interface:VideoDescriptionUnavailable}", /*interrupt=*/true);
            mOwnsSpeech = true;
        }
    }

    VideoDescription::~VideoDescription()
    {
        stop();
    }

    void VideoDescription::update(double seconds)
    {
        for (std::string_view text : mTrack.advance(seconds))
        {
            // Clear any old menu announcement for the first cue, then let
            // descriptions finish even if their subtitle window has ended.
            say(text, /*interrupt=*/!mOwnsSpeech);
            mOwnsSpeech = true;
        }
    }

    void VideoDescription::stop()
    {
        if (mOwnsSpeech)
        {
            Accessibility::AccessibilityManager::instance().stop();
            mOwnsSpeech = false;
        }
    }
}
