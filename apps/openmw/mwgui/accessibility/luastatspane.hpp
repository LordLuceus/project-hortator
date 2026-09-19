#ifndef OPENMW_MWGUI_ACCESSIBILITY_LUASTATSPANE_H
#define OPENMW_MWGUI_ACCESSIBILITY_LUASTATSPANE_H

#include <string>

#include <MyGUI_Widget.h>

#include "screen.hpp"

namespace MWGui::A11y
{
    /// Accessible pane for a Lua mod that has replaced the stats window.
    ///
    /// Stats Window Extender (Nexus 57727) calls I.UI.registerWindow('Stats'),
    /// which disables the engine's own StatsWindow: it never opens, so the
    /// accessibility pane built into it never enrols and the character sheet
    /// simply vanishes from the Tab cycle. This restores it by reading the
    /// mod's published data model instead of the vanilla widgets.
    ///
    /// The mod is not required: when it is absent this pane never enrols and
    /// the vanilla StatsWindow behaves exactly as before.
    ///
    /// Structure: one submenu per section the mod defines, which is where the
    /// mod puts its display names. Its boxes are anonymous layout containers
    /// and contribute only ordering.
    class LuaStatsPane
    {
    public:
        /// Called every frame while a Lua mod owns the stats window. Enrols the
        /// pane when the mod is present, keeps its contents current, and
        /// withdraws it if the mod goes away.
        void onFrame(float dt);

        /// Withdraw from the pane group and stop taking input.
        void close();

        /// True while this pane is standing in for the vanilla stats window.
        bool enrolled() const { return mEnrolled; }

    private:
        /// Rebuild the option list from the mod's current model, preserving the
        /// user's place in the list where possible. Returns true if the list
        /// actually changed; a no-op when the structure is unchanged.
        bool rebuild();

        /// Name a section the mod left unheaded, from the vanilla GMSTs, so the
        /// wording matches the window the player already knows.
        static std::string labelForSection(const std::string& sectionId);

        Screen mA11y;
        bool mEnrolled = false;

        /// Invisible widget that holds real key focus, so arrows and Enter
        /// reach us while the options themselves have no widgets of their own.
        /// Created on the host window, which is hidden but still alive.
        MyGUI::Widget* mAnchor = nullptr;

        /// Signature of the structure last built, used to detect that the mod
        /// has changed its window (a faction joined, a bounty incurred) without
        /// rebuilding the option list every frame.
        std::string mSignature;

        /// Seconds since the model was last checked for structural changes.
        float mSinceCheck = 0.f;

        /// How often to re-check. Reading the model calls into the mod's Lua,
        /// and a new section appearing a fraction of a second late is
        /// imperceptible.
        static constexpr float sCheckInterval = 0.5f;
    };
}

#endif
