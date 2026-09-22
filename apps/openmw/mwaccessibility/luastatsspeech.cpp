#include "luastatsspeech.hpp"

#include <algorithm>
#include <map>

namespace MWAccessibility
{
    namespace
    {
        std::vector<const LuaStatsLine*> orderedLines(const LuaStatsSection& section);
        std::vector<const LuaStatsSection*> orderedSections(const std::vector<LuaStatsSection>& sections);

        // Count only rows that are actually on screen, so a section whose every
        // row is hidden is treated as empty rather than as an option that
        // expands to nothing.
        std::size_t countVisibleLines(const LuaStatsSection& section)
        {
            if (!section.mVisible)
                return 0;

            std::size_t n = 0;
            for (const LuaStatsLine& line : section.mLines)
                if (line.mVisible)
                    ++n;
            for (const LuaStatsSection& child : section.mSections)
                n += countVisibleLines(child);
            return n;
        }

        // Order a section's lines: the section's own alphabetical sort first
        // (if it asked for one), then placement, matching stats.lua L955-963.
        std::vector<const LuaStatsLine*> orderedLines(const LuaStatsSection& section)
        {
            std::vector<const LuaStatsLine*> lines;
            lines.reserve(section.mLines.size());
            for (const LuaStatsLine& line : section.mLines)
                lines.push_back(&line);

            if (section.mSort == LuaLineSort::LabelAscending)
                std::stable_sort(lines.begin(), lines.end(),
                    [](const LuaStatsLine* a, const LuaStatsLine* b) { return a->mLabel < b->mLabel; });
            else if (section.mSort == LuaLineSort::LabelDescending)
                std::stable_sort(lines.begin(), lines.end(),
                    [](const LuaStatsLine* a, const LuaStatsLine* b) { return a->mLabel > b->mLabel; });

            std::vector<std::string> ids;
            std::vector<LuaPlacement> placements;
            ids.reserve(lines.size());
            placements.reserve(lines.size());
            for (const LuaStatsLine* line : lines)
            {
                ids.push_back(line->mId);
                placements.push_back(line->mPlacement);
            }

            std::vector<const LuaStatsLine*> out;
            out.reserve(lines.size());
            for (const std::size_t index : orderByPlacement(ids, placements))
                out.push_back(lines[index]);
            return out;
        }

        std::vector<const LuaStatsSection*> orderedSections(const std::vector<LuaStatsSection>& sections)
        {
            std::vector<std::string> ids;
            std::vector<LuaPlacement> placements;
            ids.reserve(sections.size());
            placements.reserve(sections.size());
            for (const LuaStatsSection& section : sections)
            {
                ids.push_back(section.mId);
                placements.push_back(section.mPlacement);
            }

            std::vector<const LuaStatsSection*> out;
            out.reserve(sections.size());
            for (const std::size_t index : orderByPlacement(ids, placements))
                out.push_back(&sections[index]);
            return out;
        }

        // Append one section's rows. The heading passed down names the group a
        // row belongs to WITHIN a submenu; it is empty at the top level,
        // because the submenu's own name already says that.
        void appendSection(const LuaStatsSection& section, const std::string& header, std::vector<LuaStatsItem>& items)
        {
            if (!section.mVisible)
                return;

            // Subsections render before the section's own lines (stats.lua
            // L945-953, then L966).
            for (const LuaStatsSection* child : orderedSections(section.mSections))
            {
                // A nested section keeps its own name, and inherits its
                // parent's only when it has none, so a row is always announced
                // under a group the user can place.
                appendSection(*child, child->mHeader.empty() ? header : child->mHeader, items);
            }

            for (const LuaStatsLine* line : orderedLines(section))
            {
                // A row the mod has hidden is not on screen; speaking it would
                // report state the player cannot see and may not have.
                if (!line->mVisible)
                    continue;

                LuaStatsItem item;
                item.mText = joinLabelValue(line->mLabel, line->mValue);
                item.mLabel = line->mLabel;
                item.mSection = header;
                item.mId = line->mId;
                item.mTooltips = line->mTooltips;
                items.push_back(std::move(item));
            }
        }

        void appendOptions(
            const LuaStatsSection& section, const LuaSectionLabeller& labeller, std::vector<LuaStatsOption>& options)
        {
            if (countVisibleLines(section) == 0)
                return;

            std::string label = section.mHeader;
            if (label.empty() && labeller)
                label = labeller(section.mId);

            // An unnamed section with no visible rows of its own is only a
            // layout container (e.g. Enumeratio's SC_ROOT / SC_LEFT_ROOT).
            // Promote its groups in their authored order instead of making
            // the player open an internal id to reach meaningful headings.
            // Keep named sections and sections with their own rows intact.
            const bool hasOwnRows = std::any_of(
                section.mLines.begin(), section.mLines.end(), [](const LuaStatsLine& line) { return line.mVisible; });
            if (label.empty() && !hasOwnRows)
            {
                for (const LuaStatsSection* child : orderedSections(section.mSections))
                    appendOptions(*child, labeller, options);
                return;
            }

            LuaStatsOption option;
            option.mId = section.mId;
            // Truly unlabelled data-bearing sections still need a fallback:
            // never discard their rows or guess a title from their contents.
            option.mLabel = label.empty() ? section.mId : std::move(label);
            option.mChildren = flattenSection(section);
            options.push_back(std::move(option));
        }
    }

