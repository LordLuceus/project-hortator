#ifndef OPENMW_MWACCESSIBILITY_LUASTATSREADER_H
#define OPENMW_MWACCESSIBILITY_LUASTATSREADER_H

#include <optional>
#include <string>

#include "luastatstree.hpp"

namespace MWAccessibility
{
    /// Reads the content of a Lua stats-window mod for the screen reader.
    ///
    /// Supports Stats Window Extender (Nexus 57727), which publishes its window
    /// as a 'StatsWindow' script interface. Reading it rather than scraping its
    /// widgets gets real labels and values -- and reaches its tooltips, which
    /// are wired to mouse movement only and so are never rendered at all for a
    /// keyboard user.
    ///
    /// Everything here is a no-op when the mod is absent, so the caller can ask
    /// unconditionally.
    namespace LuaStatsReader
    {
        /// The interface name published by Stats Window Extender.
        inline constexpr std::string_view sInterfaceName = "StatsWindow";

        /// True when a supported stats-window mod is installed and loaded.
        bool available();

        /// Snapshot the mod's current window content.
        ///
        /// Returns std::nullopt when the mod is absent or its model cannot be
        /// read; an empty tree when it is present but displaying nothing yet
        /// (its builders run lazily, so this happens on the first frames).
        std::optional<LuaStatsTree> read();

        /// Re-read one row's current value, by the line id carried in the
        /// snapshot.
        ///
        /// A snapshot's values are strings captured when it was taken, but
        /// speech must report the state at the moment it speaks: a stale
        /// number is a confident wrong answer, which is worse than silence
        /// when it is the only channel the player has. So a row's value is
        /// resolved again as it is announced.
        std::optional<std::string> readLineValue(const std::string& lineId);

        /// Re-read one row's tooltip paragraphs, by line id. Resolved on
        /// demand: tooltips are only wanted when the user presses T, and
        /// building every one up front would run arbitrary mod code for every
        /// row in the window on every rebuild.
        std::vector<std::string> readLineTooltip(const std::string& lineId);
    }
}

#endif
