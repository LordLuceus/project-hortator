#include <gtest/gtest.h>

#include <apps/openmw/mwaccessibility/luastatsspeech.hpp>

namespace
{
    using namespace MWAccessibility;

    LuaStatsLine line(std::string id, std::string label, std::string value = {})
    {
        LuaStatsLine l;
        l.mId = std::move(id);
        l.mLabel = std::move(label);
        l.mValue = std::move(value);
        return l;
    }

    LuaStatsSection section(std::string id, std::string header, std::vector<LuaStatsLine> lines)
    {
        LuaStatsSection s;
        s.mId = std::move(id);
        s.mHeader = std::move(header);
        s.mLines = std::move(lines);
        return s;
    }

    LuaStatsBox box(std::string id, std::vector<LuaStatsSection> sections)
    {
        LuaStatsBox b;
        b.mId = std::move(id);
        b.mSections = std::move(sections);
        return b;
    }

    LuaLayoutNode textNode(std::string text)
    {
        LuaLayoutNode n;
        n.mText = std::move(text);
        return n;
    }

    LuaLayoutNode parentNode(std::vector<LuaLayoutNode> children)
    {
        LuaLayoutNode n;
        n.mChildren = std::move(children);
        return n;
    }

    // The mod's real default layout, as observed from a running game: four
    // boxes in the left pane and one in the right. Section ids and headers are
    // exactly what the interface reported, including the three sections the mod
    // declares with no header at all.
    LuaStatsTree defaultWindow()
    {
        LuaStatsTree tree;
        tree.mBoxes = {
            box("healthStatsBox",
                { section("healthStats", "",
                    { line("health", "Health", "42/60"), line("magicka", "Magicka", "10/30"),
                        line("fatigue", "Fatigue", "80/100") }) }),
            box("levelStatsBox",
                { section("levelStats", "",
                    { line("level", "Level", "5"), line("race", "Race", "Dunmer"),
                        line("class", "Class", "Nightblade") }) }),
            box("attributesBox",
                { section("attributes", "",
                    { line("strength", "Strength", "40"), line("intelligence", "Intelligence", "50") }) }),
            box("rightScrollBox",
                { section("majorSkills", "Major Skills", { line("acrobatics", "Acrobatics", "32") }),
                    section("minorSkills", "Minor Skills", { line("speechcraft", "Speechcraft", "21") }) }),
        };
        return tree;
    }

    // Stands in for the engine's GMST lookup, which names the sections the mod
    // leaves unheaded.
    std::string labelFor(const std::string& sectionId)
    {
        if (sectionId == "healthStats")
            return "Stats";
        if (sectionId == "levelStats")
            return "Level";
        if (sectionId == "attributes")
            return "Attributes";
        return {};
    }

    TEST(MWAccessibilityLuaStats, JoinsALabelAndAValue)
    {
        EXPECT_EQ(joinLabelValue("Health", "42 / 60"), "Health 42 / 60");
    }

    TEST(MWAccessibilityLuaStats, JoinsTolerateAMissingHalf)
    {
        // A birthsign's abilities are listed as bare labels with no value, so a
        // missing half must not produce a stray space.
        EXPECT_EQ(joinLabelValue("The Apprentice", ""), "The Apprentice");
        EXPECT_EQ(joinLabelValue("", "37"), "37");
        EXPECT_EQ(joinLabelValue("", ""), "");
    }

    TEST(MWAccessibilityLuaStats, EveryDefaultSectionBecomesOneSubmenu)
    {
        // Data-bearing sections remain submenus, not flat top-level rows.
        // Anonymous boxes and purely structural sections never appear.
        const std::vector<LuaStatsOption> options = buildOptions(defaultWindow(), labelFor);

        ASSERT_EQ(options.size(), 5);
        EXPECT_EQ(options[0].mLabel, "Stats");
        EXPECT_EQ(options[1].mLabel, "Level");
        EXPECT_EQ(options[2].mLabel, "Attributes");
        EXPECT_EQ(options[3].mLabel, "Major Skills");
        EXPECT_EQ(options[4].mLabel, "Minor Skills");
    }

