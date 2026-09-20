#ifndef OPENMW_MWACCESSIBILITY_STATDAMAGE_H
#define OPENMW_MWACCESSIBILITY_STATDAMAGE_H

#include <string>
#include <string_view>

#include <components/esm/refid.hpp>

namespace MWMechanics
{
    class MagicEffects;
    class NpcStats;
}

namespace MWWorld
{
    class ESMStore;
}

namespace MWAccessibility
{
    // Shared by the vanilla and replacement stats panes. Drain and Absorb
    // contribute to the same damage counter but are temporary, so exclude them.
    std::string statDamageSuffix(float damage, const MWMechanics::MagicEffects& effects, ESM::RefId drainEffect,
        ESM::RefId absorbEffect, ESM::RefId statId);

    // SWE uses the stat record id as the line id. Resolve against the loaded
    // records, never the translated label, section heading or value colour.
    // Unrelated lines (including custom mod rows) receive no suffix.
    std::string luaStatDamageSuffix(
        std::string_view lineId, const MWWorld::ESMStore& store, const MWMechanics::NpcStats& stats);
}

#endif
