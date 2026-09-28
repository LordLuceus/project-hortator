#include "apps/openmw/mwaccessibility/viewmode.hpp"

#include <gtest/gtest.h>

namespace MWAccessibility
{
    TEST(MWAccessibilityViewMode, InitialViewIsSilent)
    {
        ViewModeTracker tracker;
        EXPECT_EQ(tracker.update(true), std::nullopt);
        tracker.reset();
        EXPECT_EQ(tracker.update(false), std::nullopt);
    }

    TEST(MWAccessibilityViewMode, AnnouncesBothDirectionsOnce)
    {
        ViewModeTracker tracker;
        tracker.update(true);
        EXPECT_EQ(tracker.update(false), std::optional<bool>(false));
        EXPECT_EQ(tracker.update(false), std::nullopt);
        EXPECT_EQ(tracker.update(true), std::optional<bool>(true));
        EXPECT_EQ(tracker.update(true), std::nullopt);
    }

    TEST(MWAccessibilityViewMode, TemporaryModesDoNotChangeTheBaseline)
    {
        for (bool firstPerson : { true, false })
        {
            ViewModeTracker tracker;
            tracker.update(firstPerson);
            EXPECT_EQ(tracker.update(std::nullopt), std::nullopt);
            EXPECT_EQ(tracker.update(std::nullopt), std::nullopt);
            EXPECT_EQ(tracker.update(firstPerson), std::nullopt);
        }
    }

    TEST(MWAccessibilityViewMode, SwitchAfterPreviewIsAnnounced)
    {
        ViewModeTracker tracker;
        tracker.update(false);
        tracker.update(std::nullopt);
        EXPECT_EQ(tracker.update(true), std::optional<bool>(true));
    }

    TEST(MWAccessibilityViewMode, QueuedSwitchWaitsUntilSettled)
    {
        ViewModeTracker tracker;
        tracker.update(true);
        EXPECT_EQ(tracker.update(std::nullopt), std::nullopt);
        EXPECT_EQ(tracker.update(std::nullopt), std::nullopt);
        EXPECT_EQ(tracker.update(false), std::optional<bool>(false));
    }

    TEST(MWAccessibilityViewMode, CancelledSwitchOrPreviewReturnStaysSilent)
    {
        ViewModeTracker tracker;
        tracker.update(true);
        tracker.update(std::nullopt);
        // Includes vanilla's temporary third-person detour with first person queued.
        tracker.update(std::nullopt);
        EXPECT_EQ(tracker.update(true), std::nullopt);
    }

    TEST(MWAccessibilityViewMode, ReloadDoesNotAnnounceTheSavedView)
    {
        ViewModeTracker tracker;
        tracker.update(true);
        tracker.reset();
        EXPECT_EQ(tracker.update(std::nullopt), std::nullopt);
        EXPECT_EQ(tracker.update(false), std::nullopt);
        EXPECT_EQ(tracker.update(true), std::optional<bool>(true));
    }
}
