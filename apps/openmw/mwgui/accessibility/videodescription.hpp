#ifndef OPENMW_MWGUI_A11Y_VIDEODESCRIPTION_H
#define OPENMW_MWGUI_A11Y_VIDEODESCRIPTION_H

#include <string_view>

#include "../../mwaccessibility/videodescription.hpp"

namespace VFS
{
    class Manager;
}

namespace MWGui::A11y
{
    // One foreground movie owns one narration session. Background menu videos
    // never construct this object. No sidecar means no speech side effects.
    class VideoDescription
    {
    public:
        VideoDescription(const VFS::Manager& vfs, std::string_view video);
        ~VideoDescription();
        VideoDescription(const VideoDescription&) = delete;
        VideoDescription& operator=(const VideoDescription&) = delete;

        void update(double seconds);
        // Cancel speech on skip/end/quit/minimise, without replaying cancelled
        // cues on resume. Does not reset the track's playback cursor.
        void stop();

    private:
        MWAccessibility::VideoDescriptionTrack mTrack;
        bool mOwnsSpeech = false;
    };
}

#endif
