#ifndef OPENMW_MWACCESSIBILITY_VIDEODESCRIPTION_H
#define OPENMW_MWACCESSIBILITY_VIDEODESCRIPTION_H

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace MWAccessibility
{
    struct VideoDescriptionCue
    {
        double mStart;
        double mEnd;
        std::string mText;
    };

    // Numbered, plain-text UTF-8 SRT. Reject malformed files as a whole rather
    // than speaking timestamps or partially misaligned descriptions.
    // An empty file is valid and can override/disable a bundled description.
    std::vector<VideoDescriptionCue> parseVideoDescriptions(std::istream& input);

    class VideoDescriptionTrack
    {
    public:
        explicit VideoDescriptionTrack(std::vector<VideoDescriptionCue> cues = {});

        // Use the movie clock, not wall time. Start-inclusive/end-exclusive;
        // skip cues whose entire window was missed (e.g. a decoding stall).
        // Returned views remain valid for this track's lifetime. End times
        // only govern starting a cue: they never truncate speech already sent.
        std::vector<std::string_view> advance(double seconds);
        void reset();
        bool empty() const { return mCues.empty(); }

    private:
        std::vector<VideoDescriptionCue> mCues;
        std::size_t mNext = 0;
    };
}

#endif
