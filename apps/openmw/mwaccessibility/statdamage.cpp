#include "statdamage.hpp"

#include <components/esm/attr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>

#include "../mwmechanics/magiceffects.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwworld/esmstore.hpp"

namespace MWAccessibility
{
    std::string statDamageSuffix(float damage, const MWMechanics::MagicEffects& effects, ESM::RefId drainEffect,
        ESM::RefId absorbEffect, ESM::RefId statId)
    {
        if (damage <= 0.f)
            return {};
        const float drain = effects.getOrDefault(MWMechanics::EffectKey(drainEffect, statId)).getMagnitude();
        const float absorb = effects.getOrDefault(MWMechanics::EffectKey(absorbEffect, statId)).getMagnitude();
        // Keep vanilla's rounding tolerance and wording, including damage
        // hidden by Fortify. A red number alone cannot identify permanent damage.
        return damage - drain - absorb > 0.5f ? ", damaged" : std::string();
    }

    std::string luaStatDamageSuffix(
        std::string_view lineId, const MWWorld::ESMStore& store, const MWMechanics::NpcStats& stats)
    {
        const ESM::RefId id = ESM::RefId::stringRefId(lineId);
        if (store.get<ESM::Attribute>().search(id))
            return statDamageSuffix(stats.getAttribute(id).getDamage(), stats.getMagicEffects(),
                ESM::MagicEffect::DrainAttribute, ESM::MagicEffect::AbsorbAttribute, id);
        if (store.get<ESM::Skill>().search(id))
            return statDamageSuffix(stats.getSkill(id).getDamage(), stats.getMagicEffects(),
                ESM::MagicEffect::DrainSkill, ESM::MagicEffect::AbsorbSkill, id);
        return {};
    }
}
