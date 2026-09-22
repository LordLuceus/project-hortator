#include "luastatspane.hpp"

#include <MyGUI_Gui.h>
#include <MyGUI_InputManager.h>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/windowmanager.hpp"

#include "../../mwmechanics/actorutil.hpp"
#include "../../mwmechanics/npcstats.hpp"

#include "../../mwworld/class.hpp"
#include "../../mwworld/esmstore.hpp"

#include "../../mwaccessibility/luastatsreader.hpp"
#include "../../mwaccessibility/luastatsspeech.hpp"
#include "../../mwaccessibility/statdamage.hpp"

#include "element.hpp"
#include "panegroup.hpp"

namespace MWGui::A11y
{
    namespace
    {
        // Name the sections the mod declares with no header at all
        // (healthStats, levelStats, attributes). No display name for these
        // exists anywhere in the mod -- not as a header, an l10n entry, or a
        // box title -- so the engine supplies localized labels. Level and
        // Attributes reuse vanilla GMSTs; the combined health/magicka/fatigue
        // group uses Vitals rather than the entire window's generic title.
        struct SectionLabel
        {
            std::string_view mSectionId;
            std::string_view mGameSetting;
            std::string_view mFallback;
        };

        constexpr SectionLabel sSectionLabels[] = {
            // Holds level, race and class.
            { "levelStats", "sLevel", "Level" },
            { "attributes", "sAttributes", "Attributes" },
        };

        std::string damageSuffix(const std::string& lineId)
        {
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            return MWAccessibility::luaStatDamageSuffix(
                lineId, *MWBase::Environment::get().getESMStore(), player.getClass().getNpcStats(player));
        }
    }

    std::string LuaStatsPane::labelForSection(const std::string& sectionId)
    {
        if (sectionId == "healthStats")
            return "#{Interface:Vitals}";

        for (const SectionLabel& entry : sSectionLabels)
        {
            if (sectionId == entry.mSectionId)
            {
                return std::string(MWBase::Environment::get().getWindowManager()->getGameSettingString(
                    entry.mGameSetting, entry.mFallback));
            }
        }

        return {};
    }

