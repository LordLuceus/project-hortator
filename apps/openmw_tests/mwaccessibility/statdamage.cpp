#include <gtest/gtest.h>

#include <memory>

#include <components/esm/attr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>

#include <apps/openmw/mwaccessibility/statdamage.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwmechanics/magiceffects.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

namespace
{
    using MWAccessibility::luaStatDamageSuffix;
    using MWAccessibility::statDamageSuffix;

    struct DamageKind
    {
        ESM::RefId mStat;
        ESM::RefId mDrain;
        ESM::RefId mAbsorb;
    };

    class MWAccessibilityStatDamage : public testing::TestWithParam<DamageKind>
    {
    protected:
        MWMechanics::MagicEffects mEffects;

        std::string suffix(float damage)
        {
            const auto& kind = GetParam();
            return statDamageSuffix(damage, mEffects, kind.mDrain, kind.mAbsorb, kind.mStat);
        }

        void temporaryDamage(float drain, float absorb)
        {
            const auto& kind = GetParam();
            mEffects.add({ kind.mDrain, kind.mStat }, MWMechanics::EffectParam(drain));
            mEffects.add({ kind.mAbsorb, kind.mStat }, MWMechanics::EffectParam(absorb));
        }
    };

    TEST_P(MWAccessibilityStatDamage, AnnouncesPermanentDamage)
    {
        EXPECT_EQ(suffix(6.f), ", damaged");
    }

    TEST_P(MWAccessibilityStatDamage, HealthyStatsStayQuiet)
    {
        EXPECT_EQ(suffix(0.f), "");
    }

    TEST_P(MWAccessibilityStatDamage, DrainAloneStaysQuiet)
    {
        temporaryDamage(6.f, 0.f);
        EXPECT_EQ(suffix(6.f), "");
    }

    TEST_P(MWAccessibilityStatDamage, AbsorbAloneStaysQuiet)
    {
        temporaryDamage(0.f, 6.f);
        EXPECT_EQ(suffix(6.f), "");
    }

    TEST_P(MWAccessibilityStatDamage, SubtractsBothTemporaryEffects)
    {
        temporaryDamage(4.f, 2.f);
        EXPECT_EQ(suffix(6.f), "");
        EXPECT_EQ(suffix(9.f), ", damaged");
    }

    TEST_P(MWAccessibilityStatDamage, PreservesVanillaRoundingTolerance)
    {
        temporaryDamage(4.f, 2.f);
        EXPECT_EQ(suffix(5.f), "");
        EXPECT_EQ(suffix(6.00001f), "");
        EXPECT_EQ(suffix(6.5f), "");
        EXPECT_EQ(suffix(6.5001f), ", damaged");
    }

    TEST_P(MWAccessibilityStatDamage, EffectsOnAnotherStatDoNotMaskDamage)
    {
        const auto& kind = GetParam();
        const ESM::RefId other = ESM::RefId::stringRefId("anotherStat");
        mEffects.add({ kind.mDrain, other }, MWMechanics::EffectParam(30.f));
        mEffects.add({ kind.mAbsorb, other }, MWMechanics::EffectParam(30.f));
        EXPECT_EQ(suffix(6.f), ", damaged");
    }

    INSTANTIATE_TEST_SUITE_P(AttributesAndSkills, MWAccessibilityStatDamage,
        testing::Values(
            DamageKind{ ESM::Attribute::Strength, ESM::MagicEffect::DrainAttribute, ESM::MagicEffect::AbsorbAttribute },
            DamageKind{ ESM::Skill::LongBlade, ESM::MagicEffect::DrainSkill, ESM::MagicEffect::AbsorbSkill }));

    class MWAccessibilityLuaStatDamage : public testing::Test
    {
    protected:
        MWBase::Environment mEnvironment;
        MWWorld::ESMStore mStore;
        std::unique_ptr<MWMechanics::NpcStats> mStats;