    std::vector<std::size_t> orderByPlacement(
        const std::vector<std::string>& ids, const std::vector<LuaPlacement>& placements)
    {
        const std::size_t count = std::min(ids.size(), placements.size());

        // Phase 1: priority descending, ties keeping arrival order.
        // std::stable_sort gives the tie-break for free, which is what the
        // mod's explicit originalIndex comparison achieves.
        std::vector<std::size_t> sorted(count);
        for (std::size_t i = 0; i < count; ++i)
            sorted[i] = i;
        std::stable_sort(sorted.begin(), sorted.end(),
            [&](std::size_t a, std::size_t b) { return placements[a].mPriority > placements[b].mPriority; });

        // Phase 2: re-insert by placement type. The target lookup searches only
        // what has ALREADY been placed, so an After whose target has not been
        // placed yet falls back to the end, exactly as the mod does.
        std::vector<std::size_t> out;
        out.reserve(count);
        for (const std::size_t index : sorted)
        {
            const LuaPlacement& placement = placements[index];

            if (placement.mType == LuaPlacementType::Top)
            {
                out.insert(out.begin(), index);
                continue;
            }

            if ((placement.mType == LuaPlacementType::After || placement.mType == LuaPlacementType::Before)
                && !placement.mTarget.empty())
            {
                const auto found = std::find_if(
                    out.begin(), out.end(), [&](std::size_t placed) { return ids[placed] == placement.mTarget; });
                if (found != out.end())
                {
                    out.insert(placement.mType == LuaPlacementType::After ? found + 1 : found, index);
                    continue;
                }
            }

            out.push_back(index);
        }

        return out;
    }

    std::string joinLabelValue(const std::string& label, const std::string& value)
    {
        if (label.empty())
            return value;
        if (value.empty())
            return label;
        return label + " " + value;
    }

    std::vector<LuaStatsItem> flattenSection(const LuaStatsSection& section)
    {
        std::vector<LuaStatsItem> items;
        appendSection(section, {}, items);
        return items;
    }

    std::vector<LuaStatsOption> buildOptions(const LuaStatsTree& tree, const LuaSectionLabeller& labeller)
    {
        std::vector<LuaStatsOption> options;

        // createPane in the extender sorts each pane independently. Retain
        // that boundary: Top/Before/After and priority have no meaning across
        // columns. Pane indices impose left-before-right reading order.
        std::map<std::size_t, std::vector<const LuaStatsBox*>> panes;
        for (const LuaStatsBox& box : tree.mBoxes)
            panes[box.mPaneIndex].push_back(&box);

        // Boxes are anonymous, so they never become options themselves: they
        // only decide the order their sections are read in.
        for (const auto& pane : panes)
        {
            const auto& boxes = pane.second;
            std::vector<std::string> boxIds;
            std::vector<LuaPlacement> boxPlacements;
            boxIds.reserve(boxes.size());
            boxPlacements.reserve(boxes.size());
            for (const LuaStatsBox* box : boxes)
            {
                boxIds.push_back(box->mId);
                boxPlacements.push_back(box->mPlacement);
            }

            for (const std::size_t boxIndex : orderByPlacement(boxIds, boxPlacements))
            {
                for (const LuaStatsSection* section : orderedSections(boxes[boxIndex]->mSections))
                    appendOptions(*section, labeller, options);
            }
        }

        return options;
    }

    namespace
    {
        bool hasSpeakableText(const LuaLayoutNode& node)
        {
            // Padding, frames and icons carry no text, and the flex containers
            // that hold the real content carry none of their own.
            return !node.mText.empty() && node.mText.find_first_not_of(" \t\r\n") != std::string::npos;
        }

        /// Append every piece of text in a subtree to one string, separated by
        /// spaces. Used for a subtree the mod lays out as a single row.
        void joinSubtreeText(const LuaLayoutNode& node, std::string& out)
        {
            if (hasSpeakableText(node))
            {
                if (!out.empty())
                    out += ' ';
                out += node.mText;
            }

            for (const LuaLayoutNode& child : node.mChildren)
                joinSubtreeText(child, out);
        }

        void collectTooltipText(const LuaLayoutNode& node, std::vector<std::string>& out)
        {
            // A horizontal Flex is one visual line, however many text nodes it
            // contains, so collapse the whole subtree into a single paragraph
            // rather than recursing into it. Without this, a labelled value is
            // read as two separate utterances -- its label and its number --
            // with no cue that they belong together.
            if (node.mHorizontal)
            {
                std::string row;
                joinSubtreeText(node, row);
                if (!row.empty())
                    out.push_back(std::move(row));
                return;
            }

            if (hasSpeakableText(node))
                out.push_back(node.mText);

            for (const LuaLayoutNode& child : node.mChildren)
            {
                // A node the mod named "value" holds the datum belonging to the
                // caption just emitted, so append it to that paragraph rather
                // than starting a new one. This is the vertical case: the level
                // tooltip stacks "Progress toward next level" above its progress
                // bar, so only the name tells us they are one item.
                std::string value;
                if (child.mIsValue)
                    joinSubtreeText(child, value);

                if (!value.empty() && !out.empty())
                    out.back() = joinLabelValue(out.back(), value);
                else if (!value.empty())
                    out.push_back(std::move(value));
                else
                    collectTooltipText(child, out);
            }
        }
    }

    std::vector<std::string> flattenTooltip(const LuaLayoutNode& root)
    {
        std::vector<std::string> lines;
        collectTooltipText(root, lines);
        return lines;
    }
}