    bool LuaStatsPane::rebuild()
    {
        const std::optional<MWAccessibility::LuaStatsTree> tree = MWAccessibility::LuaStatsReader::read();
        if (!tree)
            return false;

        std::vector<MWAccessibility::LuaStatsOption> options
            = MWAccessibility::buildOptions(*tree, &LuaStatsPane::labelForSection);

        // Rebuild only when the model has actually changed, since rebuilding
        // every frame would rerun the mod's builders continuously for no
        // benefit.
        //
        // The signature covers each row's LABEL as well as the section ids and
        // row counts. Counting rows alone is not enough: the mod's own settings
        // can change what a row says without changing how many there are (its
        // "show faction rank as number or as title" option is exactly that), and
        // such a change would otherwise stay invisible until a save reload.
        // Row VALUES are deliberately excluded -- they are read at speech time,
        // so putting them here would force a pointless rebuild on every tick of
        // a regenerating stat.
        std::string signature;
        for (const MWAccessibility::LuaStatsOption& option : options)
        {
            signature += option.mId;
            signature += '\x1f';
            signature += option.mLabel;
            signature += '\x1f';
            for (const MWAccessibility::LuaStatsItem& row : option.mChildren)
            {
                signature += row.mLabel;
                signature += '\x1d';
                signature += row.mSection;
                signature += '\x1c';
            }
            signature += '\x1e';
        }

        if (signature == mSignature)
            return false;
        mSignature = std::move(signature);

        // Keep the user where they were: a rebuild is triggered by the mod's
        // own data changing, which should not throw away their place in a long
        // list of skills.
        const std::string previousLabel = mA11y.currentLabel();

        mA11y.clear();

        // The player's name, which the mod does not publish at all.
        //
        // Vanilla shows it as the stats window's TITLE rather than as a row,
        // so it is not part of any data model to read: the mod has no name
        // field, no name line, and sets no window caption. It is still the
        // character's identity, and the one thing from the vanilla sheet that
        // would otherwise be missing entirely, so the engine supplies it from
        // the player record. Read at speech time, since a name can change.
        Element nameElement;
        nameElement.widget = nullptr;
        nameElement.label
            = std::string(MWBase::Environment::get().getWindowManager()->getGameSettingString("sName", "Name"));
        nameElement.value = []() -> std::string {
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            return std::string(player.getClass().getName(player));
        };
        mA11y.add(std::move(nameElement));

        for (const MWAccessibility::LuaStatsOption& option : options)
        {
            Element element;
            element.widget = nullptr;
            element.label = option.mLabel;
            // The children callback runs each time the submenu is opened, so
            // every row's value is resolved at speech time rather than when
            // the list was built. A stale number is a confident wrong answer,
            // and speech is the only channel this player has to catch it.
            element.children = [rows = option.mChildren] {
                std::vector<SubItem> items;
                items.reserve(rows.size());
                for (const MWAccessibility::LuaStatsItem& row : rows)
                {
                    SubItem item;
                    // An empty result is "this row has no value", NOT "the read
                    // failed": readLineValue reports a valueless row as an
                    // engaged optional holding an empty string. Treating that
                    // as a successful read would join the label with nothing
                    // and silently discard the captured text -- which is how
                    // faction rows (whose text lives in their tooltip layout
                    // rather than a value function) lost everything but their
                    // name.
                    const std::optional<std::string> live = MWAccessibility::LuaStatsReader::readLineValue(row.mId);
                    item.label
                        = (live && !live->empty()) ? MWAccessibility::joinLabelValue(row.mLabel, *live) : row.mText;
                    item.label += damageSuffix(row.mId);
                    item.section = row.mSection;
                    item.tooltips = [id = row.mId, captured = row.mTooltips]() -> std::vector<std::string> {
                        // Resolved on demand: the mod's tooltip builders are
                        // arbitrary code, and running one per row per rebuild
                        // would cost far more than the few the user asks for.
                        std::vector<std::string> lines = MWAccessibility::LuaStatsReader::readLineTooltip(id);
                        if (lines.empty())
                            lines = captured;
                        // Match vanilla's detail readout as well as the row.
                        // Re-evaluate on demand so Restore removes the warning.
                        if (!lines.empty())
                            lines.front() += damageSuffix(id);
                        return lines;
                    };
                    items.push_back(std::move(item));
                }
                return items;
            };

            mA11y.add(std::move(element));
        }

        if (!previousLabel.empty())
            mA11y.selectByLabel(previousLabel, false);

        return true;
    }

