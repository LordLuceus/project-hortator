#include "apps/openmw/mwaccessibility/videodescription.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace MWAccessibility
{
    namespace
    {
        std::vector<VideoDescriptionCue> parseDescriptions(const std::string& text)
        {
            std::istringstream stream(text);
            return parseVideoDescriptions(stream);
        }

        TEST(MWAccessibilityVideoDescription, emptyInputHasNoCues)
        {
            EXPECT_TRUE(parseDescriptions("").empty());
            EXPECT_TRUE(parseDescriptions(" \t\r\n\n \n").empty());
        }

        TEST(MWAccessibilityVideoDescription, parsesNumberedCuesAndFinalBlockWithoutNewline)
        {
            const auto cues = parseDescriptions(
                "7\n00:00:01,250 --> 00:00:02,500\nFirst description.\n\n"
                "3\n01:02:03.004 --> 01:02:05.006\nSecond description.");
            ASSERT_EQ(cues.size(), 2u);
            EXPECT_DOUBLE_EQ(cues[0].mStart, 1.250);
            EXPECT_DOUBLE_EQ(cues[0].mEnd, 2.500);
            EXPECT_EQ(cues[0].mText, "First description.");
            EXPECT_DOUBLE_EQ(cues[1].mStart, 3723.004);
            EXPECT_DOUBLE_EQ(cues[1].mEnd, 3725.006);
            EXPECT_EQ(cues[1].mText, "Second description.");
        }

        TEST(MWAccessibilityVideoDescription, acceptsBomCrLfWhitespaceSeparatorsAndMultilineText)
        {
            const auto cues = parseDescriptions(
                "\xEF\xBB\xBF"
                "1\r\n"
                "00:00:00,000 --> 00:00:01,000\r\n"
                "First line\r\nsecond line.\r\n \t\r\n\r\n"
                "9\r\n00:00:01,000 --> 00:00:02,000\r\nLast line.\r\n");
            ASSERT_EQ(cues.size(), 2u);
            EXPECT_DOUBLE_EQ(cues[0].mStart, 0.0);
            EXPECT_DOUBLE_EQ(cues[0].mEnd, 1.0);
            EXPECT_EQ(cues[0].mText, "First line second line.");
            EXPECT_EQ(cues[1].mText, "Last line.");
        }

        TEST(MWAccessibilityVideoDescription, acceptsOverlapsAndEqualStartsInFileOrder)
        {
            const auto cues = parseDescriptions(
                "1\n00:00:01,000 --> 00:00:05,000\nFirst\n\n"
                "2\n00:00:01,000 --> 00:00:03,000\nSecond\n\n"
                "3\n00:00:02,000 --> 00:00:04,000\nThird\n");
            ASSERT_EQ(cues.size(), 3u);
            EXPECT_EQ(cues[0].mText, "First");
            EXPECT_EQ(cues[1].mText, "Second");
            EXPECT_EQ(cues[2].mText, "Third");
        }

        TEST(MWAccessibilityVideoDescription, rejectsMalformedOrIncompleteBlocks)
        {
            const std::vector<std::string> invalid = {
                "1",
                "1\n00:00:00,000 --> 00:00:01,000\n",
                "1\n00:00:00,000 --> 00:00:01,000\n \t\n",
                "00:00:00,000 --> 00:00:01,000\nText\n",
                "word\n00:00:00,000 --> 00:00:01,000\nText\n",
                "1x\n00:00:00,000 --> 00:00:01,000\nText\n",
                "-1\n00:00:00,000 --> 00:00:01,000\nText\n",
                "1\nnot a timing line\nText\n",
                "1\n00:00:00,000 -> 00:00:01,000\nText\n",
                "1\n00:00:00,000 --> 00:00:01,000 extra\nText\n",
                "1\n00:00:00,000 --> 00:00:01,000\nText\n\ntrailing garbage\n",
            };
            for (const auto& text : invalid)
            {
                SCOPED_TRACE(text);
                EXPECT_THROW(parseDescriptions(text), std::runtime_error);
            }
        }

        TEST(MWAccessibilityVideoDescription, rejectsMalformedTimestampsAtEitherEndpoint)
        {
            const std::vector<std::string> invalid = {
                "0:00:01,000",
                "000:00:01,000",
                "00:0:01,000",
                "00:000:01,000",
                "00:00:1,000",
                "00:00:001,000",
                "00:00:01,00",
                "00:00:01,0000",
                "00:00:01",
                "00:00:01:000",
                "00:60:01,000",
                "00:00:60,000",
                "-1:00:01,000",
                "00:00:0x,000",
                "00:00:01,0x0",
                "nan",
                "inf",
            };
            for (const auto& timestamp : invalid)
            {
                SCOPED_TRACE(timestamp);
                EXPECT_THROW(parseDescriptions("1\n" + timestamp + " --> 99:59:59,999\nText\n"), std::runtime_error);
                EXPECT_THROW(parseDescriptions("1\n00:00:00,000 --> " + timestamp + "\nText\n"), std::runtime_error);
            }
        }

        TEST(MWAccessibilityVideoDescription, rejectsEmptyOrReversedIntervalsAndDecreasingStarts)
        {
            EXPECT_THROW(parseDescriptions("1\n00:00:01,000 --> 00:00:01,000\nText\n"), std::runtime_error);
            EXPECT_THROW(parseDescriptions("1\n00:00:02,000 --> 00:00:01,000\nText\n"), std::runtime_error);
            EXPECT_THROW(parseDescriptions("1\n00:00:02,000 --> 00:00:04,000\nFirst\n\n"
                                           "2\n00:00:01,000 --> 00:00:05,000\nSecond\n"),
                std::runtime_error);
        }

        TEST(MWAccessibilityVideoDescription, emptyTrackRemainsSilent)
        {
            VideoDescriptionTrack track;
            EXPECT_TRUE(track.empty());
            EXPECT_TRUE(track.advance(0.0).empty());
            EXPECT_TRUE(track.advance(100.0).empty());
            track.reset();
            EXPECT_TRUE(track.empty());
            EXPECT_TRUE(track.advance(0.0).empty());
        }

        TEST(MWAccessibilityVideoDescription, dispatchesAtStartOnlyOnceWhilePaused)
        {
            VideoDescriptionTrack track({ { 1.0, 2.0, "First" }, { 2.0, 3.0, "Second" } });
            EXPECT_FALSE(track.empty());
            EXPECT_TRUE(track.advance(0.999).empty());
            EXPECT_EQ(track.advance(1.0), (std::vector<std::string_view>{ "First" }));
            EXPECT_TRUE(track.advance(1.0).empty());
            EXPECT_TRUE(track.advance(1.5).empty());
            EXPECT_EQ(track.advance(2.0), (std::vector<std::string_view>{ "Second" }));
            EXPECT_TRUE(track.advance(2.0).empty());
            EXPECT_TRUE(track.advance(3.0).empty());
        }

        TEST(MWAccessibilityVideoDescription, endIsExclusiveOnFirstAdvance)
        {
            VideoDescriptionTrack beforeEnd({ { 1.0, 2.0, "Text" } });
            EXPECT_EQ(beforeEnd.advance(1.999), (std::vector<std::string_view>{ "Text" }));
            VideoDescriptionTrack atEnd({ { 1.0, 2.0, "Text" } });
            EXPECT_TRUE(atEnd.advance(2.0).empty());
            EXPECT_TRUE(atEnd.advance(1.5).empty());
        }

        TEST(MWAccessibilityVideoDescription, lateFramesDropExpiredCuesAndDispatchAllLiveCuesInOrder)
        {
            VideoDescriptionTrack track({ { 0.0, 1.0, "Expired" }, { 1.0, 6.0, "Long" }, { 2.0, 3.0, "Also expired" },
                { 2.0, 5.0, "Overlap" }, { 4.0, 6.0, "Due" }, { 5.0, 7.0, "Future" } });
            EXPECT_EQ(track.advance(4.0), (std::vector<std::string_view>{ "Long", "Overlap", "Due" }));
            EXPECT_TRUE(track.advance(4.0).empty());
            EXPECT_EQ(track.advance(5.0), (std::vector<std::string_view>{ "Future" }));
            EXPECT_TRUE(track.advance(100.0).empty());
        }

        TEST(MWAccessibilityVideoDescription, backwardClockDoesNotReplayDispatchedOrDroppedCues)
        {
            VideoDescriptionTrack track({ { 0.0, 1.0, "Dropped" }, { 1.0, 3.0, "Spoken" }, { 3.0, 4.0, "Future" } });
            EXPECT_EQ(track.advance(2.0), (std::vector<std::string_view>{ "Spoken" }));
            EXPECT_TRUE(track.advance(0.0).empty());
            EXPECT_TRUE(track.advance(1.0).empty());
            EXPECT_TRUE(track.advance(2.0).empty());
            EXPECT_EQ(track.advance(3.0), (std::vector<std::string_view>{ "Future" }));
        }

        TEST(MWAccessibilityVideoDescription, invalidClockValuesDoNotConsumeCues)
        {
            VideoDescriptionTrack track({ { 0.0, 1.0, "First" }, { 1.0, 2.0, "Second" } });
            const std::vector<double> invalid = { -1.0, -std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() };
            for (const double time : invalid)
                EXPECT_TRUE(track.advance(time).empty());
            EXPECT_EQ(track.advance(0.0), (std::vector<std::string_view>{ "First" }));
            for (const double time : invalid)
                EXPECT_TRUE(track.advance(time).empty());
            EXPECT_EQ(track.advance(1.0), (std::vector<std::string_view>{ "Second" }));
        }

        TEST(MWAccessibilityVideoDescription, resetAllowsDispatchedAndDroppedCuesToReplay)
        {
            VideoDescriptionTrack track({ { 0.0, 1.0, "First" }, { 1.0, 2.0, "Second" } });
            EXPECT_EQ(track.advance(1.0), (std::vector<std::string_view>{ "Second" }));
            EXPECT_TRUE(track.advance(10.0).empty());
            track.reset();
            EXPECT_EQ(track.advance(0.0), (std::vector<std::string_view>{ "First" }));
            EXPECT_EQ(track.advance(1.0), (std::vector<std::string_view>{ "Second" }));
            track.reset();
            EXPECT_EQ(track.advance(0.0), (std::vector<std::string_view>{ "First" }));
        }

        TEST(MWAccessibilityVideoDescription, bundledIntroductionContainsEightPlayableDescriptions)
        {
            const auto path = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data-mw" / "video"
                / "mw_intro.bik.ad.srt";
            std::ifstream stream(path, std::ios::binary);
            ASSERT_TRUE(stream.is_open()) << path;
            const auto cues = parseVideoDescriptions(stream);
            ASSERT_EQ(cues.size(), 8u);
            const std::vector<VideoDescriptionCue> expected{
                { 0.8, 6.5,
                    "Text: 'Each Event is preceded by Prophecy. But without the hero, there is no Event.' "
                    "Zurin Arctus, the Underking." },
                { 7.5, 21.5,
                    "Text scrolls: 'In the waning years of the Third Era of Tamriel, a prisoner born on a "
                    "certain day to uncertain parents was sent under guard, without explanation, to Morrowind, "
                    "ignorant of the role he was to play in that nation's history...'" },
                { 23.5, 27.5, "A desolate wasteland emerges beneath a thick, blood-red fog." },
                { 34.5, 36.0, "Glowing runes drift through the mist." },
                { 43.5, 46.0, "The runes dissolve into text: 'Many fall, but one remains.'" },
                { 46.5, 49.0, "Torrential rain pours over a swamp where a shirtless dark elf stands at the shore." },
                { 49.5, 51.0, "Surreal visions flash of a moon in space and ocean islands at night." },
                { 53.5, 55.0, "A wooden ship railing sinks into water beneath a stormy sky, fading to black." },
            };
            for (std::size_t i = 0; i < cues.size(); ++i)
            {
                SCOPED_TRACE(i);
                EXPECT_DOUBLE_EQ(cues[i].mStart, expected[i].mStart);
                EXPECT_DOUBLE_EQ(cues[i].mEnd, expected[i].mEnd);
                EXPECT_EQ(cues[i].mText, expected[i].mText);
                // A fresh track isolates each cue from overlapping descriptions.
                VideoDescriptionTrack single({ cues[i] });
                EXPECT_EQ(single.advance(cues[i].mStart), (std::vector<std::string_view>{ cues[i].mText }));
                EXPECT_TRUE(single.advance(cues[i].mEnd).empty());
            }
        }

        TEST(MWAccessibilityVideoDescription, dispatchedTextRemainsIntactAfterItsEnd)
        {
            VideoDescriptionTrack track({ { 0.0, 1.0, "A description that may take longer than its cue to speak." } });
            const auto queued = track.advance(0.0);
            ASSERT_EQ(queued.size(), 1u);
            EXPECT_TRUE(track.advance(1.0).empty());
            EXPECT_TRUE(track.advance(20.0).empty());
            EXPECT_EQ(queued[0], "A description that may take longer than its cue to speak.");
        }
    }
}
