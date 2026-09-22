#ifndef OPENMW_MWACCESSIBILITY_LUASTATSSPEECH_H
#define OPENMW_MWACCESSIBILITY_LUASTATSSPEECH_H

#include <functional>
#include <string>
#include <vector>

#include "luastatstree.hpp"

namespace MWAccessibility
{
    /// How one row of a Lua stats window should be announced.
    struct LuaStatsItem
    {
        /// Spoken text as captured, e.g. "Alteration 37".
        std::string mText;

        /// The row's name on its own, e.g. "Alteration". Kept separate so the
        /// caller can rejoin it with a freshly-read value at speech time
        /// instead of announcing the captured one.
        std::string mLabel;

        /// Sub-group heading, announced when navigation crosses into a
        /// different one. Only set for rows in a NESTED section, whose parent
        /// is already the submenu's own name.
        std::string mSection;

        /// The mod's line id, so the caller can re-read this row later.
        std::string mId;

        /// Tooltip paragraphs for this row, cycled with T / Shift+T.
        std::vector<std::string> mTooltips;
    };

    /// One navigable option on the accessible stats screen.
    ///
    /// Every option is a submenu: each of the mod's sections becomes exactly
    /// one, with no flat top-level rows. A single universal rule was preferred
    /// over special-casing the vitals -- health is already available at any
    /// time on a dedicated key and in the HUD, so it loses nothing by sitting
    /// one keypress deep, and the structure then generalises to any dependent
    /// mod's sections without further decisions.
    struct LuaStatsOption
    {
        /// Spoken name of the submenu, e.g. "Major Skills".
        std::string mLabel;

        /// The mod's section id.
        std::string mId;

        /// The rows inside.
        std::vector<LuaStatsItem> mChildren;
    };

    /// Supplies a label for a section the mod left unheaded, given its id.
    /// Returns an empty string when it has nothing better to offer.
    ///
    /// Used to name healthStats / levelStats / attributes, which the mod
    /// declares with no header, no l10n entry and no box title -- so no display
    /// name for them exists anywhere in the mod. The engine supplies localized
    /// labels: Vitals for health/magicka/fatigue, and vanilla GMSTs for Level
    /// and Attributes.
    using LuaSectionLabeller = std::function<std::string(const std::string& sectionId)>;

    /// Reorder entries the way the mod draws them.
    ///
    /// Reproduces the mod's unexported sortSections: a stable sort by placement
    /// priority DESCENDING (default 100), then re-insertion by placement type
    /// -- Top first, Bottom appended, After/Before next to the target id if it
    /// has already been placed, otherwise appended.
    ///
    /// \p ids supplies each entry's id and \p placements its placement, so the
    /// same logic serves boxes, sections and lines. Returns the indices of the
    /// entries in display order.
    std::vector<std::size_t> orderByPlacement(
        const std::vector<std::string>& ids, const std::vector<LuaPlacement>& placements);

    /// Flatten one section's rows, including any nested sections, in the order
    /// the mod renders them (subsections first, then the section's own lines).
    std::vector<LuaStatsItem> flattenSection(const LuaStatsSection& section);

    /// Decide the submenu list for a whole window.
    ///
    /// One option per visible section, in the mod's display order. Sections
    /// whose rows are all hidden are dropped, so no option expands to nothing.
    /// \p labeller names any section the mod left unheaded.
    /// Unnamed sections with no visible rows of their own are transparent:
    /// their child groups become options, recursively, in display order.
    std::vector<LuaStatsOption> buildOptions(const LuaStatsTree& tree, const LuaSectionLabeller& labeller = {});

    /// Join a label and a value for speech, e.g. ("Health", "42 / 60") ->
    /// "Health 42 / 60". Either part may be empty.
    std::string joinLabelValue(const std::string& label, const std::string& value);

    /// A node of a Lua UI layout, reduced to the parts that carry text.
    ///
    /// The mod builds tooltips as nested layout tables of the form
    /// { props = { text = ... }, content = { ...children... } }, uniformly, so
    /// the text can be recovered by walking the tree its builder produced
    /// without the tooltip ever being rendered -- which matters because it
    /// never is for a keyboard user.
    struct LuaLayoutNode
    {
        std::string mText;

        /// True when this node lays its children out in a ROW (a Flex with
        /// props.horizontal set). Everything under such a node is one visual
        /// line, so its descendants' text must be spoken joined rather than as
        /// separate paragraphs.
        bool mHorizontal = false;

        /// True when the mod named this node "value" -- its own marker for the
        /// part of a row that holds the datum, as opposed to its caption. The
        /// mod uses it for plain values, for progress bars (whose text is
        /// "current/maximum") and for custom widgets.
        ///
        /// This is needed as well as mHorizontal because a value is not always
        /// laid beside its caption: the level tooltip stacks the heading
        /// "Progress toward next level" ABOVE its progress bar as vertical
        /// siblings, so nothing about the layout direction reveals that the two
        /// belong together. The name does.
        bool mIsValue = false;

        std::vector<LuaLayoutNode> mChildren;
    };

    /// Collect a tooltip layout's text into one paragraph per visual ROW, in
    /// the order the mod laid them out (title, then body, then detail).
    ///
    /// A row is either a single text node or a horizontal Flex subtree, whose
    /// text is joined into one paragraph: the mod composes a labelled value out
    /// of sibling "label" and "value" text nodes inside one horizontal Flex, and
    /// a progress bar out of a container whose child carries "current/maximum".
    /// Emitting a paragraph per text node splits those in half.
    ///
    /// Blank nodes -- the padding and framing that make up most of a layout --
    /// are skipped rather than yielding paragraphs that read as silence.
    std::vector<std::string> flattenTooltip(const LuaLayoutNode& root);
}

#endif