        void SetUp() override
        {
            mEnvironment.setESMStore(mStore);
            // Seed vanilla ids without loading a game or translated display names.
            for (int i = 0; i < ESM::Attribute::Length; ++i)
            {
                ESM::Attribute attribute;
                attribute.mId = *ESM::Attribute::indexToRefId(i).getIf<ESM::StringRefId>();
                mStore.insertStatic(attribute);
            }
            ESM::Skill skill{};
            skill.mId = ESM::Skill::LongBlade;
            mStore.insertStatic(skill);
            mStats = std::make_unique<MWMechanics::NpcStats>();
        }

        void damageStrength(float damage, float fortify = 0.f)
        {
            MWMechanics::AttributeValue value;
            value.setBase(40.f);
            value.setModifier(fortify);
            value.damage(damage);
            mStats->setAttribute(ESM::Attribute::Strength, value);
        }
    };

    TEST_F(MWAccessibilityLuaStatDamage, RecognizesExtenderAttributeId)
    {
        damageStrength(6.f);
        EXPECT_EQ(luaStatDamageSuffix("strength", mStore, *mStats), ", damaged");
        EXPECT_EQ(luaStatDamageSuffix("speed", mStore, *mStats), "");
    }

    TEST_F(MWAccessibilityLuaStatDamage, RecognizesExtenderSkillId)
    {
        auto& skill = mStats->getSkill(ESM::Skill::LongBlade);
        skill.setBase(40.f);
        skill.damage(6.f);
        // Use precisely the id representation published by the Lua stats API.
        const std::string id = ESM::RefId(ESM::Skill::LongBlade).serializeText();
        EXPECT_EQ(luaStatDamageSuffix(id, mStore, *mStats), ", damaged");
        mStats->getMagicEffects().add(
            { ESM::MagicEffect::DrainSkill, ESM::Skill::LongBlade }, MWMechanics::EffectParam(6.f));
        EXPECT_EQ(luaStatDamageSuffix(id, mStore, *mStats), "");
    }

    TEST_F(MWAccessibilityLuaStatDamage, AttributeDrainAndAbsorbAreNotPermanent)
    {
        damageStrength(6.f);
        mStats->getMagicEffects().add(
            { ESM::MagicEffect::DrainAttribute, ESM::Attribute::Strength }, MWMechanics::EffectParam(4.f));
        mStats->getMagicEffects().add(
            { ESM::MagicEffect::AbsorbAttribute, ESM::Attribute::Strength }, MWMechanics::EffectParam(2.f));
        EXPECT_EQ(luaStatDamageSuffix("strength", mStore, *mStats), "");
        damageStrength(9.f);
        EXPECT_EQ(luaStatDamageSuffix("strength", mStore, *mStats), ", damaged");
    }

    TEST_F(MWAccessibilityLuaStatDamage, FortifyDoesNotHidePermanentDamage)
    {
        damageStrength(6.f, 20.f);
        EXPECT_GT(mStats->getAttribute(ESM::Attribute::Strength).getModified(), 40.f);
        EXPECT_EQ(luaStatDamageSuffix("strength", mStore, *mStats), ", damaged");
    }

    TEST_F(MWAccessibilityLuaStatDamage, RestoreRemovesWarningOnNextRead)
    {
        damageStrength(6.f);
        EXPECT_EQ(luaStatDamageSuffix("strength", mStore, *mStats), ", damaged");
        auto value = mStats->getAttribute(ESM::Attribute::Strength);
        value.restore(6.f);
        mStats->setAttribute(ESM::Attribute::Strength, value);
        EXPECT_EQ(luaStatDamageSuffix("strength", mStore, *mStats), "");
    }

    TEST_F(MWAccessibilityLuaStatDamage, UnrelatedRowsAreNotStats)
    {
        damageStrength(6.f);
        for (const char* id : { "", "health", "level", "reputation", "factionRank", "customStrengthBonus" })
            EXPECT_EQ(luaStatDamageSuffix(id, mStore, *mStats), "") << id;
    }
}
