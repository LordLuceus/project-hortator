#ifndef OPENMW_MWACCESSIBILITY_VIEWMODE_H
#define OPENMW_MWACCESSIBILITY_VIEWMODE_H

#include <optional>

namespace MWAccessibility
{
    /// Observe settled first/third-person views, ignoring temporary or queued modes.
    class ViewModeTracker
    {
    public:
        void reset() { mFirstPerson.reset(); }

        /// nullopt means preview, vanity, static camera or an unfinished transition.
        /// The first settled view after reset establishes a silent baseline.
        std::optional<bool> update(std::optional<bool> firstPerson)
        {
            if (!firstPerson)
                return std::nullopt;
            const bool changed = mFirstPerson && *mFirstPerson != *firstPerson;
            mFirstPerson = firstPerson;
            return changed ? firstPerson : std::nullopt;
        }

    private:
        std::optional<bool> mFirstPerson;
    };
}

#endif
