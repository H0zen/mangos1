/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#include "ProcHandlers.h"

#include "DBCStores.h"
#include "Log.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "Utilities/Util.h"
#include "mai/MaiEngine.h"

#include <cstdint>
#include <ctime>

namespace Combat
{
    namespace Procs
    {
        ProcResult Haste(ProcEvent const& e)
        {
            SpellEntry const* hasteSpell = e.AuraSpell();
            if (!hasteSpell)
            {
                return ProcResult::Failed;
            }

            Unit*         target     = e.target;
            std::int32_t  basePoints = 0;
            std::uint32_t triggered  = 0;

            if (hasteSpell->SpellClassSet == SPELLFAMILY_ROGUE)
            {
                switch (hasteSpell->ID)
                {
                    case 13877:                         // Blade Flurry
                    case 33735:
                        target = e.actor->SelectRandomUnfriendlyTarget(e.target);
                        if (!target)
                        {
                            return ProcResult::Failed;
                        }

                        basePoints = std::int32_t(e.damage);
                        triggered  = 22482;
                        break;
                }
            }

            // Nothing to trigger still counts as handled: the charge drops.
            if (triggered == 0)
            {
                return ProcResult::Ok;
            }

            return e.Trigger(target, triggered,
                             basePoints != 0 ? &basePoints : nullptr,
                             "Procs::Haste");
        }

        ProcResult TriggerDamage(ProcEvent const& e)
        {
            SpellEntry const* spellInfo = e.AuraSpell();

            if (!spellInfo || !e.actor || !e.target)
            {
                return ProcResult::Failed;
            }

            DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST,
                             "Proc: %d damage from spell %u (auratype %u)",
                             e.Amount(), spellInfo->ID,
                             e.aura->GetModifier()->m_auraname);

            SpellNonMeleeDamage damageInfo(
                e.actor, e.target, spellInfo->ID,
                SpellSchoolMask(spellInfo->SchoolMask));

            e.actor->CalculateSpellDamage(&damageInfo, e.Amount(), spellInfo);

            damageInfo.target->CalculateAbsorbResistBlock(
                e.actor, &damageInfo, spellInfo);

            e.actor->DealDamageMods(damageInfo.target, damageInfo.damage,
                                    &damageInfo.absorb);
            e.actor->SendSpellNonMeleeDamageLog(&damageInfo);
            e.actor->DealSpellDamage(&damageInfo, true);

            return ProcResult::Ok;
        }

        ProcResult OverrideClassScript(ProcEvent const& e)
        {
            if (!e.target || !e.target->IsAlive() || !e.aura)
            {
                return ProcResult::Failed;
            }

            const std::int32_t scriptId = e.aura->GetModifier()->m_miscvalue;
            const std::int32_t amount   = e.Amount();

            std::uint32_t triggered = 0;

            switch (scriptId)
            {
                case 836:                               // Improved Blizzard 1
                case 988:                               // Improved Blizzard 2
                case 989:                               // Improved Blizzard 3
                {
                    if (!e.procSpell || e.procSpell->SpellVisualID != 9487)
                    {
                        return ProcResult::Failed;
                    }

                    triggered = scriptId == 836 ? 12484
                              : scriptId == 988 ? 12485
                                                : 12486;
                    break;
                }

                case 4086:                              // Improved Mend Pet 1
                case 4087:                              // Improved Mend Pet 2
                {
                    if (!roll_chance_i(amount))
                    {
                        return ProcResult::Failed;
                    }

                    triggered = 24406;
                    break;
                }

                case 4533:                              // Dreamwalker 2 pieces
                {
                    if (!roll_chance_i(50))
                    {
                        return ProcResult::Failed;
                    }

                    switch (e.target->GetPowerType())
                    {
                        case POWER_MANA:   triggered = 28722; break;
                        case POWER_RAGE:   triggered = 28723; break;
                        case POWER_ENERGY: triggered = 28724; break;
                        default:
                            return ProcResult::Failed;
                    }
                    break;
                }

                case 4537:                              // Dreamwalker 6 pieces
                    triggered = 28750;                  // Blessing of the Claw
                    break;

                case 5497:                              // Improved Mana Gems
                    triggered = 37445;                  // Mana Surge
                    break;
            }

            // An unhandled script id is not a failure; it simply does nothing.
            if (triggered == 0)
            {
                return ProcResult::Ok;
            }

            return e.Trigger(e.target, triggered, nullptr,
                             "Procs::OverrideClassScript");
        }

