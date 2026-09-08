#ifndef OPENMW_MWACCESSIBILITY_TUTORIALHINTS_H
#define OPENMW_MWACCESSIBILITY_TUTORIALHINTS_H

#include <components/vfs/pathutil.hpp>

namespace MWAccessibility
{
    /// Called whenever a script runs the Say instruction, with the voice file
    /// that is about to play.
    ///
    /// A very small number of vanilla tutorial lines give directions that are
    /// unusable without sight ("continue through to the next building" -- which
    /// of the four doors?). Where that happens, we follow the line with a
    /// message box in the same style as the game's own tutorial popups, adding
    /// only the missing detail.
    ///
    /// Hanging this off the voice line rather than off our own trigger means we
    /// inherit Bethesda's exact conditions for free, and can never fire at a
    /// moment the tutorial did not intend.
    void onScriptedSay(const VFS::Path::Normalized& voiceFile);
}

#endif