    TEST(MWAccessibilityLuaStats, VitalsAreASubmenuRatherThanTopLevelRows)
    {
        // Health sits one keypress deep on purpose: it is already available at
        // any time on a dedicated key and in the HUD, so a single universal
        // rule beats an exception for it.
        const std::vector<LuaStatsOption> options = buildOptions(defaultWindow(), labelFor);

        ASSERT_FALSE(options.empty());
        ASSERT_EQ(options[0].mChildren.size(), 3);
        EXPECT_EQ(options[0].mChildren[0].mText, "Health 42/60");
        EXPECT_EQ(options[0].mChildren[2].mText, "Fatigue 80/100");
    }

    TEST(MWAccessibilityLuaStats, BoxesAreAnonymousAndNeverSpoken)
    {
        // Boxes carry no label or header field at all -- only an id. Speaking
        // one would announce "attributesBox".
        const std::vector<LuaStatsOption> options = buildOptions(defaultWindow(), labelFor);

        for (const LuaStatsOption& option : options)
        {
            EXPECT_EQ(option.mLabel.find("Box"), std::string::npos) << "box id leaked into speech: " << option.mLabel;
            EXPECT_EQ(option.mLabel.find("box"), std::string::npos) << "box id leaked into speech: " << option.mLabel;
        }
    }

    TEST(MWAccessibilityLuaStats, SectionsFromDifferentBoxesKeepPaneOrder)
    {
        // The left pane's boxes must precede the right pane's. The mod returns
        // its panes as a map, so this ordering is the reader's responsibility;
        // here it is simply that box order is preserved.
        const std::vector<LuaStatsOption> options = buildOptions(defaultWindow(), labelFor);

        ASSERT_EQ(options.size(), 5);
        EXPECT_EQ(options[0].mId, "healthStats");
        EXPECT_EQ(options[4].mId, "minorSkills");
    }

    TEST(MWAccessibilityLuaStats, AnUnheadedSectionIsNamedByTheLabeller)
    {
        // The mod declares healthStats / levelStats / attributes with an empty
        // table: no header, no l10n entry, no box title. A name has to come
        // from the engine's GMSTs, so it matches the vanilla window's wording.
        LuaStatsTree tree;
        tree.mBoxes = { box("attributesBox", { section("attributes", "", { line("strength", "Strength", "40") }) }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mLabel, "Attributes");
    }

    TEST(MWAccessibilityLuaStats, AnUnknownUnheadedSectionFallsBackToItsId)
    {
        // A third-party mod's unheaded section has no name anywhere. The id is
        // at least a word from that mod's own documentation, and a nameless
        // submenu would be unusable.
        LuaStatsTree tree;
        tree.mBoxes = { box("someModBox", { section("someModSection", "", { line("x", "Row", "1") }) }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mLabel, "someModSection");
    }

    TEST(MWAccessibilityLuaStats, EnumeratioRootsExposeTheirActualGroups)
    {
        // Enumeratio's shipped layout with Stats on the left: the two roots
        // have no headers or rows, only these explicitly named child groups.
        auto left = section("SC_LEFT_ROOT", "", {});
        left.mSections = { section("SC_STATS", "Stats", { line("QUEST_COUNT", "Quests", "4") }) };
        auto right = section("SC_ROOT", "", {});
        right.mSections = {
            section("SC_NEEDS", "Primary Needs", { line("meals", "Meals", "2") }),
            section("SC_INTERACT", "Interactions", { line("talks", "Conversations", "3") }),
            section("SC_COMBAT", "Combat", { line("kills", "Kills", "5") }),
            section("SC_MAGIC", "Magic", { line("casts", "Spells cast", "6") }),
            section("SC_CRIME", "Crime", { line("bounty", "Highest bounty", "7") }),
            section("SC_MISC", "Misc", { line("misc", "Counter", "8") }),
            section("SC_INSULTS", "Insults", { line("insults", "Insults", "9") }),
        };
        LuaStatsTree tree;
        tree.mBoxes = { box("SC_LEFT_BOX", { left }), box("rightScrollBox", { right }) };
        const auto options = buildOptions(tree, labelFor);
        const std::vector<std::string> names{ "Stats", "Primary Needs", "Interactions", "Combat", "Magic", "Crime",
            "Misc", "Insults" };
        ASSERT_EQ(options.size(), names.size());
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            EXPECT_EQ(options[i].mLabel, names[i]);
            ASSERT_EQ(options[i].mChildren.size(), 1);
            EXPECT_TRUE(options[i].mChildren[0].mSection.empty());
        }
        EXPECT_EQ(options[0].mId, "SC_STATS");
        EXPECT_EQ(options[0].mChildren[0].mText, "Quests 4");
    }

    TEST(MWAccessibilityLuaStats, AnonymousWrappersAreTransparentAtMultipleDepths)
    {
        auto inner = section("inner", "", {});
        inner.mSections = { section("named", "Authored heading", { line("x", "Row", "1") }) };
        auto outer = section("outer", "", {});
        outer.mSections = { inner };
        LuaStatsTree tree;
        tree.mBoxes = { box("b", { outer }) };
        const auto options = buildOptions(tree);
        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mId, "named");
        EXPECT_EQ(options[0].mLabel, "Authored heading");
    }

