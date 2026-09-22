#include "videodescription.hpp"

#include <algorithm>
#include <cmath>
#include <istream>
#include <stdexcept>
#include <utility>

namespace MWAccessibility
{
    namespace
    {
        std::string_view trim(std::string_view value)
        {
            const auto first = value.find_first_not_of(" \t\r");
            if (first == std::string_view::npos)
                return {};
            return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
        }

        bool digits(std::string_view value)
        {
            return !value.empty()
                && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
        }

        [[noreturn]] void invalid(std::size_t line, std::string_view reason)
        {
            throw std::runtime_error("Audio description line " + std::to_string(line) + ": " + std::string(reason));
        }

        double timestamp(std::string_view value, std::size_t line)
        {
            value = trim(value);
            if (value.size() != 12 || value[2] != ':' || value[5] != ':' || (value[8] != ',' && value[8] != '.')
                || !digits(value.substr(0, 2)) || !digits(value.substr(3, 2)) || !digits(value.substr(6, 2))
                || !digits(value.substr(9, 3)))
                invalid(line, "expected HH:MM:SS,mmm timestamp");
            const int hours = (value[0] - '0') * 10 + value[1] - '0';
            const int minutes = (value[3] - '0') * 10 + value[4] - '0';
            const int seconds = (value[6] - '0') * 10 + value[7] - '0';
            const int millis = (value[9] - '0') * 100 + (value[10] - '0') * 10 + value[11] - '0';
            if (minutes >= 60 || seconds >= 60)
                invalid(line, "minutes and seconds must be below 60");
            return hours * 3600 + minutes * 60 + seconds + millis / 1000.0;
        }
    }

    std::vector<VideoDescriptionCue> parseVideoDescriptions(std::istream& input)
    {
        std::vector<VideoDescriptionCue> cues;
        std::string line;
        std::size_t lineNumber = 0;
        const auto nextLine = [&] {
            if (!std::getline(input, line))
            {
                if (input.bad())
                    invalid(lineNumber, "could not read file");
                return false;
            }
            ++lineNumber;
            if (lineNumber == 1 && line.starts_with("\xef\xbb\xbf"))
                line.erase(0, 3);
            return true;
        };

        while (nextLine())
        {
            if (trim(line).empty())
                continue;
            if (!digits(trim(line)))
                invalid(lineNumber, "expected numeric cue index");
            if (!nextLine())
                invalid(lineNumber, "missing cue timing");
            const std::string_view timing = trim(line);
            const auto arrow = timing.find("-->");
            if (arrow == std::string_view::npos)
                invalid(lineNumber, "missing --> between timestamps");
            VideoDescriptionCue cue{ timestamp(timing.substr(0, arrow), lineNumber),
                timestamp(timing.substr(arrow + 3), lineNumber), {} };
            if (cue.mEnd <= cue.mStart)
                invalid(lineNumber, "cue end must follow start");
            if (!cues.empty() && cue.mStart < cues.back().mStart)
                invalid(lineNumber, "cues must be ordered by start time");
            while (nextLine() && !trim(line).empty())
            {
                if (!cue.mText.empty())
                    cue.mText += ' ';
                cue.mText += trim(line);
            }
            if (cue.mText.empty())
                invalid(lineNumber, "missing cue text");
            cues.push_back(std::move(cue));
        }
        return cues;
    }

    VideoDescriptionTrack::VideoDescriptionTrack(std::vector<VideoDescriptionCue> cues)
        : mCues(std::move(cues))
    {
    }

    std::vector<std::string_view> VideoDescriptionTrack::advance(double seconds)
    {
        std::vector<std::string_view> due;
        if (!std::isfinite(seconds) || seconds < 0)
            return due;
        while (mNext < mCues.size() && mCues[mNext].mStart <= seconds)
        {
            const VideoDescriptionCue& cue = mCues[mNext++];
            if (seconds < cue.mEnd)
                due.emplace_back(cue.mText);
        }
        return due;
    }

    void VideoDescriptionTrack::reset()
    {
        mNext = 0;
    }
}