        ProcResult Mending(ProcEvent const& e)
        {
            SpellEntry const* spellProto = e.AuraSpell();

            if (!spellProto || !e.actor || !e.aura)
            {
                return ProcResult::Failed;
            }

            const SpellEffectIndex effIdx = e.aura->GetEffIndex();

            std::int32_t heal = e.Amount();
            const ObjectGuid casterGuid = e.aura->GetCasterGuid();

            const std::int32_t jumps =
                e.aura->GetHolder()->GetAuraCharges() - 1;

            // The current holder expires at the next charge decrease.
            e.aura->GetHolder()->SetAuraCharges(1);

            if (jumps > 0 && e.ActorPlayer() && casterGuid.IsPlayer())
            {
                float radius = spellProto->EffectRadiusIndex[effIdx]
                    ? GetSpellRadius(sSpellRadiusStore.LookupEntry(
                          spellProto->EffectRadiusIndex[effIdx]))
                    : GetSpellMaxRange(sSpellRangeStore.LookupEntry(
                          spellProto->RangeIndex));

                Unit* casterUnit = e.aura->GetCaster();

                if (casterUnit && casterUnit->GetTypeId() == TYPEID_PLAYER)
                {
                    Player* caster = static_cast<Player*>(casterUnit);

                    caster->ApplySpellMod(spellProto->ID, SPELLMOD_RADIUS,
                                          radius, nullptr);

                    if (Player* next =
                            e.ActorPlayer()->GetNextRandomRaidMember(radius))
                    {
                        // Applied from the caster, cast from this holder.
                        SpellModifier* mod = new SpellModifier(
                            SPELLMOD_CHARGES, SPELLMOD_FLAT, jumps - 5,
                            spellProto->ID, spellProto->SpellClassMask);

                        e.aura->SetInUse(true);
                        e.actor->RemoveAurasByCasterSpell(
                            spellProto->ID, caster->GetObjectGuid());

                        caster->AddSpellMod(mod, true);
                        e.actor->CastCustomSpell(
                            next, spellProto->ID, &heal, nullptr, nullptr,
                            true, nullptr, e.aura, caster->GetObjectGuid());
                        caster->AddSpellMod(mod, false);
                        e.aura->SetInUse(false);
                    }
                }
            }

            e.actor->CastCustomSpell(e.actor, 33110, &heal, nullptr, nullptr,
                                     true, nullptr, nullptr, casterGuid);

            return ProcResult::Ok;
        }

        ProcResult CastingSpeedNotStack(ProcEvent const& e)
        {
            // Melee hits and instant casts do not carry it.
            return (e.procSpell && GetSpellCastTime(e.procSpell) != 0)
                ? ProcResult::Ok
                : ProcResult::Failed;
        }

        ProcResult ReflectSpellsSchool(ProcEvent const& e)
        {
            if (!e.procSpell || !e.aura)
            {
                return ProcResult::Failed;
            }

            return (e.aura->GetModifier()->m_miscvalue &
                    e.procSpell->SchoolMask) != 0
                ? ProcResult::Ok
                : ProcResult::Failed;
        }

        ProcResult PowerCostSchool(ProcEvent const& e)
        {
            if (!e.procSpell || !e.aura)
            {
                return ProcResult::Failed;
            }

            const bool free = e.procSpell->ManaCost == 0 &&
                              e.procSpell->ManaCostPct == 0;

            if (free)
            {
                return ProcResult::Failed;
            }

            return (e.aura->GetModifier()->m_miscvalue &
                    e.procSpell->SchoolMask) != 0
                ? ProcResult::Ok
                : ProcResult::Failed;
        }

        ProcResult MechanicImmunity(ProcEvent const& e)
        {
            if (!e.procSpell || !e.aura)
            {
                return ProcResult::Failed;
            }

            return std::int32_t(e.procSpell->Mechanic) ==
                   e.aura->GetModifier()->m_miscvalue
                ? ProcResult::Ok
                : ProcResult::Failed;
        }

        ProcResult ManaShield(ProcEvent const& e)
        {
            SpellEntry const* dummySpell = e.AuraSpell();

            if (!dummySpell)
            {
                return ProcResult::Failed;
            }

            Unit*         target    = e.target;
            std::uint32_t triggered = 0;

            if (dummySpell->SpellClassSet == SPELLFAMILY_MAGE)
            {
                // Incanter's Regalia, on top of Mana Shield.
                if (dummySpell->SpellClassMask & UI64LIT(0x0000000000008000))
                {
                    if (!e.ActorPlayer())
                    {
                        return ProcResult::Failed;
                    }

                    target    = e.actor;
                    triggered = 37436;
                }
            }

            // Counting the charge is all this one does otherwise.
            if (triggered == 0)
            {
                return ProcResult::Failed;
            }

            return e.Trigger(target, triggered, nullptr, "Procs::ManaShield");
        }

