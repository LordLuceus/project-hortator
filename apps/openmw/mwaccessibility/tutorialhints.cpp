#include "tutorialhints.hpp"

#include <string>
#include <string_view>
#include <vector>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

namespace MWAccessibility
{
    namespace
    {
        // Ganciele Douar, the guard on the hall door in the Seyda Neen Census
        // and Excise Office, plays this once the player has taken their release
        // papers and comes within 180 units of him (vanilla script
        // CharGenDoorGuardTalker). He says: "Continue through to the next
        // building, and talk to Sellus Gravius."
        //
        // VFS::Path::Normalized lower-cases and turns every backslash into a
        // forward slash (see components/vfs/pathutil.hpp), so the ESM's
        // "vo\Misc\CharGen Door2.wav" is compared in this form.
        constexpr std::string_view sCharGenDoorGuardVoice = "vo/misc/chargen door2.wav";
    }

    void onScriptedSay(const VFS::Path::Normalized& voiceFile)
    {
        // BY FAR the most common piece of player feedback on the mod: people get
        // stuck here, in the first few minutes of a new game.
        //
        // The whole Census and Excise Office is ONE cell, but it is two separate
        // buildings joined by an outdoor courtyard. Sellus Gravius therefore sits
        // in the scanner's list from the moment the player walks into the first
        // building -- roughly 1400 units away, through a wall -- so when the game
        // tells them to go and see him, the obvious move is to walk at him. That
        // cannot work, and there is no way to tell by ear that it cannot work.
        //
        // Vanilla DOES cue this, out loud, at exactly the right moment; the cue
        // just is not actionable without sight. Four doors are in play and only
        // one leads to the courtyard:
        //
        //   chargen door exit     45 deg NE, 1021 units   <- the correct one
        //   ex_nord_door_01      217 deg SW,  419 units   locked (level 100)
        //   ex_nord_door_01      113 deg SE, 1168 units
        //   CharGen Exit Door    130 deg SE, 1444 units   final exit, ends chargen
        //
        // (Bearings measured from the guard's own position in Morrowind.esm.)
        // So the missing information is precisely one compass direction, and that
        // is all we add. Deliberately NOT mentioned: the scanner, auto-walk, or
        // any other mod feature. This has to read as part of the game's own
        // tutorial, not as a mod talking over it -- the player is three minutes
        // into Morrowind and everything else they have heard so far is Bethesda's.
        //
        // We hang this off the guard's VOICE LINE rather than testing the same
        // conditions ourselves. That inherits Bethesda's trigger exactly (papers
        // taken, player in range, said once), so it cannot fire early, fire twice,
        // or fire in a playthrough where the sequence went differently. It also
        // means a mod that replaces the tutorial silences our hint along with the
        // line it was attached to, which is the correct behaviour.
        if (voiceFile != sCharGenDoorGuardVoice)
            return;

        // Phrased to sit directly after what the guard just said, and to match
        // the register of the vanilla tutorial popups ("Check the barrel to your
        // left, and get the ring inside."): one plain instruction, no jargon, no
        // explanation of why.
        //
        // Presented EXACTLY as the game's own tutorial popups are. Those are
        // script calls of the form MessageBox "..." "Ok", and a MessageBox with
        // buttons routes to interactiveMessageBox (see
        // MWScript::InterpreterContext::messageBox), which pauses the game and
        // waits for the button. The plain messageBox() overload is the transient
        // corner notification used for "you are overloaded" and the like -- it
        // does not pause, and using it here would make this hint the one popup in
        // the sequence that behaves differently. The single "Ok" button is taken
        // from the GMST so it stays localized, exactly as the vanilla popups do.
        //
        // Accessibility comes for free: InteractiveMessageBox already announces
        // its prompt and focused button, and marks the prompt rereadable.
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        const std::vector<std::string> buttons = { std::string(wm->getGameSettingString("sOK", "OK")) };
        wm->interactiveMessageBox(
            "The door to the courtyard is in the northeast corner. Cross the courtyard to reach the next building.",
            buttons);
    }
}