    TEST(MWAccessibilityLuaStats, UnnamedSectionsWithOwnRowsAreNotDiscarded)
    {
        auto root = section("unnamed", "", { line("own", "Own stat", "1") });
        root.mSections = { section("child", "Child", { line("nested", "Nested stat", "2") }) };
        LuaStatsTree tree;
        tree.mBoxes = { box("b", { root }) };
        const auto options = buildOptions(tree, labelFor);
        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mLabel, "unnamed");
        ASSERT_EQ(options[0].mChildren.size(), 2);
        EXPECT_EQ(options[0].mChildren[0].mId, "nested");
        EXPECT_EQ(options[0].mChildren[1].mId, "own");
    }

    TEST(MWAccessibilityLuaStats, HiddenOwnRowsDoNotPreventPromotion)
    {
        auto hidden = line("hidden", "Hidden stat", "1");
        hidden.mVisible = false;
        auto root = section("root", "", { hidden });
        root.mSections = { section("child", "Child", { line("shown", "Shown stat", "2") }) };
        LuaStatsTree tree;
        tree.mBoxes = { box("b", { root }) };
        const auto options = buildOptions(tree, labelFor);
        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mId, "child");
        ASSERT_EQ(options[0].mChildren.size(), 1);
        EXPECT_EQ(options[0].mChildren[0].mId, "shown");
    }

    TEST(MWAccessibilityLuaStats, NamedWrapperStillKeepsItsGroupsTogether)
    {
        auto root = section("root", "Named parent", {});
        root.mSections = { section("child", "Child", { line("x", "Row", "1") }) };
        LuaStatsTree tree;
        tree.mBoxes = { box("b", { root }) };
        const auto options = buildOptions(tree, labelFor);
        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mLabel, "Named parent");
        EXPECT_EQ(options[0].mChildren[0].mSection, "Child");
    }

    TEST(MWAccessibilityLuaStats, EngineNamedWrapperIsNotPromoted)
    {
        auto root = section("attributes", "", {});
        root.mSections = { section("child", "Child", { line("x", "Row", "1") }) };
        LuaStatsTree tree;
        tree.mBoxes = { box("b", { root }) };
        const auto options = buildOptions(tree, labelFor);
        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mId, "attributes");
        EXPECT_EQ(options[0].mLabel, "Attributes");
    }

    TEST(MWAccessibilityLuaStats, HiddenWrappersAndChildrenStayHidden)
    {
        auto hidden = section("hidden", "Hidden", { line("x", "Row", "1") });
        hidden.mVisible = false;
        auto root = section("root", "", {});
        root.mSections
            = { hidden, section("empty", "Empty", {}), section("shown", "Shown", { line("y", "Visible", "2") }) };
        auto hiddenRoot = root;
        hiddenRoot.mVisible = false;
        LuaStatsTree tree;
        tree.mBoxes = { box("b", { hiddenRoot, root }) };
        const auto options = buildOptions(tree, labelFor);
        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mId, "shown");
    }

    TEST(MWAccessibilityLuaStats, PromotedGroupsKeepWrapperAndChildPlacementOrder)
    {
        auto first = section("first", "First", { line("x", "X", "1") });
        auto second = section("second", "Second", { line("y", "Y", "2") });
        second.mPlacement.mType = LuaPlacementType::Before;
        second.mPlacement.mTarget = "first";
        auto root = section("root", "", {});
        root.mPlacement.mType = LuaPlacementType::Top;
        root.mSections = { first, second };
        LuaStatsTree tree;
        tree.mBoxes = { box("b", { section("sibling", "Sibling", { line("z", "Z", "3") }), root }) };
        const auto options = buildOptions(tree, labelFor);
        ASSERT_EQ(options.size(), 3);
        EXPECT_EQ(options[0].mId, "second");
        EXPECT_EQ(options[1].mId, "first");
        EXPECT_EQ(options[2].mId, "sibling");
    }

    TEST(MWAccessibilityLuaStats, AHeaderFromTheModAlwaysWinsOverTheLabeller)
    {
        // The mod's own header is the authored name; the labeller only fills
        // gaps. Overriding it would rename another mod's content.
        LuaStatsTree tree;
        tree.mBoxes
            = { box("b", { section("attributes", "Custom Heading", { line("strength", "Strength", "40") }) }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mLabel, "Custom Heading");
    }

    TEST(MWAccessibilityLuaStats, WorksWithNoLabellerAtAll)
    {
        LuaStatsTree tree;
        tree.mBoxes = { box("b", { section("attributes", "", { line("strength", "Strength", "40") }) }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree);

        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mLabel, "attributes");
    }

    TEST(MWAccessibilityLuaStats, TopLevelRowsCarryNoRedundantSectionPrefix)
    {
        // The submenu's own name already says which group these rows are in,
        // so repeating it on every row would be paid on every keypress.
        const std::vector<LuaStatsOption> options = buildOptions(defaultWindow(), labelFor);

        ASSERT_FALSE(options.empty());
        for (const LuaStatsItem& item : options[0].mChildren)
            EXPECT_TRUE(item.mSection.empty());
    }

    TEST(MWAccessibilityLuaStats, HigherPriorityIsDrawnAndSpokenFirst)
    {
        // The mod sorts priority DESCENDING, so the API's insertion order is
        // not the display order. Speech must follow the screen.
        std::vector<std::string> ids = { "low", "high", "middle" };
        std::vector<LuaPlacement> placements(3);
        placements[0].mPriority = 10;
        placements[1].mPriority = 300;
        placements[2].mPriority = 100;

        const std::vector<std::size_t> order = orderByPlacement(ids, placements);

        ASSERT_EQ(order.size(), 3);
        EXPECT_EQ(ids[order[0]], "high");
        EXPECT_EQ(ids[order[1]], "middle");
        EXPECT_EQ(ids[order[2]], "low");
    }

    TEST(MWAccessibilityLuaStats, EqualPrioritiesKeepArrivalOrder)
    {
        // The mod breaks ties on its recorded originalIndex, so the sort must
        // be stable -- otherwise two dependent mods at the default priority
        // would swap unpredictably between reads.
        std::vector<std::string> ids = { "a", "b", "c", "d" };
        const std::vector<LuaPlacement> placements(4);

        const std::vector<std::size_t> order = orderByPlacement(ids, placements);

        ASSERT_EQ(order.size(), 4);
        EXPECT_EQ(ids[order[0]], "a");
        EXPECT_EQ(ids[order[3]], "d");
    }

    TEST(MWAccessibilityLuaStats, TopPlacementJumpsToTheFront)
    {
        std::vector<std::string> ids = { "first", "wantsTop" };
        std::vector<LuaPlacement> placements(2);
        placements[1].mType = LuaPlacementType::Top;

        const std::vector<std::size_t> order = orderByPlacement(ids, placements);

        ASSERT_EQ(order.size(), 2);
        EXPECT_EQ(ids[order[0]], "wantsTop");
        EXPECT_EQ(ids[order[1]], "first");
    }

    TEST(MWAccessibilityLuaStats, AfterAndBeforePlaceRelativeToTheirTarget)
    {
        // How a dependent mod slots a row next to an existing one.
        std::vector<std::string> ids = { "health", "magicka", "afterHealth", "beforeMagicka" };
        std::vector<LuaPlacement> placements(4);
        placements[2].mType = LuaPlacementType::After;
        placements[2].mTarget = "health";
        placements[3].mType = LuaPlacementType::Before;
        placements[3].mTarget = "magicka";

        const std::vector<std::size_t> order = orderByPlacement(ids, placements);

        ASSERT_EQ(order.size(), 4);
        EXPECT_EQ(ids[order[0]], "health");
        EXPECT_EQ(ids[order[1]], "afterHealth");
        EXPECT_EQ(ids[order[2]], "beforeMagicka");
        EXPECT_EQ(ids[order[3]], "magicka");
    }

    TEST(MWAccessibilityLuaStats, AnUnresolvedTargetFallsBackToTheEnd)
    {
        // The mod looks the target up only among entries already placed, so a
        // reference to something absent appends rather than failing.
        // Reproducing that keeps speech and screen together even when a mod's
        // placement is unsatisfiable.
        std::vector<std::string> ids = { "real", "danglingRef" };
        std::vector<LuaPlacement> placements(2);
        placements[1].mType = LuaPlacementType::After;
        placements[1].mTarget = "doesNotExist";

        const std::vector<std::size_t> order = orderByPlacement(ids, placements);

        ASSERT_EQ(order.size(), 2);
        EXPECT_EQ(ids[order[0]], "real");
        EXPECT_EQ(ids[order[1]], "danglingRef");
    }

    TEST(MWAccessibilityLuaStats, PlacementReordersSubmenusThemselves)
    {
        LuaStatsTree tree;
        LuaStatsSection first = section("first", "First", { line("a", "A", "1") });
        LuaStatsSection promoted = section("promoted", "Promoted", { line("b", "B", "2") });
        promoted.mPlacement.mPriority = 500;
        tree.mBoxes = { box("b", { first, promoted }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 2);
        EXPECT_EQ(options[0].mLabel, "Promoted");
    }

    TEST(MWAccessibilityLuaStats, PlacementAppliesToRowsInsideASubmenu)
    {
        LuaStatsTree tree;
        LuaStatsLine first = line("alchemy", "Alchemy", "22");
        LuaStatsLine promoted = line("alteration", "Alteration", "37");
        promoted.mPlacement.mPriority = 500;
        tree.mBoxes = { box("b", { section("majorSkills", "Major Skills", { first, promoted }) }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 1);
        ASSERT_EQ(options[0].mChildren.size(), 2);
        EXPECT_EQ(options[0].mChildren[0].mText, "Alteration 37");
    }

    TEST(MWAccessibilityLuaStats, ASectionCanAlphabetiseItsLines)
    {
        LuaStatsTree tree;
        LuaStatsSection s = section("majorSkills", "Major Skills",
            { line("c", "Restoration", "3"), line("a", "Alchemy", "1"), line("b", "Destruction", "2") });
        s.mSort = LuaLineSort::LabelAscending;
        tree.mBoxes = { box("b", { s }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options[0].mChildren.size(), 3);
        EXPECT_EQ(options[0].mChildren[0].mText, "Alchemy 1");
        EXPECT_EQ(options[0].mChildren[2].mText, "Restoration 3");
    }

    TEST(MWAccessibilityLuaStats, AHiddenRowIsNeverSpoken)
    {
        // The mod's visibleFn hides rows whose data is absent (Bounty at zero).
        // A hidden row is not on screen, so announcing it would report state
        // the player does not have.
        LuaStatsTree tree;
        LuaStatsLine hidden = line("bounty", "Bounty", "0");
        hidden.mVisible = false;
        tree.mBoxes = { box("b", { section("s", "Group", { line("shown", "Alchemy", "22"), hidden }) }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 1);
        ASSERT_EQ(options[0].mChildren.size(), 1);
        EXPECT_EQ(options[0].mChildren[0].mText, "Alchemy 22");
    }

    TEST(MWAccessibilityLuaStats, ASectionWhoseEveryRowIsHiddenIsDropped)
    {
        LuaStatsTree tree;
        LuaStatsLine hidden = line("bounty", "Bounty", "0");
        hidden.mVisible = false;
        tree.mBoxes = { box("b",
            { section("bounty", "Bounty", { hidden }), section("real", "Real", { line("x", "Row", "1") }) }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mLabel, "Real");
    }

    TEST(MWAccessibilityLuaStats, AHiddenSectionIsDroppedEvenWhenItsRowsAreVisible)
    {
        // visibleFn exists at SECTION level too: the Sign section hides itself
        // for a character with no birthsign, while the rows it holds know
        // nothing about that.
        LuaStatsTree tree;
        LuaStatsSection hidden = section("birthSign", "Sign", { line("elfborn", "The Apprentice") });
        hidden.mVisible = false;
        tree.mBoxes = { box("b", { hidden, section("real", "Real", { line("x", "Row", "1") }) }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 1);
        EXPECT_EQ(options[0].mLabel, "Real");
    }

    TEST(MWAccessibilityLuaStats, ARowWithNoValueReadsAsItsLabelAlone)
    {
        // Birthsign abilities are label-and-tooltip only.
        LuaStatsTree tree;
        LuaStatsLine ability = line("elfborn", "The Apprentice");
        ability.mTooltips = { "Fortify Magicka" };
        tree.mBoxes = { box("b", { section("birthSign", "Sign", { ability }) }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 1);
        ASSERT_EQ(options[0].mChildren.size(), 1);
        EXPECT_EQ(options[0].mChildren[0].mText, "The Apprentice");
        ASSERT_EQ(options[0].mChildren[0].mTooltips.size(), 1);
    }

    TEST(MWAccessibilityLuaStats, RowsCarryTheirIdAndTooltips)
    {
        // The id identifies a row for a later re-read, and the tooltips are the
        // only way a keyboard user reaches them at all.
        LuaStatsTree tree;
        LuaStatsLine l = line("alteration", "Alteration", "37");
        l.mTooltips = { "Alteration", "Governing Attribute: Willpower" };
        tree.mBoxes = { box("b", { section("majorSkills", "Major Skills", { l }) }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options[0].mChildren.size(), 1);
        EXPECT_EQ(options[0].mChildren[0].mId, "alteration");
        ASSERT_EQ(options[0].mChildren[0].mTooltips.size(), 2);
        EXPECT_EQ(options[0].mChildren[0].mTooltips[1], "Governing Attribute: Willpower");
    }

    TEST(MWAccessibilityLuaStats, NestedSectionsReadBeforeTheirParentsRows)
    {
        // The mod renders subsections above the parent's own lines, so
        // flattening must follow -- otherwise speech order diverges from the
        // screen for any mod that nests.
        LuaStatsTree tree;
        LuaStatsSection parent = section("faction", "Factions", { line("ownRow", "Reputation", "5") });
        parent.mSections = { section("blades", "Blades", { line("rank", "Rank", "Protector") }) };
        tree.mBoxes = { box("b", { parent }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 1);
        ASSERT_EQ(options[0].mChildren.size(), 2);
        EXPECT_EQ(options[0].mChildren[0].mText, "Rank Protector");
        EXPECT_EQ(options[0].mChildren[0].mSection, "Blades");
        EXPECT_EQ(options[0].mChildren[1].mText, "Reputation 5");
    }

    TEST(MWAccessibilityLuaStats, ANestedSectionWithoutAHeadingInheritsItsParents)
    {
        LuaStatsTree tree;
        LuaStatsSection parent = section("reputation", "Reputation", {});
        parent.mSections = { section("child", "", { line("row", "Morrowind", "5") }) };
        tree.mBoxes = { box("b", { parent }) };

        const std::vector<LuaStatsOption> options = buildOptions(tree, labelFor);

        ASSERT_EQ(options.size(), 1);
        ASSERT_EQ(options[0].mChildren.size(), 1);
        // The top level of a submenu carries no prefix, so an unheaded child of
        // the root inherits nothing rather than repeating the submenu's name.
        EXPECT_TRUE(options[0].mChildren[0].mSection.empty());
    }

    TEST(MWAccessibilityLuaStats, JoinsAValueNodeToTheCaptionAboveIt)
    {
        // The exact shape the level tooltip builds: a heading, a padding node,
        // then the progress bar -- which the mod names "value" and whose own
        // text is "current/maximum". They are VERTICAL siblings, so only the
        // name reveals that the two halves are one item. Reported from play as
        // "Progress toward next level" and "10/10" arriving as two tooltips.
        LuaLayoutNode bar = textNode("10/10");
        bar.mIsValue = true;

        const LuaLayoutNode root
            = parentNode({ textNode("Progress toward next level"), LuaLayoutNode{}, std::move(bar) });

        const std::vector<std::string> lines = flattenTooltip(root);

        ASSERT_EQ(lines.size(), 1);
        EXPECT_EQ(lines[0], "Progress toward next level 10/10");
    }

    TEST(MWAccessibilityLuaStats, JoinsAValueNodeWrappedInAContainer)
    {
        // The real progress bar is a bordered CONTAINER named "value" whose text
        // lives on a child, so the join has to flatten the whole subtree rather
        // than read the named node's own text.
        LuaLayoutNode bar = parentNode({ LuaLayoutNode{}, textNode("42/60") });
        bar.mIsValue = true;

        const LuaLayoutNode root = parentNode({ textNode("Health"), std::move(bar) });

        const std::vector<std::string> lines = flattenTooltip(root);

        ASSERT_EQ(lines.size(), 1);
        EXPECT_EQ(lines[0], "Health 42/60");
    }

    TEST(MWAccessibilityLuaStats, JoinsSiblingTextInsideAHorizontalRow)
    {
        // A labelled value laid out side by side: one horizontal Flex holding a
        // "label" text node and a "value" text node. Visually one line.
        LuaLayoutNode row = parentNode({ textNode("Reputation"), textNode("5") });
        row.mHorizontal = true;

        const std::vector<std::string> lines = flattenTooltip(parentNode({ std::move(row) }));

        ASSERT_EQ(lines.size(), 1);
        EXPECT_EQ(lines[0], "Reputation 5");
    }

    TEST(MWAccessibilityLuaStats, AValueNodeWithNoCaptionStillSpeaks)
    {
        // A value with nothing above it must not be swallowed.
        LuaLayoutNode bar = textNode("3/5");
        bar.mIsValue = true;

        const std::vector<std::string> lines = flattenTooltip(parentNode({ std::move(bar) }));

        ASSERT_EQ(lines.size(), 1);
        EXPECT_EQ(lines[0], "3/5");
    }

    TEST(MWAccessibilityLuaStats, FlattensATooltipLayoutIntoParagraphs)
    {
        const LuaLayoutNode root = parentNode({ parentNode({ textNode("Alteration"), LuaLayoutNode{},
            textNode("The school of Alteration involves manipulation of the physical world."),
            textNode("Governing Attribute: Willpower") }) });

        const std::vector<std::string> lines = flattenTooltip(root);

        ASSERT_EQ(lines.size(), 3);
        EXPECT_EQ(lines[0], "Alteration");
        EXPECT_EQ(lines[2], "Governing Attribute: Willpower");
    }

    TEST(MWAccessibilityLuaStats, TooltipFlatteningSkipsBlankAndWhitespaceNodes)
    {
        // Most nodes in a layout are padding and framing. A whitespace-only
        // node would otherwise become a paragraph that reads as silence, and
        // the user would have to press T again to get past it.
        const LuaLayoutNode root
            = parentNode({ textNode("Title"), textNode(""), textNode("   "), textNode("\n\t"), textNode("Body") });

        const std::vector<std::string> lines = flattenTooltip(root);

        ASSERT_EQ(lines.size(), 2);
        EXPECT_EQ(lines[0], "Title");
        EXPECT_EQ(lines[1], "Body");
    }

    TEST(MWAccessibilityLuaStats, TooltipFlatteningReadsDeeplyNestedText)
    {
        const LuaLayoutNode root
            = parentNode({ parentNode({ parentNode({ parentNode({ textNode("Deep detail") }) }) }) });

        const std::vector<std::string> lines = flattenTooltip(root);

        ASSERT_EQ(lines.size(), 1);
        EXPECT_EQ(lines[0], "Deep detail");
    }

    TEST(MWAccessibilityLuaStats, AnEmptyTooltipYieldsNoParagraphs)
    {
        EXPECT_TRUE(flattenTooltip(LuaLayoutNode{}).empty());
        EXPECT_TRUE(flattenTooltip(parentNode({ LuaLayoutNode{}, LuaLayoutNode{} })).empty());
    }

    TEST(MWAccessibilityLuaStats, AnEmptyTreeYieldsNoOptions)
    {
        // The mod can be installed but showing nothing (early in a session,
        // before its builders have run). The pane simply has no options yet.
        EXPECT_TRUE(buildOptions(LuaStatsTree{}, labelFor).empty());
    }
}