        ProcResult AttackPowerAttackerBonus(ProcEvent const& e)
        {
            SpellEntry const* dummySpell = e.AuraSpell();

            if (!dummySpell || !e.aura)
            {
                return ProcResult::Failed;
            }

            // Hunter's Mark, ranks 1-4: grows by a tenth of its base per hit,
            // to four times base.
            if (dummySpell->SpellClassSet == SPELLFAMILY_HUNTER &&
                (dummySpell->SpellClassMask & UI64LIT(0x0000000000000400)))
            {
                const std::int32_t base = e.BasePoints();
                Modifier* modifier = e.aura->GetModifier();

                modifier->m_amount += base / 10;

                if (modifier->m_amount > base * 4)
                {
                    modifier->m_amount = base * 4;
                }
            }

            return ProcResult::Ok;
        }

        ProcResult ModResistance(ProcEvent const& e)
        {
            SpellEntry const* spellInfo = e.AuraSpell();

            if (!spellInfo)
            {
                return ProcResult::Failed;
            }

            // Inner Fire spends a charge on real damage only.
            if (spellInfo->IsFitToFamily(SPELLFAMILY_PRIEST,
                                         UI64LIT(0x0000000000002)) &&
                e.damage == 0)
            {
                return ProcResult::Failed;
            }

            return ProcResult::Ok;
        }

        ProcResult RemoveByDamageChance(ProcEvent const& e)
        {
            if (!e.actor || !e.aura)
            {
                return ProcResult::Failed;
            }

            // The chance to break scales the damage taken against what the
            // holder's level makes survivable.
            const std::uint32_t level = e.actor->getLevel();
            const std::uint32_t maxDamage =
                level > 8 ? 25 * level - 150 : 50;

            const float chance =
                float(e.damage) / float(maxDamage) * 100.0f;

            if (!roll_chance_f(chance))
            {
                return ProcResult::Failed;
            }

            e.aura->SetInUse(true);
            e.actor->RemoveAurasByCasterSpell(e.aura->GetId(),
                                              e.aura->GetCasterGuid());
            e.aura->SetInUse(false);

            return ProcResult::Ok;
        }

        ProcResult Invisibility(ProcEvent const& e)
        {
            SpellEntry const* spellInfo = e.AuraSpell();

            if (!spellInfo || !e.actor)
            {
                return ProcResult::Failed;
            }

            if (spellInfo->HasAttribute(SPELL_ATTR_PASSIVE) ||
                spellInfo->HasAttribute(SPELL_ATTR_EX_CANT_BE_REFLECTED))
            {
                return ProcResult::Failed;
            }

            e.actor->RemoveAurasDueToSpell(e.aura->GetId());

            return ProcResult::Ok;
        }

        namespace
        {
            /// The numbers a proc's triggered spell may compute from, read
            /// once at the moment it fires. A step three seconds in gets what
            /// was true when the proc happened, not what is true when it runs.
            PointsInputs NumbersOf(ProcEvent const& e)
            {
                PointsInputs in;

                in.damage           = std::int32_t(e.damage);
                in.auraAmount       = e.Amount();
                in.procSpellManaCost = e.procSpell
                    ? std::int32_t(e.procSpell->ManaCost)
                    : 0;

                if (SpellEntry const* aura = e.AuraSpell())
                {
                    in.auraEffectValue =
                        aura->CalculateSimpleValue(EFFECT_INDEX_1);
                }

                if (e.actor)
                {
                    in.actorMaxHealth   = std::int32_t(e.actor->GetMaxHealth());
                    in.actorMaxMana     =
                        std::int32_t(e.actor->GetMaxPower(POWER_MANA));
                    in.actorAttackPower = std::int32_t(
                        e.actor->GetTotalAttackPowerValue(BASE_ATTACK));
                }

                if (e.target)
                {
                    in.targetCreateHealth =
                        std::int32_t(e.target->GetCreateHealth());
                }

                return in;
            }

            /// Ask the scripts. True when one of them owned this proc.
            bool RanAsScript(ProcEvent const& e)
            {
                SpellEntry const* aura = e.AuraSpell();
                if (!aura)
                {
                    return false;
                }

                return mai::MaiEngine::AuraProcced(
                    e.actor, e.target, aura->ID,
                    e.procSpell ? e.procSpell->ID : 0, NumbersOf(e));
            }
        }

        ProcResult Dummy(ProcEvent const& e)
        {
            if (RanAsScript(e))
            {
                return ProcResult::Ok;
            }

            return e.actor->HandleDummyAuraProc(e);
        }

        ProcResult TriggerSpell(ProcEvent const& e)
        {
            if (RanAsScript(e))
            {
                return ProcResult::Ok;
            }

            return e.actor->HandleProcTriggerSpellAuraProc(e);
        }
    }
}