    void LuaStatsPane::onFrame(float dt)
    {
        const bool available = MWAccessibility::LuaStatsReader::available();

        if (!available)
        {
            // The mod was disabled or unloaded mid-session; leave the Tab cycle
            // rather than offering a pane that can no longer be read.
            if (mEnrolled)
                close();
            return;
        }

        if (!mEnrolled)
        {
            // The options have no widgets of their own, so an invisible anchor
            // holds real key focus while the pane navigates them internally
            // (virtual focus).
            //
            // Unlike every other pane, this one CANNOT hang its anchor off the
            // window it belongs to: a Lua mod has taken that window over, and a
            // disabled window is never made visible (WindowBase::setVisible
            // forces it false). MyGUI will not give key focus to a widget
            // inside a hidden parent, so an anchor parented there can never
            // receive a keystroke. Create it at the GUI root instead, where it
            // is always focusable, and keep it 1x1 and empty so it is invisible
            // in practice.
            if (!mAnchor)
            {
                mAnchor = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>(
                    {}, 0, 0, 1, 1, MyGUI::Align::Default, "Overlay");
                mAnchor->setNeedKeyFocus(true);
                mA11y.setVirtualFocus(mAnchor);
            }

            // close() hides it so it cannot hold key focus while we are out of
            // the Tab cycle; MyGUI will not focus a widget that is not visible.
            mAnchor->setVisible(true);

            rebuild();

            // Stand in for the vanilla stats window: same pane name and the
            // same position in the Tab cycle, so the character sheet is where
            // the player expects regardless of which mod is providing it.
            PaneGroup::instance().enrol(&mA11y,
                std::string(MWBase::Environment::get().getWindowManager()->getGameSettingString("sStats", "Stats")), 0);
            mEnrolled = true;

            // Take the initial focus away from whichever pane grabbed it while
            // we were still absent.
            //
            // Vanilla enrols from onOpen(), before any pane's first onFrame.
            // We can only enrol from onFrame, and this window is ticked LAST
            // of the inventory-mode windows (windowmanagerimp.cpp L359), so
            // Inventory has already run maybeActivateInitial and claimed focus
            // by the time we get here on the first open. maybeActivateInitial
            // then refuses to act, because a pane is already active -- which
            // is exactly what left the player on Inventory.
            //
            // So on the frame we enrol, hand focus over explicitly.
            //
            // This must happen on EVERY enrol, not just the first of a session.
            // Each time the menu is reopened the panes re-enrol from scratch and
            // this window is ticked last, so Inventory claims focus first every
            // single time -- the race is not a start-up artefact. claimInitial
            // is itself a no-op unless we are the lowest-order pane and nothing
            // has been navigated yet, so the user's own choice of pane is still
            // respected while the menu stays open.
            PaneGroup::instance().claimInitial(&mA11y);
        }

        else
        {
            // The mod builds its content lazily and changes it during play, so
            // the option list has to be re-checked. Throttled rather than run
            // every frame: reading the model calls into the mod's Lua, and a
            // new faction appearing a fraction of a second later is
            // imperceptible.
            mSinceCheck += dt;
            if (mSinceCheck >= sCheckInterval)
            {
                mSinceCheck = 0.f;

                // Don't rebuild under the user's fingers: replacing the list
                // while a submenu is open would drop them back out of it
                // mid-read. The next check picks the change up.
                if (!mA11y.submenuOpen())
                    rebuild();
            }
        }

        // Only offer to claim focus while actually enrolled, matching both
        // vanilla call sites. Without this guard the pane keeps competing for
        // focus after the user has Tabbed away: PaneGroup::cycle() suspends us
        // (which drops our anchor's key delegate) and resumes another pane,
        // but this window goes on ticking, so we would re-activate ourselves
        // behind the group's back -- leaving our anchor holding real key focus
        // with no delegate bound to it, i.e. every key silently dead.
        if (PaneGroup::instance().contains(&mA11y))
            PaneGroup::instance().maybeActivateInitial(&mA11y);

        mA11y.onFrame(dt);
    }

    void LuaStatsPane::close()
    {
        if (!mEnrolled)
            return;

        PaneGroup::instance().withdraw(&mA11y);
        mA11y.deactivate();
        mA11y.clear();
        mEnrolled = false;

        // Hide the anchor, and drop key focus if it still holds it.
        //
        // Every other pane's anchor is a child of its own window, so hiding the
        // window hides the anchor and MyGUI releases key focus for free. Ours
        // cannot be: the host window is disabled by Lua and never becomes
        // visible, so a child of it could never receive a keystroke, and the
        // anchor has to live at the GUI root instead.
        //
        // That means nothing hides it for us. Left visible and focused after the
        // menu closes, it swallowed keys for the rest of the session: the
        // scanner's search prompt could not take input and movement keys passed
        // straight through, once the stats pane had been opened even once.
        if (mAnchor)
        {
            if (MyGUI::InputManager::getInstance().getKeyFocusWidget() == mAnchor)
                MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(nullptr);

            mAnchor->setVisible(false);
        }

        // If the inventory mode is gone for good (not merely hidden behind a
        // sub-mode such as reading a book), let the group forget which pane
        // was active so the next fresh open lands on the first pane and
        // announces it, as vanilla does. Mirrors InventoryWindow::onClose.
        if (!MWBase::Environment::get().getWindowManager()->containsMode(GM_Inventory))
            PaneGroup::instance().resetMemory();

        // Force a fresh read on re-enrol: the mod's content may have changed
        // completely while we were out of the Tab cycle.
        mSignature.clear();
        mSinceCheck = 0.f;

        // The anchor widget itself is deliberately kept (merely hidden above):
        // recreating it on every open would leak a widget per cycle.
    }
}
