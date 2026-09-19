#ifndef OPENMW_MWACCESSIBILITY_LUASTATSTREE_H
#define OPENMW_MWACCESSIBILITY_LUASTATSTREE_H

#include <string>
#include <vector>

namespace MWAccessibility
{
    /// A snapshot of the content a Lua stats-window mod (Stats Window Extender,
    /// Nexus 57727) is displaying, in engine-neutral form.
    ///
    /// The mod publishes a four-level tree -- pane > box > section > line -- via
    /// its 'StatsWindow' script interface. Reading it is engine work (it needs a
    /// Lua call), but deciding how it should SOUND is not, so the shape of the
    /// announcement is decided by pure functions over these structs. That keeps
    /// the interesting half testable without a running game or the mod
    /// installed.
    ///
    /// Nothing here holds a Lua reference: a line's value and tooltip are
    /// functions on the Lua side, so they are called at READ time and their
    /// results copied. The snapshot is therefore safe to keep across frames
    /// even though the mod rebuilds its widget tree constantly.

    /// Where the mod places an entry among its siblings.
    ///
    /// Mirrors the mod's own constants.Placement. Display order is NOT insertion
    /// order: the mod sorts by priority and then re-inserts by type, in an
    /// unexported local function. We reproduce it, because speech that read the
    /// window in a different order from the screen would be wrong on its own
    /// terms -- the author chose that order.
    enum class LuaPlacementType
    {
        Bottom = 0, ///< Append. The mod's default.
        Top, ///< Insert first.
        After, ///< Immediately after mTarget.
        Before, ///< Immediately before mTarget.
    };

    struct LuaPlacement
    {
        LuaPlacementType mType = LuaPlacementType::Bottom;

        /// Id of the sibling this entry attaches to, for After / Before.
        std::string mTarget;

        /// Higher sorts earlier. The mod's default is 100.
        int mPriority = 100;
    };

    /// How a section orders its own lines before placement is applied.
    /// Mirrors the mod's constants.Sort.
    enum class LuaLineSort
    {
        AddedOrder = 0,
        LabelAscending,
        LabelDescending,
    };

    /// One row of the stats window: a label with an optional value.
    struct LuaStatsLine
    {
        /// The mod's line id. Not spoken; identifies the row for re-reads.
        std::string mId;

        /// Spoken name of the row, e.g. "Alteration".
        std::string mLabel;

        /// Spoken value, e.g. "37".
        ///
        /// Empty for a row that is only a label: a birthsign's abilities are
        /// listed as bare names with a tooltip and no value at all, so a value
        /// must never be assumed to exist.
        std::string mValue;

        /// Tooltip text, already flattened from the mod's tooltip layout. One
        /// entry per paragraph, cycled with T / Shift+T.
        ///
        /// This is the only way a keyboard user can reach these at all: the mod
        /// wires tooltips to mouseMove with no focus trigger, so they are never
        /// rendered for us to read.
        std::vector<std::string> mTooltips;

        /// False when the mod's visibleFn hides this row (e.g. Bounty when the
        /// player has none). A hidden row is not on screen, so it must not be
        /// spoken.
        bool mVisible = true;

        LuaPlacement mPlacement;
    };

    /// A named group of lines, e.g. "Major Skills". This is the level that
    /// becomes an expandable submenu: the mod puts its display names here, and
    /// nowhere else.
    struct LuaStatsSection
    {
        /// The mod's section id, e.g. "majorSkills". Not spoken directly, but
        /// used as a placement target, to look up a label for an unheaded
        /// section, and as a last-resort spoken fallback.
        std::string mId;

        /// Heading text, e.g. "Major Skills". EMPTY for the mod's own
        /// healthStats / levelStats / attributes sections, which are declared
        /// with no header at all -- those are labelled from their id.
        std::string mHeader;

        std::vector<LuaStatsLine> mLines;

        /// Nested sections. The mod renders these BEFORE the section's own
        /// lines, so flattening must do the same.
        std::vector<LuaStatsSection> mSections;

        /// How this section orders its lines before placement.
        LuaLineSort mSort = LuaLineSort::AddedOrder;

        /// False when the mod's visibleFn hides the whole section (e.g. Sign
        /// for a character with no birthsign).
        bool mVisible = true;

        LuaPlacement mPlacement;
    };

    /// A framed group in the window, e.g. the attributes box.
    ///
    /// Boxes are ANONYMOUS: the mod's addBoxToPane stores no title, and there
    /// is no l10n entry for one. They exist only to group sections for layout,
    /// so they contribute nothing to speech except the order their sections are
    /// read in.
    struct LuaStatsBox
    {
        /// The mod's box id, e.g. "attributesBox". Not spoken; used as a
        /// placement target.
        std::string mId;

        std::vector<LuaStatsSection> mSections;

        LuaPlacement mPlacement;
    };

    /// The whole window.
    ///
    /// Boxes are held in a single list in pane order (left pane first, then
    /// right). The mod returns its panes as a Lua MAP, whose iteration order is
    /// arbitrary, so the reader must flatten it by explicit key rather than by
    /// iterating -- otherwise the right pane's skills could precede the left
    /// pane's health on some runs and not others.
    struct LuaStatsTree
    {
        std::vector<LuaStatsBox> mBoxes;
    };
}

#endif
