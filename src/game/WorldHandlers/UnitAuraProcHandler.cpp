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

#include "Platform/Define.h"
#include "Utilities/MathDefines.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Player.h"
#include "Unit.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "Totem.h"
#include "Creature.h"
#include "Util.h"
#include <cmath>
#include <ctime>
#include "ObjectLookup.h"
#include "combat/CombatRegistry.h"
#include "combat/ReactionQueue.h"
#include "Map.h"

/**
 * @brief Checks whether a proc aura may trigger for the current spell event context.
 *
 * @param pVictim The proc victim.
 * @param holder The aura holder being evaluated.
 * @param procSpell The spell that caused the proc event.
 * @param procFlag The proc flags for the event.
 * @param procExtra Additional proc context flags.
 * @param attType The triggering attack type.
 * @param isVictim True if the current unit is the victim side of the event.
 * @param spellProcEvent Receives the resolved proc event entry.
 * @return true if the aura should trigger; otherwise false.
 */
bool Unit::IsTriggeredAtSpellProcEvent(Unit* pVictim, SpellAuraHolder* holder, SpellEntry const* procSpell, uint32 procFlag, uint32 procExtra, WeaponAttackType attType, bool isVictim, SpellProcEventEntry const*& spellProcEvent)
{
    SpellEntry const* spellProto = holder->GetSpellProto();

    // Get proc Event Entry
    spellProcEvent = sSpellMgr.GetSpellProcEvent(spellProto->ID);

    // Get EventProcFlag
    uint32 EventProcFlag;
    if (spellProcEvent && spellProcEvent->procFlags) // if exist get custom spellProcEvent->procFlags
    {
        EventProcFlag = spellProcEvent->procFlags;
    }
    else
    {
        EventProcFlag = spellProto->ProcTypeMask;        // else get from spell proto
    }
    // Continue if no trigger exist
    if (!EventProcFlag)
    {
        return false;
    }

    // Check spellProcEvent data requirements
    if (!SpellMgr::IsSpellProcEventCanTriggeredBy(spellProcEvent, EventProcFlag, procSpell, procFlag, procExtra))
    {
        return false;
    }

    // In most cases req get honor or XP from kill
    if (EventProcFlag & PROC_FLAG_KILL && GetTypeId() == TYPEID_PLAYER)
    {
        bool allow = ((Player*)this)->isHonorOrXPTarget(pVictim);
        // Shadow Word: Death - can trigger from every kill
        if (holder->GetId() == 32409)
        {
            allow = true;
        }
        if (!allow)
        {
            return false;
        }
    }
    // Aura added by spell can`t trigger from self (prevent drop charges/do triggers)
    // But except periodic triggers (can triggered from self)
    if (procSpell && procSpell->ID == spellProto->ID && !(EventProcFlag & PROC_FLAG_ON_TAKE_PERIODIC))
    {
        return false;
    }

    // Check if current equipment allows aura to proc
    if (!isVictim && GetTypeId() == TYPEID_PLAYER)
    {
        if (spellProto->EquippedItemClass == ITEM_CLASS_WEAPON)
        {
            Item* item = NULL;
            if (attType == BASE_ATTACK)
            {
                item = ((Player*)this)->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
            }
            else if (attType == OFF_ATTACK)
            {
                item = ((Player*)this)->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            }
            else
            {
                item = ((Player*)this)->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
            }

            if (!CanUseEquippedWeapon(attType))
            {
                return false;
            }

            if (!item || item->IsBroken() || item->GetProto()->Class != ITEM_CLASS_WEAPON || !((1 << item->GetProto()->SubClass) & spellProto->EquippedItemSubclass))
            {
                return false;
            }
        }
        else if (spellProto->EquippedItemClass == ITEM_CLASS_ARMOR)
        {
            // Check if player is wearing shield
            Item* item = ((Player*)this)->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            if (!item || item->IsBroken() || item->GetProto()->Class != ITEM_CLASS_ARMOR || !((1 << item->GetProto()->SubClass) & spellProto->EquippedItemSubclass))
            {
                return false;
            }
        }
    }
    // Get chance from spell
    float chance = (float)spellProto->ProcChance;
    // If in spellProcEvent exist custom chance, chance = spellProcEvent->customChance;
    if (spellProcEvent && spellProcEvent->customChance)
    {
        chance = spellProcEvent->customChance;
    }
    // If PPM exist calculate chance from PPM
    if (!isVictim && spellProcEvent && spellProcEvent->ppmRate != 0)
    {
        uint32 WeaponSpeed = GetAttackTime(attType);
        chance = GetPPMProcChance(WeaponSpeed, spellProcEvent->ppmRate);
    }
    // Apply chance modifier aura
    if (Player* modOwner = GetSpellModOwner())
    {
        modOwner->ApplySpellMod(spellProto->ID, SPELLMOD_CHANCE_OF_SUCCESS, chance);
    }

    return roll_chance_f(chance);
}

Combat::ProcResult Unit::HandleDummyAuraProc(Combat::ProcEvent const& e)
{
    Unit* const pVictim = e.target;
    Aura* const triggeredByAura = e.aura;
    SpellEntry const* const procSpell = e.procSpell;
    uint32 const damage = e.damage;
    uint32 const procFlag = e.flags;
    uint32 const procEx = e.extra;
    uint32 const cooldown = e.cooldown;

    SpellEntry const* dummySpell = triggeredByAura->GetSpellProto();
    SpellEffectIndex effIndex = triggeredByAura->GetEffIndex();
    int32  triggerAmount = triggeredByAura->GetModifier()->m_amount;

    Item* castItem = triggeredByAura->GetCastItemGuid() && GetTypeId() == TYPEID_PLAYER
                     ? ((Player*)this)->GetItemByGuid(triggeredByAura->GetCastItemGuid()) : NULL;

    uint32 triggered_spell_id = 0;
    Unit* target = pVictim;
    int32  basepoints[MAX_EFFECT_INDEX] = {0, 0, 0};

    switch (dummySpell->SpellClassSet)
    {
        case SPELLFAMILY_GENERIC:
        {
            switch (dummySpell->ID)
            {
                    // Eye for an Eye
                case 9799:
                case 25988:
                {
                    // prevent damage back from weapon special attacks
                    if (!procSpell || procSpell->DefenseType != SPELL_DAMAGE_CLASS_MAGIC)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // return damage % to attacker but < 50% own total health
                    basepoints[0] = triggerAmount * int32(damage) / 100;
                    if (basepoints[0] > (int32)GetMaxHealth() / 2)
                    {
                        basepoints[0] = (int32)GetMaxHealth() / 2;
                    }

                    triggered_spell_id = 25997;
                    break;
                }
                // Sweeping Strikes
                case 12328:
                case 18765:
                case 35429:
                {
                    // prevent chain of triggered spell from same triggered spell
                    if (procSpell && procSpell->ID == 26654)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    target = SelectRandomUnfriendlyTarget(pVictim);
                    if (!target)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    triggered_spell_id = 26654;
                    break;
                }
                // Twisted Reflection (boss spell)
                case 21063:
                    triggered_spell_id = 21064;
                    break;
                    // Unstable Power
                case 24658:
                {
                    if (!procSpell || procSpell->ID == 24659)
                    {
                        return Combat::ProcResult::Failed;
                    }
                    // Need remove one 24659 aura
                    RemoveAuraHolderFromStack(24659);
                    return Combat::ProcResult::Ok;
                }
                // Restless Strength
                case 24661:
                {
                    // Need remove one 24662 aura
                    RemoveAuraHolderFromStack(24662);
                    return Combat::ProcResult::Ok;
                }
                // Adaptive Warding (Frostfire Regalia set)
                case 28764:
                {
                    if (!procSpell)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // find Mage Armor
                    bool found = false;
                    AuraList const& mRegenInterrupt = GetAurasByType(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT);
                    for (AuraList::const_iterator iter = mRegenInterrupt.begin(); iter != mRegenInterrupt.end(); ++iter)
                    {
                        if (SpellEntry const* iterSpellProto = (*iter)->GetSpellProto())
                        {
                            if (iterSpellProto->SpellClassSet == SPELLFAMILY_MAGE && (iterSpellProto->SpellClassMask & UI64LIT(0x10000000)))
                            {
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    switch (GetFirstSchoolInMask(GetSpellSchoolMask(procSpell)))
                    {
                        case SPELL_SCHOOL_NORMAL:
                        case SPELL_SCHOOL_HOLY:
                            return Combat::ProcResult::Failed;  // ignored
                        case SPELL_SCHOOL_FIRE:   triggered_spell_id = 28765; break;
                        case SPELL_SCHOOL_NATURE: triggered_spell_id = 28768; break;
                        case SPELL_SCHOOL_FROST:  triggered_spell_id = 28766; break;
                        case SPELL_SCHOOL_SHADOW: triggered_spell_id = 28769; break;
                        case SPELL_SCHOOL_ARCANE: triggered_spell_id = 28770; break;
                        default:
                            return Combat::ProcResult::Failed;
                    }

                    target = this;
                    break;
                }
                // Obsidian Armor (Justice Bearer`s Pauldrons shoulder)
                case 27539:
                {
                    if (!procSpell)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    switch (GetFirstSchoolInMask(GetSpellSchoolMask(procSpell)))
                    {
                        case SPELL_SCHOOL_NORMAL:
                            return Combat::ProcResult::Failed;  // ignore
                        case SPELL_SCHOOL_HOLY:   triggered_spell_id = 27536; break;
                        case SPELL_SCHOOL_FIRE:   triggered_spell_id = 27533; break;
                        case SPELL_SCHOOL_NATURE: triggered_spell_id = 27538; break;
                        case SPELL_SCHOOL_FROST:  triggered_spell_id = 27534; break;
                        case SPELL_SCHOOL_SHADOW: triggered_spell_id = 27535; break;
                        case SPELL_SCHOOL_ARCANE: triggered_spell_id = 27540; break;
                        default:
                            return Combat::ProcResult::Failed;
                    }

                    target = this;
                    break;
                }
                // Mana Leech (Passive) (Priest Pet Aura)
                case 28305:
                {
                    // Cast on owner
                    target = GetOwner();
                    if (!target)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    basepoints[0] = int32(damage * 2.5f);   // manaregen
                    triggered_spell_id = 34650;
                    break;
                }
                // Mark of Malice
                case 33493:
                {
                    // Cast finish spell at last charge
                    if (triggeredByAura->GetHolder()->GetAuraCharges() > 1)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    target = this;
                    triggered_spell_id = 33494;
                    break;
                }
                // Vampiric Aura (boss spell)
                case 38196:
                {
                    basepoints[0] = 3 * damage;             // 300%
                    if (basepoints[0] < 0)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    triggered_spell_id = 31285;
                    target = this;
                    break;
                }
                // Aura of Madness (Darkmoon Card: Madness trinket)
                //=====================================================
                // 39511 Sociopath: +35 strength (Paladin, Rogue, Druid, Warrior)
                // 40997 Delusional: +70 attack power (Rogue, Hunter, Paladin, Warrior, Druid)
                // 40998 Kleptomania: +35 agility (Warrior, Rogue, Paladin, Hunter, Druid)
                // 40999 Megalomania: +41 damage/healing (Druid, Shaman, Priest, Warlock, Mage, Paladin)
                // 41002 Paranoia: +35 spell/melee/ranged crit strike rating (All classes)
                // 41005 Manic: +35 haste (spell, melee and ranged) (All classes)
                // 41009 Narcissism: +35 intellect (Druid, Shaman, Priest, Warlock, Mage, Paladin, Hunter)
                // 41011 Martyr Complex: +35 stamina (All classes)
                // 41406 Dementia: Every 5 seconds either gives you +5% damage/healing. (Druid, Shaman, Priest, Warlock, Mage, Paladin)
                // 41409 Dementia: Every 5 seconds either gives you -5% damage/healing. (Druid, Shaman, Priest, Warlock, Mage, Paladin)
                case 39446:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // Select class defined buff
                    switch (getClass())
                    {
                        case CLASS_PALADIN:                 // 39511,40997,40998,40999,41002,41005,41009,41011,41409
                        case CLASS_DRUID:                   // 39511,40997,40998,40999,41002,41005,41009,41011,41409
                        {
                            uint32 RandomSpell[] = {39511, 40997, 40998, 40999, 41002, 41005, 41009, 41011, 41409};
                            triggered_spell_id = RandomSpell[urand(0, countof(RandomSpell)-1)];
                            break;
                        }
                        case CLASS_ROGUE:                   // 39511,40997,40998,41002,41005,41011
                        case CLASS_WARRIOR:                 // 39511,40997,40998,41002,41005,41011
                        {
                            uint32 RandomSpell[] = {39511, 40997, 40998, 41002, 41005, 41011};
                            triggered_spell_id = RandomSpell[urand(0, countof(RandomSpell)-1)];
                            break;
                        }
                        case CLASS_PRIEST:                  // 40999,41002,41005,41009,41011,41406,41409
                        case CLASS_SHAMAN:                  // 40999,41002,41005,41009,41011,41406,41409
                        case CLASS_MAGE:                    // 40999,41002,41005,41009,41011,41406,41409
                        case CLASS_WARLOCK:                 // 40999,41002,41005,41009,41011,41406,41409
                        {
                            uint32 RandomSpell[] = {40999, 41002, 41005, 41009, 41011, 41406, 41409};
                            triggered_spell_id = RandomSpell[urand(0, countof(RandomSpell)-1)];
                            break;
                        }
                        case CLASS_HUNTER:                  // 40997,40999,41002,41005,41009,41011,41406,41409
                        {
                            uint32 RandomSpell[] = {40997, 40999, 41002, 41005, 41009, 41011, 41406, 41409};
                            triggered_spell_id = RandomSpell[urand(0, countof(RandomSpell)-1)];
                            break;
                        }
                        default:
                            return Combat::ProcResult::Failed;
                    }

                    target = this;
                    if (roll_chance_i(10))
                    {
                        ((Player*)this)->Say("This is Madness!", LANG_UNIVERSAL);
                    }
                    break;
                }
                // Sunwell Exalted Caster Neck (Shattered Sun Pendant of Acumen neck)
                // cast 45479 Light's Wrath if Exalted by Aldor
                // cast 45429 Arcane Bolt if Exalted by Scryers
                case 45481:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // Get Aldor reputation rank
                    if (((Player*)this)->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45479;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (((Player*)this)->GetReputationRank(934) == REP_EXALTED)
                    {
                        // triggered at positive/self casts also, current attack target used then
                        if (IsFriendlyTo(target))
                        {
                            target = getVictim();
                            if (!target)
                            {
                                target = ObjectLookup::GetUnit(*this, ((Player*)this)->GetSelectionGuid());
                                if (!target)
                                {
                                    return Combat::ProcResult::Failed;
                                }
                            }
                            if (IsFriendlyTo(target))
                            {
                                return Combat::ProcResult::Failed;
                            }
                        }

                        triggered_spell_id = 45429;
                        break;
                    }
                    return Combat::ProcResult::Failed;
                }
                // Sunwell Exalted Melee Neck (Shattered Sun Pendant of Might neck)
                // cast 45480 Light's Strength if Exalted by Aldor
                // cast 45428 Arcane Strike if Exalted by Scryers
                case 45482:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // Get Aldor reputation rank
                    if (((Player*)this)->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45480;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (((Player*)this)->GetReputationRank(934) == REP_EXALTED)
                    {
                        triggered_spell_id = 45428;
                        break;
                    }
                    return Combat::ProcResult::Failed;
                }
                // Sunwell Exalted Tank Neck (Shattered Sun Pendant of Resolve neck)
                // cast 45431 Arcane Insight if Exalted by Aldor
                // cast 45432 Light's Ward if Exalted by Scryers
                case 45483:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // Get Aldor reputation rank
                    if (((Player*)this)->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45432;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (((Player*)this)->GetReputationRank(934) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45431;
                        break;
                    }
                    return Combat::ProcResult::Failed;
                }
                // Sunwell Exalted Healer Neck (Shattered Sun Pendant of Restoration neck)
                // cast 45478 Light's Salvation if Exalted by Aldor
                // cast 45430 Arcane Surge if Exalted by Scryers
                case 45484:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // Get Aldor reputation rank
                    if (((Player*)this)->GetReputationRank(932) == REP_EXALTED)
                    {
                        target = this;
                        triggered_spell_id = 45478;
                        break;
                    }
                    // Get Scryers reputation rank
                    if (((Player*)this)->GetReputationRank(934) == REP_EXALTED)
                    {
                        triggered_spell_id = 45430;
                        break;
                    }
                    return Combat::ProcResult::Failed;
                }
                /*
                // Sunwell Exalted Caster Neck (??? neck)
                // cast ??? Light's Wrath if Exalted by Aldor
                // cast ??? Arcane Bolt if Exalted by Scryers*/
                case 46569:
                    return Combat::ProcResult::Failed;          // old unused version
            }
            break;
        }
        case SPELLFAMILY_MAGE:
        {
            // Magic Absorption
            if (dummySpell->SpellIconID == 459)             // only this spell have SpellIconID == 459 and dummy aura
            {
                if (GetPowerType() != POWER_MANA)
                {
                    return Combat::ProcResult::Failed;
                }

                // mana reward
                basepoints[0] = (triggerAmount * GetMaxPower(POWER_MANA) / 100);
                target = this;
                triggered_spell_id = 29442;
                break;
            }
            // Master of Elements
            if (dummySpell->SpellIconID == 1920)
            {
                if (!procSpell)
                {
                    return Combat::ProcResult::Failed;
                }

                // mana cost save
                int32 cost = procSpell->ManaCost + procSpell->ManaCostPct * GetCreateMana() / 100;
                basepoints[0] = cost * triggerAmount / 100;
                if (basepoints[0] <= 0)
                {
                    return Combat::ProcResult::Failed;
                }

                target = this;
                triggered_spell_id = 29077;
                break;
            }

            switch (dummySpell->ID)
            {
                    // Ignite
                case 11119:
                case 11120:
                case 12846:
                case 12847:
                case 12848:
                {
                    switch (dummySpell->ID)
                    {
                        case 11119: basepoints[0] = int32(0.04f * damage); break;
                        case 11120: basepoints[0] = int32(0.08f * damage); break;
                        case 12846: basepoints[0] = int32(0.12f * damage); break;
                        case 12847: basepoints[0] = int32(0.16f * damage); break;
                        case 12848: basepoints[0] = int32(0.20f * damage); break;
                        default:
                            sLog.outError("Unit::HandleDummyAuraProc: non handled spell id: %u (IG)", dummySpell->ID);
                            return Combat::ProcResult::Failed;
                    }

                    triggered_spell_id = 12654;
                    break;
                }
                // Combustion
                case 11129:
                {
                    // last charge and crit
                    if (triggeredByAura->GetHolder()->GetAuraCharges() <= 1 && (procEx & PROC_EX_CRITICAL_HIT))
                    {
                        RemoveAurasDueToSpell(28682);       //-> remove Combustion auras
                        return Combat::ProcResult::Ok;          // charge counting (will removed)
                    }

                    CastSpell(this, 28682, true, castItem, triggeredByAura);
                    // charge update only at crit hits, no hidden cooldowns
                    return (procEx & PROC_EX_CRITICAL_HIT)
                        ? Combat::ProcResult::Ok
                        : Combat::ProcResult::Failed;
                }
            }
            break;
        }
        case SPELLFAMILY_WARRIOR:
        {
            // Retaliation
            if (dummySpell->IsFitToFamilyMask(UI64LIT(0x0000000800000000)))
            {
                // check attack comes not from behind
                if (!Where().HasInArc(pVictim->Where(), M_PI_F))
                {
                    return Combat::ProcResult::Failed;
                }

                triggered_spell_id = 22858;
                break;
            }
            // Second Wind
            if (dummySpell->SpellIconID == 1697)
            {
                // only for spells and hit/crit (trigger start always) and not start from self casted spells (5530 Mace Stun Effect for example)
                if (procSpell == 0 || !(procEx & (PROC_EX_NORMAL_HIT | PROC_EX_CRITICAL_HIT)) || this == pVictim)
                {
                    return Combat::ProcResult::Failed;
                }
                // Need stun or root mechanic
                if (!(GetAllSpellMechanicMask(procSpell) & IMMUNE_TO_ROOT_AND_STUN_MASK))
                {
                    return Combat::ProcResult::Failed;
                }

                switch (dummySpell->ID)
                {
                    case 29838: triggered_spell_id=29842; break;
                    case 29834: triggered_spell_id=29841; break;
                    default:
                        sLog.outError("Unit::HandleDummyAuraProc: non handled spell id: %u (SW)", dummySpell->ID);
                        return Combat::ProcResult::Failed;
                }

                target = this;
                break;
            }
            break;
        }
        case SPELLFAMILY_WARLOCK:
        {
            // Seed of Corruption
            if (dummySpell->SpellClassMask & UI64LIT(0x0000001000000000))
            {
                Modifier* mod = triggeredByAura->GetModifier();
                // if damage is more than need or target die from damage deal finish spell
                if (mod->m_amount <= (int32)damage || GetHealth() <= damage)
                {
                    // remember guid before aura delete
                    ObjectGuid casterGuid = triggeredByAura->GetCasterGuid();

                    // Remove aura (before cast for prevent infinite loop handlers)
                    RemoveAurasDueToSpell(triggeredByAura->GetId());

                    // Cast finish spell (triggeredByAura already not exist!)
                    CastSpell(this, 27285, true, castItem, NULL, casterGuid);
                    return Combat::ProcResult::Ok;              // no hidden cooldown
                }

                // Damage counting
                mod->m_amount -= damage;
                return Combat::ProcResult::Ok;
            }
            // Seed of Corruption (Mobs cast) - no die req
            if (dummySpell->SpellClassMask == UI64LIT(0x0) && dummySpell->SpellIconID == 1932)
            {
                Modifier* mod = triggeredByAura->GetModifier();
                // if damage is more than need deal finish spell
                if (mod->m_amount <= (int32)damage)
                {
                    // remember guid before aura delete
                    ObjectGuid casterGuid = triggeredByAura->GetCasterGuid();

                    // Remove aura (before cast for prevent infinite loop handlers)
                    RemoveAurasDueToSpell(triggeredByAura->GetId());

                    // Cast finish spell (triggeredByAura already not exist!)
                    CastSpell(this, 32865, true, castItem, NULL, casterGuid);
                    return Combat::ProcResult::Ok;              // no hidden cooldown
                }
                // Damage counting
                mod->m_amount -= damage;
                return Combat::ProcResult::Ok;
            }
            switch (dummySpell->ID)
            {
                    // Nightfall
                case 18094:
                case 18095:
                {
                    target = this;
                    triggered_spell_id = 17941;
                    break;
                }
                // Soul Leech
                case 30293:
                case 30295:
                case 30296:
                {
                    // health
                    basepoints[0] = int32(damage * triggerAmount / 100);
                    target = this;
                    triggered_spell_id = 30294;
                    break;
                }
                // Shadowflame (Voidheart Raiment set bonus)
                case 37377:
                {
                    triggered_spell_id = 37379;
                    break;
                }
                // Pet Healing (Corruptor Raiment or Rift Stalker Armor)
                case 37381:
                {
                    target = GetPet();
                    if (!target)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // heal amount
                    basepoints[0] = damage * triggerAmount / 100;
                    triggered_spell_id = 37382;
                    break;
                }
                // Shadowflame Hellfire (Voidheart Raiment set bonus)
                case 39437:
                {
                    triggered_spell_id = 37378;
                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_PRIEST:
        {
            // Vampiric Touch
            if (dummySpell->SpellClassMask & UI64LIT(0x0000040000000000))
            {
                if (!pVictim || !pVictim->IsAlive())
                {
                    return Combat::ProcResult::Failed;
                }

                // pVictim is caster of aura
                if (triggeredByAura->GetCasterGuid() != pVictim->GetObjectGuid())
                {
                    return Combat::ProcResult::Failed;
                }

                // energize amount
                basepoints[0] = triggerAmount * damage / 100;
                pVictim->CastCustomSpell(pVictim, 34919, &basepoints[0], NULL, NULL, true, castItem, triggeredByAura);
                return Combat::ProcResult::Ok;                  // no hidden cooldown
            }
            switch (dummySpell->ID)
            {
                    // Vampiric Embrace
                case 15286:
                {
                    if (!pVictim || !pVictim->IsAlive())
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // pVictim is caster of aura
                    if (triggeredByAura->GetCasterGuid() != pVictim->GetObjectGuid())
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // heal amount
                    basepoints[0] = triggerAmount * damage / 100;
                    pVictim->CastCustomSpell(pVictim, 15290, &basepoints[0], NULL, NULL, true, castItem, triggeredByAura);
                    return Combat::ProcResult::Ok;              // no hidden cooldown
                }
                // Priest Tier 6 Trinket (Ashtongue Talisman of Acumen)
                case 40438:
                {
                    // Shadow Word: Pain
                    if (procSpell->SpellClassMask & UI64LIT(0x0000000000008000))
                    {
                        triggered_spell_id = 40441;
                    }
                    // Renew
                    else if (procSpell->SpellClassMask & UI64LIT(0x0000000000000010))
                    {
                        triggered_spell_id = 40440;
                    }
                    else
                    {
                        return Combat::ProcResult::Failed;
                    }

                    target = this;
                    break;
                }
                // Oracle Healing Bonus ("Garments of the Oracle" set)
                case 26169:
                {
                    // heal amount
                    basepoints[0] = int32(damage * 10 / 100);
                    target = this;
                    triggered_spell_id = 26170;
                    break;
                }
                // Frozen Shadoweave (Shadow's Embrace set) warning! its not only priest set
                case 39372:
                {
                    if (!procSpell || (GetSpellSchoolMask(procSpell) & (SPELL_SCHOOL_MASK_FROST | SPELL_SCHOOL_MASK_SHADOW)) == 0)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // heal amount
                    basepoints[0] = damage * triggerAmount / 100;
                    target = this;
                    triggered_spell_id = 39373;
                    break;
                }
                // Greater Heal (Vestments of Faith (Priest Tier 3) - 4 pieces bonus)
                case 28809:
                {
                    triggered_spell_id = 28810;
                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_DRUID:
        {
            switch (dummySpell->ID)
            {
                    // Healing Touch (Dreamwalker Raiment set)
                case 28719:
                {
                    // mana back
                    basepoints[0] = int32(procSpell->ManaCost * 30 / 100);
                    target = this;
                    triggered_spell_id = 28742;
                    break;
                }
                // Healing Touch Refund (Idol of Longevity trinket)
                case 28847:
                {
                    target = this;
                    triggered_spell_id = 28848;
                    break;
                }
                // Mana Restore (Malorne Raiment set / Malorne Regalia set)
                case 37288:
                case 37295:
                {
                    target = this;
                    triggered_spell_id = 37238;
                    break;
                }
                // Druid Tier 6 Trinket
                case 40442:
                {
                    float  chance;

                    // Starfire
                    if (procSpell->SpellClassMask & UI64LIT(0x0000000000000004))
                    {
                        triggered_spell_id = 40445;
                        chance = 25.0f;
                    }
                    // Rejuvenation
                    else if (procSpell->SpellClassMask & UI64LIT(0x0000000000000010))
                    {
                        triggered_spell_id = 40446;
                        chance = 25.0f;
                    }
                    // Mangle (Bear) and Mangle (Cat)
                    else if (procSpell->SpellClassMask & UI64LIT(0x0000044000000000))
                    {
                        triggered_spell_id = 40452;
                        chance = 40.0f;
                    }
                    else
                    {
                        return Combat::ProcResult::Failed;
                    }

                    if (!roll_chance_f(chance))
                    {
                        return Combat::ProcResult::Failed;
                    }

                    target = this;
                    break;
                }
                // Maim Interrupt
                case 44835:
                {
                    // Deadly Interrupt Effect
                    triggered_spell_id = 32747;
                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_ROGUE:
        {
            switch (dummySpell->ID)
            {
                    // Clean Escape
                case 23582:
                    // triggered spell have same masks and etc with main Vanish spell
                    if (!procSpell || procSpell->Effect[EFFECT_INDEX_0] == SPELL_EFFECT_NONE)
                    {
                        return Combat::ProcResult::Failed;
                    }
                    triggered_spell_id = 23583;
                    break;
                    // Deadly Throw Interrupt
                case 32748:
                {
                    // Prevent cast Deadly Throw Interrupt on self from last effect (apply dummy) of Deadly Throw
                    if (this == pVictim)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    triggered_spell_id = 32747;
                    break;
                }
            }
            // Quick Recovery
            if (dummySpell->SpellIconID == 2116)
            {
                if (!procSpell)
                {
                    return Combat::ProcResult::Failed;
                }

                // energy cost save
                basepoints[0] = procSpell->ManaCost * triggerAmount / 100;
                if (basepoints[0] <= 0)
                {
                    return Combat::ProcResult::Failed;
                }

                target = this;
                triggered_spell_id = 31663;
                break;
            }
            break;
        }
        case SPELLFAMILY_HUNTER:
        {
            // Thrill of the Hunt
            if (dummySpell->SpellIconID == 2236)
            {
                if (!procSpell)
                {
                    return Combat::ProcResult::Failed;
                }

                // mana cost save
                basepoints[0] = procSpell->ManaCost * 40 / 100;
                if (basepoints[0] <= 0)
                {
                    return Combat::ProcResult::Failed;
                }

                target = this;
                triggered_spell_id = 34720;
                break;
            }
            break;
        }
        case SPELLFAMILY_PALADIN:
        {
            // Seal of Righteousness - melee proc dummy
            if ((dummySpell->SpellClassMask & UI64LIT(0x000000008000000)) && triggeredByAura->GetEffIndex() == EFFECT_INDEX_0)
            {
                if (GetTypeId() != TYPEID_PLAYER)
                {
                    return Combat::ProcResult::Failed;
                }

                uint32 spellId;
                switch (triggeredByAura->GetId())
                {
                    case 20154:
                    case 21084: spellId = 25742; break;     // Rank 1
                    case 20287: spellId = 25740; break;     // Rank 2
                    case 20288: spellId = 25739; break;     // Rank 3
                    case 20289: spellId = 25738; break;     // Rank 4
                    case 20290: spellId = 25737; break;     // Rank 5
                    case 20291: spellId = 25736; break;     // Rank 6
                    case 20292: spellId = 25735; break;     // Rank 7
                    case 20293: spellId = 25713; break;     // Rank 8
                    case 27155: spellId = 27156; break;     // Rank 9
                    default:
                        sLog.outError("Unit::HandleDummyAuraProc: non handled possibly SoR (Id = %u)", triggeredByAura->GetId());
                        return Combat::ProcResult::Failed;
                }
                Item* item = ((Player*)this)->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
                float speed = (item ? item->GetProto()->Delay : BASE_ATTACK_TIME) / 1000.0f;

                float damageBasePoints;
                if (item && item->GetProto()->InventoryType == INVTYPE_2HWEAPON)
                    // two hand weapon
                {
                    damageBasePoints = 1.20f * triggerAmount * 1.2f * 1.03f * speed / 100.0f + 1;
                }
                else
                    // one hand weapon/no weapon
                {
                    damageBasePoints = 0.85f * ceil(triggerAmount * 1.2f * 1.03f * speed / 100.0f) - 1;
                }

                int32 damagePoint = int32(damageBasePoints + 0.03f * (GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE) + GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE)) / 2.0f) + 1;

                // apply damage bonuses manually
                if (damagePoint >= 0)
                {
                    damagePoint = SpellDamageBonusDone(pVictim, dummySpell, damagePoint, SPELL_DIRECT_DAMAGE);
                    damagePoint = pVictim->SpellDamageBonusTaken(this, dummySpell, damagePoint, SPELL_DIRECT_DAMAGE);
                }

                CastCustomSpell(pVictim, spellId, &damagePoint, NULL, NULL, true, NULL, triggeredByAura);
                return Combat::ProcResult::Ok;                  // no hidden cooldown
            }
            // Seal of Blood do damage trigger
            if (dummySpell->SpellClassMask & UI64LIT(0x0000040000000000))
            {
                switch (triggeredByAura->GetEffIndex())
                {
                    case EFFECT_INDEX_0:
                        triggered_spell_id = 31893;
                        break;
                    case EFFECT_INDEX_1:
                    {
                        // damage
                        basepoints[0] = triggerAmount * damage / 100;
                        target = this;
                        triggered_spell_id = 32221;
                        break;
                    }
                    default:
                        break;
                }
            }

            switch (dummySpell->ID)
            {
                    // Holy Power (Redemption Armor set)
                case 28789:
                {
                    if (!pVictim)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // Set class defined buff
                    switch (pVictim->getClass())
                    {
                        case CLASS_PALADIN:
                        case CLASS_PRIEST:
                        case CLASS_SHAMAN:
                        case CLASS_DRUID:
                            triggered_spell_id = 28795;     // Increases the friendly target's mana regeneration by $s1 per 5 sec. for $d.
                            break;
                        case CLASS_MAGE:
                        case CLASS_WARLOCK:
                            triggered_spell_id = 28793;     // Increases the friendly target's spell damage and healing by up to $s1 for $d.
                            break;
                        case CLASS_HUNTER:
                        case CLASS_ROGUE:
                            triggered_spell_id = 28791;     // Increases the friendly target's attack power by $s1 for $d.
                            break;
                        case CLASS_WARRIOR:
                            triggered_spell_id = 28790;     // Increases the friendly target's armor
                            break;
                        default:
                            return Combat::ProcResult::Failed;
                    }
                    break;
                }
                // Spiritual Attunement
                case 31785:
                case 33776:
                {
                    // if healed by another unit (pVictim)
                    if (this == pVictim)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // dont count overhealing
                    uint32 diff = GetMaxHealth() - GetHealth();
                    if (!diff)
                    {
                        return Combat::ProcResult::Failed;
                    }
                    basepoints[0] = triggerAmount * (damage > diff ? diff : damage) / 100;
                    target = this;
                    triggered_spell_id = 31786;
                    break;
                }
                // Seal of Vengeance (damage calc on apply aura)
                case 31801:
                {
                    if (effIndex != EFFECT_INDEX_0)         // effect 1,2 used by seal unleashing code
                    {
                        return Combat::ProcResult::Failed;
                    }

                    triggered_spell_id = 31803;
                    break;
                }
                // Paladin Tier 6 Trinket (Ashtongue Talisman of Zeal)
                case 40470:
                {
                    if (!procSpell)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    float  chance;

                    // Flash of light/Holy light
                    if (procSpell->SpellClassMask & UI64LIT(0x00000000C0000000))
                    {
                        triggered_spell_id = 40471;
                        chance = 15.0f;
                    }
                    // Judgement
                    else if (procSpell->SpellClassMask & UI64LIT(0x0000000000800000))
                    {
                        triggered_spell_id = 40472;
                        chance = 50.0f;
                    }
                    else
                    {
                        return Combat::ProcResult::Failed;
                    }

                    if (!roll_chance_f(chance))
                    {
                        return Combat::ProcResult::Failed;
                    }

                    break;
                }
            }
            break;
        }
        case SPELLFAMILY_SHAMAN:
        {
            switch (dummySpell->ID)
            {
                    // Totemic Power (The Earthshatterer set)
                case 28823:
                {
                    if (!pVictim)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // Set class defined buff
                    switch (pVictim->getClass())
                    {
                        case CLASS_PALADIN:
                        case CLASS_PRIEST:
                        case CLASS_SHAMAN:
                        case CLASS_DRUID:
                            triggered_spell_id = 28824;     // Increases the friendly target's mana regeneration by $s1 per 5 sec. for $d.
                            break;
                        case CLASS_MAGE:
                        case CLASS_WARLOCK:
                            triggered_spell_id = 28825;     // Increases the friendly target's spell damage and healing by up to $s1 for $d.
                            break;
                        case CLASS_HUNTER:
                        case CLASS_ROGUE:
                            triggered_spell_id = 28826;     // Increases the friendly target's attack power by $s1 for $d.
                            break;
                        case CLASS_WARRIOR:
                            triggered_spell_id = 28827;     // Increases the friendly target's armor
                            break;
                        default:
                            return Combat::ProcResult::Failed;
                    }
                    break;
                }
                // Lesser Healing Wave (Totem of Flowing Water Relic)
                case 28849:
                {
                    target = this;
                    triggered_spell_id = 28850;
                    break;
                }
                // Windfury Weapon (Passive) 1-5 Ranks
                case 33757:
                {
                    if (GetTypeId() != TYPEID_PLAYER)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    if (!castItem || !castItem->IsEquipped())
                    {
                        return Combat::ProcResult::Failed;
                    }

                    // custom cooldown processing case
                    if (cooldown && ((Player*)this)->HasSpellCooldown(dummySpell->ID))
                    {
                        return Combat::ProcResult::Failed;
                    }

                    uint32 spellId;
                    switch (castItem->GetEnchantmentId(EnchantmentSlot(TEMP_ENCHANTMENT_SLOT)))
                    {
                        case 283: spellId = 33757; break;   // 1 Rank
                        case 284: spellId = 33756; break;   // 2 Rank
                        case 525: spellId = 33755; break;   // 3 Rank
                        case 1669:spellId = 33754; break;   // 4 Rank
                        case 2636:spellId = 33727; break;   // 5 Rank
                        default:
                        {
                            sLog.outError("Unit::HandleDummyAuraProc: non handled item enchantment (rank?) %u for spell id: %u (Windfury)",
                                          castItem->GetEnchantmentId(EnchantmentSlot(TEMP_ENCHANTMENT_SLOT)), dummySpell->ID);
                            return Combat::ProcResult::Failed;
                        }
                    }

                    SpellEntry const* windfurySpellEntry = sSpellStore.LookupEntry(spellId);
                    if (!windfurySpellEntry)
                    {
                        sLog.outError("Unit::HandleDummyAuraProc: nonexistent spell id: %u (Windfury)", spellId);
                        return Combat::ProcResult::Failed;
                    }

                    int32 extra_attack_power = CalculateSpellDamage(pVictim, windfurySpellEntry, EFFECT_INDEX_0);

                    // Off-Hand case
                    if (castItem->GetSlot() == EQUIPMENT_SLOT_OFFHAND)
                    {
                        // Value gained from additional AP
                        basepoints[0] = int32(extra_attack_power / 14.0f * GetAttackTime(OFF_ATTACK) / 1000 / 2);
                        triggered_spell_id = 33750;
                    }
                    // Main-Hand case
                    else
                    {
                        // Value gained from additional AP
                        basepoints[0] = int32(extra_attack_power / 14.0f * GetAttackTime(BASE_ATTACK) / 1000);
                        triggered_spell_id = 25504;
                    }

                    // apply cooldown before cast to prevent processing itself
                    if (cooldown)
                    {
                        ((Player*)this)->AddSpellCooldown(dummySpell->ID, 0, time(NULL) + cooldown);
                    }

                    // Attack twice, as two reactions rather than two calls.
                    //
                    // Inline, the loop held the victim across a cast that
                    // deals damage: the second swing of a Windfury that killed
                    // with the first was reading a unit DealDamage had already
                    // turned into a corpse. Re-resolving each time round made
                    // that safe and left the real problem -- the casts ran
                    // from inside the proc that raised them, so nothing they
                    // set off shared this swing's depth counter or its budget.
                    //
                    // Queued, they are ordinary reactions: resolved from their
                    // guids when they run, counted against the same limits as
                    // everything else, and unable to recurse into the handler
                    // that pushed them.
                    if (Map* map = GetMap())
                    {
                        Combat::ReactionQueue& queue =
                            map->CombatState().Queue();

                        Combat::ProcCast windfury;
                        windfury.spellId    = triggered_spell_id;
                        windfury.basePoints = basepoints[0];
                        windfury.triggered  = true;

                        for (uint32 i = 0; i < 2; ++i)
                        {
                            Combat::Reaction swing;
                            swing.source = GetObjectGuid();
                            swing.target = pVictim->GetObjectGuid();
                            swing.depth  = queue.NextDepth();
                            swing.what   = windfury;

                            queue.Push(swing);
                        }
                    }

                    return Combat::ProcResult::Ok;
                }
                // Shaman Tier 6 Trinket
                case 40463:
                {
                    if (!procSpell)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    float  chance;
                    if (procSpell->SpellClassMask & UI64LIT(0x0000000000000001))
                    {
                        triggered_spell_id = 40465;         // Lightning Bolt
                        chance = 15.0f;
                    }
                    else if (procSpell->SpellClassMask & UI64LIT(0x0000000000000080))
                    {
                        triggered_spell_id = 40465;         // Lesser Healing Wave
                        chance = 10.0f;
                    }
                    else if (procSpell->SpellClassMask & UI64LIT(0x0000001000000000))
                    {
                        triggered_spell_id = 40466;         // Stormstrike
                        chance = 50.0f;
                    }
                    else
                    {
                        return Combat::ProcResult::Failed;
                    }

                    if (!roll_chance_f(chance))
                    {
                        return Combat::ProcResult::Failed;
                    }

                    target = this;
                    break;
                }
            }

            // Earth Shield
            if (dummySpell->SpellClassMask & UI64LIT(0x0000040000000000))
            {
                if (GetTypeId() != TYPEID_PLAYER)
                {
                    return Combat::ProcResult::Failed;
                }

                // heal
                basepoints[0] = triggerAmount;
                target = this;
                triggered_spell_id = 379;
                break;
            }
            // Lightning Overload
            if (dummySpell->SpellIconID == 2018)            // only this spell have SpellFamily Shaman SpellIconID == 2018 and dummy aura
            {
                if (!procSpell || GetTypeId() != TYPEID_PLAYER || !pVictim)
                {
                    return Combat::ProcResult::Failed;
                }

                // custom cooldown processing case
                if (cooldown && GetTypeId() == TYPEID_PLAYER && ((Player*)this)->HasSpellCooldown(dummySpell->ID))
                {
                    return Combat::ProcResult::Failed;
                }

                uint32 spellId = 0;
                // Every Lightning Bolt and Chain Lightning spell have duplicate vs half damage and zero cost
                switch (procSpell->ID)
                {
                        // Lightning Bolt
                    case   403: spellId = 45284; break;     // Rank  1
                    case   529: spellId = 45286; break;     // Rank  2
                    case   548: spellId = 45287; break;     // Rank  3
                    case   915: spellId = 45288; break;     // Rank  4
                    case   943: spellId = 45289; break;     // Rank  5
                    case  6041: spellId = 45290; break;     // Rank  6
                    case 10391: spellId = 45291; break;     // Rank  7
                    case 10392: spellId = 45292; break;     // Rank  8
                    case 15207: spellId = 45293; break;     // Rank  9
                    case 15208: spellId = 45294; break;     // Rank 10
                    case 25448: spellId = 45295; break;     // Rank 11
                    case 25449: spellId = 45296; break;     // Rank 12
                        // Chain Lightning
                    case   421: spellId = 45297; break;     // Rank  1
                    case   930: spellId = 45298; break;     // Rank  2
                    case  2860: spellId = 45299; break;     // Rank  3
                    case 10605: spellId = 45300; break;     // Rank  4
                    case 25439: spellId = 45301; break;     // Rank  5
                    case 25442: spellId = 45302; break;     // Rank  6
                    default:
                        sLog.outError("Unit::HandleDummyAuraProc: non handled spell id: %u (LO)", procSpell->ID);
                        return Combat::ProcResult::Failed;
                }

                // Remove cooldown (Chain Lightning - have Category Recovery time)
                if (procSpell->SpellClassMask & UI64LIT(0x0000000000000002))
                {
                    ((Player*)this)->RemoveSpellCooldown(spellId);
                }

                CastSpell(pVictim, spellId, true, castItem, triggeredByAura);

                if (cooldown && GetTypeId() == TYPEID_PLAYER)
                {
                    ((Player*)this)->AddSpellCooldown(dummySpell->ID, 0, time(NULL) + cooldown);
                }

                return Combat::ProcResult::Ok;
            }
            break;
        }
        default:
            break;
    }

    if (!triggered_spell_id)
    {
        // Linked spells (Proc chain)
        SpellLinkedSet linkedSet = sSpellMgr.GetSpellLinked(dummySpell->ID, SPELL_LINKED_TYPE_PROC);
        if (linkedSet.size() > 0)
        {
            for (SpellLinkedSet::const_iterator itr = linkedSet.begin(); itr != linkedSet.end(); ++itr)
            {
                if (target == NULL)
                {
                    target = !(procFlag & PROC_FLAG_SUCCESSFUL_POSITIVE_SPELL) && IsPositiveSpell(*itr) ? this : pVictim;
                }
                CastSpell(this, *itr, true, castItem, triggeredByAura);
                if (cooldown && GetTypeId() == TYPEID_PLAYER)
                {
                    ((Player*)this)->AddSpellCooldown(*itr, 0, time(NULL) + cooldown);
                }
            }
        }
    }

    // processed charge only counting case
    if (!triggered_spell_id)
    {
        return Combat::ProcResult::Ok;
    }

    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(triggered_spell_id);

    if (!triggerEntry)
    {
        sLog.outError("Unit::HandleDummyAuraProc: Spell %u have nonexistent triggered spell %u", dummySpell->ID, triggered_spell_id);
        return Combat::ProcResult::Failed;
    }

    // default case
    if (!target || (target != this && !target->IsAlive()))
    {
        return Combat::ProcResult::Failed;
    }

    if (cooldown && GetTypeId() == TYPEID_PLAYER && ((Player*)this)->HasSpellCooldown(triggered_spell_id))
    {
        return Combat::ProcResult::Failed;
    }

    if (basepoints[EFFECT_INDEX_0] || basepoints[EFFECT_INDEX_1] || basepoints[EFFECT_INDEX_2])
        CastCustomSpell(target, triggered_spell_id,
                        basepoints[EFFECT_INDEX_0] ? &basepoints[EFFECT_INDEX_0] : NULL,
                        basepoints[EFFECT_INDEX_1] ? &basepoints[EFFECT_INDEX_1] : NULL,
                        basepoints[EFFECT_INDEX_2] ? &basepoints[EFFECT_INDEX_2] : NULL,
                        true, castItem, triggeredByAura);
    else
    {
        CastSpell(target, triggered_spell_id, true, castItem, triggeredByAura);
    }

    if (cooldown && GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)this)->AddSpellCooldown(triggered_spell_id, 0, time(NULL) + cooldown);
    }

    return Combat::ProcResult::Ok;
}

/**
 * @brief Handles proc-trigger-spell auras and resolves their triggered casts.
 *
 * @return SpellAuraProcResult The proc handling result.
 */
Combat::ProcResult Unit::HandleProcTriggerSpellAuraProc(Combat::ProcEvent const& e)
{
    Unit* const pVictim = e.target;
    Aura* const triggeredByAura = e.aura;
    SpellEntry const* const procSpell = e.procSpell;
    uint32 const damage = e.damage;
    uint32 const procFlags = e.flags;
    uint32 const cooldown = e.cooldown;

    // Get triggered aura spell info
    SpellEntry const* auraSpellInfo = triggeredByAura->GetSpellProto();

    // Basepoints of trigger aura
    int32 triggerAmount = triggeredByAura->GetModifier()->m_amount;

    // Set trigger spell id, target, custom basepoints
    uint32 trigger_spell_id = auraSpellInfo->EffectTriggerSpell[triggeredByAura->GetEffIndex()];
    Unit*  target = NULL;
    int32  basepoints[MAX_EFFECT_INDEX] = {0, 0, 0};

    if (triggeredByAura->GetModifier()->m_auraname == SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE)
    {
        basepoints[0] = triggerAmount;
    }

    Item* castItem = triggeredByAura->GetCastItemGuid() && GetTypeId() == TYPEID_PLAYER
                     ? ((Player*)this)->GetItemByGuid(triggeredByAura->GetCastItemGuid()) : NULL;

    // Try handle unknown trigger spells
    // Custom requirements (not listed in procEx) Warning! damage dealing after this
    // Custom triggered spells
    switch (auraSpellInfo->SpellClassSet)
    {
        case SPELLFAMILY_GENERIC:
            switch (auraSpellInfo->ID)
            {
                    // case 191:                            // Elemental Response
                    //    switch (procSpell->School)
                    //    {
                    //        case SPELL_SCHOOL_FIRE:  trigger_spell_id = 34192; break;
                    //        case SPELL_SCHOOL_FROST: trigger_spell_id = 34193; break;
                    //        case SPELL_SCHOOL_ARCANE:trigger_spell_id = 34194; break;
                    //        case SPELL_SCHOOL_NATURE:trigger_spell_id = 34195; break;
                    //        case SPELL_SCHOOL_SHADOW:trigger_spell_id = 34196; break;
                    //        case SPELL_SCHOOL_HOLY:  trigger_spell_id = 34197; break;
                    //        case SPELL_SCHOOL_NORMAL:trigger_spell_id = 34198; break;
                    //    }
                    //    break;
                    // case 5301:  break;                   // Defensive State (DND)
                    // case 7137:  break;                   // Shadow Charge (Rank 1)
                    // case 7377:  break;                   // Take Immune Periodic Damage <Not Working>
                    // case 13358: break;                   // Defensive State (DND)
                    // case 16092: break;                   // Defensive State (DND)
                    // case 18943: break;                   // Double Attack
                    // case 19194: break;                   // Double Attack
                    // case 19817: break;                   // Double Attack
                    // case 19818: break;                   // Double Attack
                    // case 22835: break;                   // Drunken Rage
                    //    trigger_spell_id = 14822; break;
                case 23780:                                 // Aegis of Preservation (Aegis of Preservation trinket)
                    trigger_spell_id = 23781;
                    break;
                case 24905:                                 // Moonkin Form (Passive)
                {
                    // Elune's Touch (instead non-existed triggered spell) 30% from AP
                    trigger_spell_id = 33926;
                    basepoints[0] = GetTotalAttackPowerValue(BASE_ATTACK) * 30 / 100;
                    target = this;
                    break;
                }
                    // case 24949: break;                       // Defensive State 2 (DND)
                case 27522:                                 // Mana Drain Trigger
                case 40336:                                 // Mana Drain Trigger
                case 46939:                                 // Black Bow of the Betrayer
                {
                    // On successful melee or ranged attack gain 8 mana and if possible drain 8 mana from the target.
                    if (IsAlive())
                    {
                        CastSpell(this, 29471, true, castItem, triggeredByAura);
                    }
                    if (pVictim && pVictim->IsAlive())
                    {
                        CastSpell(pVictim, 27526, true, castItem, triggeredByAura);
                    }
                    return Combat::ProcResult::Ok;
                }
                case 31255:                                 // Deadly Swiftness (Rank 1)
                {
                    // whenever you deal damage to a target who is below 20% health.
                    if (pVictim->GetHealth() > pVictim->GetMaxHealth() / 5)
                    {
                        return Combat::ProcResult::Failed;
                    }

                    target = this;
                    trigger_spell_id = 22588;
                    break;
                }
                // case 33207: break;                       // Gossip NPC Periodic - Fidget
                case 33896:                                 // Desperate Defense (Stonescythe Whelp, Stonescythe Alpha, Stonescythe Ambusher)
                    trigger_spell_id = 33898;
                    break;
                    // case 34082: break;                   // Advantaged State (DND)
                    // case 34783: break:                   // Spell Reflection
                    // case 35205: break:                   // Vanish
                    // case 35321: break;                   // Gushing Wound
                    // case 36096: break:                   // Spell Reflection
                    // case 36207: break:                   // Steal Weapon
                    // case 36576: break:                   // Shaleskin (Shaleskin Flayer, Shaleskin Ripper) 30023 trigger
                    // case 37030: break;                   // Chaotic Temperament
                    // case 38363: break;                   // Gushing Wound
                    // case 39215: break;                   // Gushing Wound
                    // case 40250: break;                   // Improved Duration
                    // case 40329: break;                   // Demo Shout Sensor
                    // case 40364: break;                   // Entangling Roots Sensor
                    // case 41054: break;                   // Copy Weapon
                    //    trigger_spell_id = 41055; break;
                    // case 41248: break;                   // Consuming Strikes
                    //    trigger_spell_id = 41249; break;
                    // case 43453: break:                   // Rune Ward
                    // case 43504: break;                   // Alterac Valley OnKill Proc Aura
                case 43820:                                 // Charm of the Witch Doctor (Amani Charm of the Witch Doctor trinket)
                {
                    // Pct value stored in dummy
                    basepoints[0] = pVictim->GetCreateHealth() * auraSpellInfo->CalculateSimpleValue(EFFECT_INDEX_1) / 100;
                    target = pVictim;
                    break;
                }
                // case 44326: break:                       // Pure Energy Passive
                // case 44526: break;                       // Hate Monster (Spar) (30 sec)
                // case 44527: break;                       // Hate Monster (Spar Buddy) (30 sec)
                // case 44819: break;                       // Hate Monster (Spar Buddy) (>30% Health)
                // case 44820: break;                       // Hate Monster (Spar) (<30%)
                case 45057:                                 // Evasive Maneuvers (Commendation of Kael`thas trinket)
                    // reduce you below $s1% health (in fact in this specific case can proc from any attack while health in result less $s1%)
                    if (int32(GetHealth()) - int32(damage) >= int32(GetMaxHealth() * triggerAmount / 100))
                    {
                        return Combat::ProcResult::Failed;
                    }
                    break;
                    // case 45205: break;                   // Copy Offhand Weapon
                    // case 45343: break;                   // Dark Flame Aura
                    // case 45903: break:                   // Offensive State
                    // case 46146: break:                   // [PH] Ahune  Spanky Hands
                    // case 46146: break;                   // [PH] Ahune  Spanky Hands
                    // case 47300: break;                   // Dark Flame Aura
                    // case 50051: break;                   // Ethereal Pet Aura
                    break;
            }
            break;
        case SPELLFAMILY_MAGE:
            if (auraSpellInfo->SpellIconID == 2127)         // Blazing Speed
            {
                switch (auraSpellInfo->ID)
                {
                    case 31641:  // Rank 1
                    case 31642:  // Rank 2
                        trigger_spell_id = 31643;
                        break;
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpellAuraProc: Spell %u miss possibly Blazing Speed", auraSpellInfo->ID);
                        return Combat::ProcResult::Failed;
                }
            }
            else if (auraSpellInfo->ID == 26467)            // Persistent Shield (Scarab Brooch trinket)
            {
                // This spell originally trigger 13567 - Dummy Trigger (vs dummy effect)
                basepoints[0] = damage * 15 / 100;
                target = pVictim;
                trigger_spell_id = 26470;
            }
            break;
        case SPELLFAMILY_WARRIOR:
            // Deep Wounds (replace triggered spells to directly apply DoT), dot spell have familyflags
            if (!auraSpellInfo->SpellClassMask && auraSpellInfo->SpellIconID == 243)
            {
                float weaponDamage;
                // DW should benefit of attack power, damage percent mods etc.
                // TODO: check if using offhand damage is correct and if it should be divided by 2
                if (haveOffhandWeapon() && getAttackTimer(BASE_ATTACK) > getAttackTimer(OFF_ATTACK))
                {
                    weaponDamage = (GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE) + GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE)) / 2;
                }
                else
                {
                    weaponDamage = (GetFloatValue(UNIT_FIELD_MINDAMAGE) + GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2;
                }

                switch (auraSpellInfo->ID)
                {
                    case 12834: basepoints[0] = int32(weaponDamage * 0.2f); break;
                    case 12849: basepoints[0] = int32(weaponDamage * 0.4f); break;
                    case 12867: basepoints[0] = int32(weaponDamage * 0.6f); break;
                        // Impossible case
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpellAuraProc: DW unknown spell rank %u", auraSpellInfo->ID);
                        return Combat::ProcResult::Failed;
                }

                // 1 tick/sec * 6 sec = 6 ticks
                basepoints[0] /= 6;

                trigger_spell_id = 12721;
                break;
            }
            // Rampage
            else if (auraSpellInfo->SpellIconID == 2006 && auraSpellInfo->IsFitToFamilyMask(UI64LIT(0x0000000000100000)))
            {
                switch (auraSpellInfo->ID)
                {
                    case 29801: trigger_spell_id = 30029; break;       // Rank 1
                    case 30030: trigger_spell_id = 30031; break;       // Rank 2
                    case 30033: trigger_spell_id = 30032; break;       // Rank 3
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpellAuraProc: Spell %u not handled in Rampage", auraSpellInfo->ID);
                        return Combat::ProcResult::Failed;
                }
            }
            break;
        case SPELLFAMILY_WARLOCK:
        {
            // Pyroclasm
            if (auraSpellInfo->SpellIconID == 1137)
            {
                if (!pVictim || !pVictim->IsAlive() || pVictim == this || procSpell == NULL)
                {
                    return Combat::ProcResult::Failed;
                }
                // Calculate spell tick count for spells
                uint32 tick = 1; // Default tick = 1

                // Hellfire have 15 tick
                if (procSpell->SpellClassMask & UI64LIT(0x0000000000000040))
                {
                    tick = 15;
                }
                // Rain of Fire have 4 tick
                else if (procSpell->SpellClassMask & UI64LIT(0x0000000000000020))
                {
                    tick = 4;
                }
                else
                {
                    return Combat::ProcResult::Failed;
                }

                // Calculate chance = baseChance / tick
                float chance = 0;
                switch (auraSpellInfo->ID)
                {
                    case 18096: chance = 13.0f / tick; break;
                    case 18073: chance = 26.0f / tick; break;
                }
                // Roll chance
                if (!roll_chance_f(chance))
                {
                    return Combat::ProcResult::Failed;
                }

                trigger_spell_id = 18093;
            }
            // Drain Soul
            else if (auraSpellInfo->SpellClassMask & UI64LIT(0x0000000000004000))
            {
                Unit::AuraList const& mAddFlatModifier = GetAurasByType(SPELL_AURA_ADD_FLAT_MODIFIER);
                for (Unit::AuraList::const_iterator i = mAddFlatModifier.begin(); i != mAddFlatModifier.end(); ++i)
                {
                    if ((*i)->GetModifier()->m_miscvalue == SPELLMOD_CHANCE_OF_SUCCESS && (*i)->GetSpellProto()->SpellIconID == 113)
                    {
                        // basepoints of trigger spell stored in dummyeffect of spellProto
                        int32 basepoints = GetMaxPower(POWER_MANA) * (*i)->GetSpellProto()->CalculateSimpleValue(EFFECT_INDEX_2) / 100;
                        CastCustomSpell(this, 18371, &basepoints, NULL, NULL, true, castItem, triggeredByAura);
                        break;
                    }
                }
                // Not remove charge (aura removed on death in any cases)
                // Need for correct work Drain Soul SPELL_AURA_CHANNEL_DEATH_ITEM aura
                return Combat::ProcResult::Failed;
            }
            // Cheat Death
            else if (auraSpellInfo->ID == 28845)
            {
                // When your health drops below 20% ....
                int32 health20 = int32(GetMaxHealth()) / 5;
                if (int32(GetHealth()) - int32(damage) >= health20 || int32(GetHealth()) < health20)
                {
                    return Combat::ProcResult::Failed;
                }
            }
            break;
        }
        case SPELLFAMILY_PRIEST:
        {
            // Greater Heal Refund (Avatar Raiment set)
            if (auraSpellInfo->ID == 37594)
            {
                // Not give if target already have full health
                if (pVictim->GetHealth() == pVictim->GetMaxHealth())
                {
                    return Combat::ProcResult::Failed;
                }
                // If your Greater Heal brings the target to full health, you gain $37595s1 mana.
                if (pVictim->GetHealth() + damage < pVictim->GetMaxHealth())
                {
                    return Combat::ProcResult::Failed;
                }
                trigger_spell_id = 37595;
            }
            // Shadowguard
            else if (auraSpellInfo->SpellIconID == 19)
            {
                switch (auraSpellInfo->ID)
                {
                    case 18137: trigger_spell_id = 28377; break;   // Rank 1
                    case 19308: trigger_spell_id = 28378; break;   // Rank 2
                    case 19309: trigger_spell_id = 28379; break;   // Rank 3
                    case 19310: trigger_spell_id = 28380; break;   // Rank 4
                    case 19311: trigger_spell_id = 28381; break;   // Rank 5
                    case 19312: trigger_spell_id = 28382; break;   // Rank 6
                    case 25477: trigger_spell_id = 28385; break;   // Rank 7
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpellAuraProc: Spell %u not handled in SG", auraSpellInfo->ID);
                        return Combat::ProcResult::Failed;
                }
            }
            // Blessed Recovery
            else if (auraSpellInfo->SpellIconID == 1875)
            {
                switch (auraSpellInfo->ID)
                {
                    case 27811: trigger_spell_id = 27813; break;
                    case 27815: trigger_spell_id = 27817; break;
                    case 27816: trigger_spell_id = 27818; break;
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpellAuraProc: Spell %u not handled in BR", auraSpellInfo->ID);
                        return Combat::ProcResult::Failed;
                }
                basepoints[0] = damage * triggerAmount / 100 / 3;
                target = this;
            }
            break;
        }
        case SPELLFAMILY_DRUID:
        {
            // Leader of the Pack
            if (auraSpellInfo->ID == 24932)
            {
                if (triggerAmount == 0)
                {
                    return Combat::ProcResult::Failed;
                }
                basepoints[0] = triggerAmount * GetMaxHealth() / 100;
                trigger_spell_id = 34299;
            }
            // Druid Forms Trinket
            else if (auraSpellInfo->ID == 37336)
            {
                switch (GetShapeshiftForm())
                {
                    case FORM_NONE:     trigger_spell_id = 37344; break;
                    case FORM_CAT:      trigger_spell_id = 37341; break;
                    case FORM_BEAR:
                    case FORM_DIREBEAR: trigger_spell_id = 37340; break;
                    case FORM_TREE:     trigger_spell_id = 37342; break;
                    case FORM_MOONKIN:  trigger_spell_id = 37343; break;
                    default:
                        return Combat::ProcResult::Failed;
                }
            }
            break;
        }
        case SPELLFAMILY_ROGUE:
        {
            if (auraSpellInfo->SpellIconID == 2260)         // Combat Potency
            {
                if (!(procFlags & PROC_FLAG_SUCCESSFUL_OFFHAND_HIT))
                {
                    return Combat::ProcResult::Failed;
                }
            }

            break;
        }
        case SPELLFAMILY_HUNTER:
            break;
        case SPELLFAMILY_PALADIN:
        {
            /*
            // Blessed Life
            if (auraSpellInfo->SpellIconID == 2137)
            {
                switch (auraSpellInfo->Id)
                {
                    case 31828:                         // Rank 1
                    case 31829:                         // Rank 2
                    case 31830:                         // Rank 3
                        break;
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpellAuraProc: Spell %u miss posibly Blessed Life", auraSpellInfo->Id);
                        return Combat::ProcResult::Failed;
                }
            }
            */
            // Healing Discount
            if (auraSpellInfo->ID == 37705)
            {
                trigger_spell_id = 37706;
                target = this;
            }
            // Judgement of Light and Judgement of Wisdom
            else if (auraSpellInfo->SpellClassMask & UI64LIT(0x0000000000080000))
            {
                switch (auraSpellInfo->ID)
                {
                        // Judgement of Light
                    case 20185: trigger_spell_id = 20267; break; // Rank 1
                    case 20344: trigger_spell_id = 20341; break; // Rank 2
                    case 20345: trigger_spell_id = 20342; break; // Rank 3
                    case 20346: trigger_spell_id = 20343; break; // Rank 4
                    case 27162: trigger_spell_id = 27163; break; // Rank 5
                        // Judgement of Wisdom
                    case 20186: trigger_spell_id = 20268; break; // Rank 1
                    case 20354: trigger_spell_id = 20352; break; // Rank 2
                    case 20355: trigger_spell_id = 20353; break; // Rank 3
                    case 27164: trigger_spell_id = 27165; break; // Rank 4
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpellAuraProc: Spell %u miss posibly Judgement of Light/Wisdom", auraSpellInfo->ID);
                        return Combat::ProcResult::Failed;
                }
                pVictim->CastSpell(pVictim, trigger_spell_id, true, castItem, triggeredByAura);
                return Combat::ProcResult::Ok;                  // no hidden cooldown
            }
            // Illumination
            else if (auraSpellInfo->SpellIconID == 241)
            {
                if (!procSpell)
                {
                    return Combat::ProcResult::Failed;
                }
                // procspell is triggered spell but we need mana cost of original casted spell
                uint32 originalSpellId = procSpell->ID;
                // Holy Shock heal
                if (procSpell->SpellClassMask & UI64LIT(0x0001000000000000))
                {
                    switch (procSpell->ID)
                    {
                        case 25914: originalSpellId = 20473; break;
                        case 25913: originalSpellId = 20929; break;
                        case 25903: originalSpellId = 20930; break;
                        case 27175: originalSpellId = 27174; break;
                        case 33074: originalSpellId = 33072; break;
                        default:
                            sLog.outError("Unit::HandleProcTriggerSpellAuraProc: Spell %u not handled in HShock", procSpell->ID);
                            return Combat::ProcResult::Failed;
                    }
                }
                SpellEntry const* originalSpell = sSpellStore.LookupEntry(originalSpellId);
                if (!originalSpell)
                {
                    sLog.outError("Unit::HandleProcTriggerSpellAuraProc: Spell %u unknown but selected as original in Illu", originalSpellId);
                    return Combat::ProcResult::Failed;
                }
                // percent stored in effect 1 (class scripts) base points
                int32 cost = originalSpell->ManaCost;
                basepoints[0] = cost * auraSpellInfo->CalculateSimpleValue(EFFECT_INDEX_1) / 100;
                trigger_spell_id = 20272;
                target = this;
            }
            // Lightning Capacitor
            else if (auraSpellInfo->ID == 37657)
            {
                if (!pVictim || !pVictim->IsAlive())
                {
                    return Combat::ProcResult::Failed;
                }
                // stacking
                CastSpell(this, 37658, true, NULL, triggeredByAura);

                Aura* dummy = GetDummyAura(37658);
                // release at 3 aura in stack (cont contain in basepoint of trigger aura)
                if (!dummy || dummy->GetStackAmount() < uint32(triggerAmount))
                {
                    return Combat::ProcResult::Failed;
                }

                RemoveAurasDueToSpell(37658);
                trigger_spell_id = 37661;
                target = pVictim;
            }
            // Bonus Healing (Crystal Spire of Karabor mace)
            else if (auraSpellInfo->ID == 40971)
            {
                // If your target is below $s1% health
                if (pVictim->GetHealth() > pVictim->GetMaxHealth() * triggerAmount / 100)
                {
                    return Combat::ProcResult::Failed;
                }
            }
            break;
        }
        case SPELLFAMILY_SHAMAN:
        {
            // Lightning Shield (overwrite non existing triggered spell call in spell.dbc
            if (auraSpellInfo->IsFitToFamilyMask(UI64LIT(0x0000000000000400)) && auraSpellInfo->SpellVisualID == 37)
            {
                switch (auraSpellInfo->ID)
                {
                    case 324:                           // Rank 1
                        trigger_spell_id = 26364; break;
                    case 325:                           // Rank 2
                        trigger_spell_id = 26365; break;
                    case 905:                           // Rank 3
                        trigger_spell_id = 26366; break;
                    case 945:                           // Rank 4
                        trigger_spell_id = 26367; break;
                    case 8134:                          // Rank 5
                        trigger_spell_id = 26369; break;
                    case 10431:                         // Rank 6
                        trigger_spell_id = 26370; break;
                    case 10432:                         // Rank 7
                        trigger_spell_id = 26363; break;
                    case 25469:                         // Rank 8
                        trigger_spell_id = 26371; break;
                    case 25472:                         // Rank 9
                        trigger_spell_id = 26372; break;
                    default:
                        sLog.outError("Unit::HandleProcTriggerSpellAuraProc: Spell %u not handled in LShield", auraSpellInfo->ID);
                        return Combat::ProcResult::Failed;
                }
            }
            // Lightning Shield (The Ten Storms set)
            else if (auraSpellInfo->ID == 23551)
            {
                trigger_spell_id = 23552;
                target = pVictim;
            }
            // Damage from Lightning Shield (The Ten Storms set)
            else if (auraSpellInfo->ID == 23552)
            {
                trigger_spell_id = 27635;
            }
            // Mana Surge (The Earthfury set)
            else if (auraSpellInfo->ID == 23572)
            {
                if (!procSpell)
                {
                    return Combat::ProcResult::Failed;
                }
                basepoints[0] = procSpell->ManaCost * 35 / 100;
                trigger_spell_id = 23571;
                target = this;
            }
            // Nature's Guardian
            else if (auraSpellInfo->SpellIconID == 2013)
            {
                // Check health condition - should drop to less 30% (trigger at any attack with result health less 30%, independent original health state)
                int32 health30 = int32(GetMaxHealth()) * 3 / 10;
                if (int32(GetHealth()) - int32(damage) >= health30)
                {
                    return Combat::ProcResult::Failed;
                }

                if (pVictim && pVictim->IsAlive())
                {
                    pVictim->GetThreatManager().modifyThreatPercent(this, -10);
                }

                basepoints[0] = triggerAmount * GetMaxHealth() / 100;
                trigger_spell_id = 31616;
                target = this;
            }
            break;
        }
        default:
            break;
    }

    // All ok. Check current trigger spell
    SpellEntry const* triggerEntry = sSpellStore.LookupEntry(trigger_spell_id);
    if (!triggerEntry)
    {
        // Not cast unknown spell
        // sLog.outError("Unit::HandleProcTriggerSpellAuraProc: Spell %u have 0 in EffectTriggered[%d], not handled custom case?",auraSpellInfo->Id,triggeredByAura->GetEffIndex());
        return Combat::ProcResult::Failed;
    }

    // not allow proc extra attack spell at extra attack
    if (m_extraAttacks && IsSpellHaveEffect(triggerEntry, SPELL_EFFECT_ADD_EXTRA_ATTACKS))
    {
        return Combat::ProcResult::Failed;
    }

    // Custom basepoints/target for exist spell
    // dummy basepoints or other customs
    switch (trigger_spell_id)
    {
            // Cast positive spell on enemy target
        case 7099:  // Curse of Mending
        case 39647: // Curse of Mending
        case 29494: // Temptation
        case 20233: // Improved Lay on Hands (cast on target)
        {
            target = pVictim;
            break;
        }
        // Combo points add triggers (need add combopoint only for main target, and after possible combopoints reset)
        case 15250: // Rogue Setup
        {
            if (!pVictim || pVictim != getVictim())  // applied only for main target
            {
                return Combat::ProcResult::Failed;
            }
            break;                                   // continue normal case
        }
        // Finishing moves that add combo points
        case 14189: // Seal Fate (Netherblade set)
        case 14157: // Ruthlessness
        {
            // Need add combopoint AFTER finishing move (or they get dropped in finish phase)
            if (Spell* spell = GetCurrentSpell(CURRENT_GENERIC_SPELL))
            {
                spell->AddTriggeredSpell(trigger_spell_id);
                return Combat::ProcResult::Ok;
            }
            return Combat::ProcResult::Failed;
        }
        // Shamanistic Rage triggered spell
        case 30824:
        {
            basepoints[0] = int32(GetTotalAttackPowerValue(BASE_ATTACK) * triggerAmount / 100);
            break;
        }
        // Enlightenment (trigger only from mana cost spells)
        case 35095:
        {
            if (!procSpell || procSpell->PowerType != POWER_MANA || (procSpell->ManaCost == 0 && procSpell->ManaCostPct == 0 && procSpell->ManaCostPerLevel == 0))
            {
                return Combat::ProcResult::Failed;
            }
            break;
        }
    }

    if (!trigger_spell_id)
    {
        // Linked spells (Proc chain)
        SpellLinkedSet linkedSet = sSpellMgr.GetSpellLinked(auraSpellInfo->ID, SPELL_LINKED_TYPE_PROC);
        if (linkedSet.size() > 0)
        {
            for (SpellLinkedSet::const_iterator itr = linkedSet.begin(); itr != linkedSet.end(); ++itr)
            {
                if (target == NULL)
                {
                    target = !(procFlags & PROC_FLAG_SUCCESSFUL_POSITIVE_SPELL) && IsPositiveSpell(*itr) ? this : pVictim;
                }
                CastSpell(target, *itr, true, castItem, triggeredByAura);
                if (cooldown && GetTypeId() == TYPEID_PLAYER)
                {
                    ((Player*)this)->AddSpellCooldown(*itr, 0, time(NULL) + cooldown);
                }
            }
        }
    }

    if (cooldown && GetTypeId() == TYPEID_PLAYER && ((Player*)this)->HasSpellCooldown(trigger_spell_id))
    {
        return Combat::ProcResult::Failed;
    }

    // try detect target manually if not set
    if (target == NULL)
    {
        target = !(procFlags & PROC_FLAG_SUCCESSFUL_POSITIVE_SPELL) && IsPositiveSpell(trigger_spell_id) ? this : pVictim;
    }

    // default case
    if (!target || (target != this && !target->IsAlive()))
    {
        return Combat::ProcResult::Failed;
    }

    if (basepoints[EFFECT_INDEX_0] || basepoints[EFFECT_INDEX_1] || basepoints[EFFECT_INDEX_2])
        CastCustomSpell(target, trigger_spell_id,
                        basepoints[EFFECT_INDEX_0] ? &basepoints[EFFECT_INDEX_0] : NULL,
                        basepoints[EFFECT_INDEX_1] ? &basepoints[EFFECT_INDEX_1] : NULL,
                        basepoints[EFFECT_INDEX_2] ? &basepoints[EFFECT_INDEX_2] : NULL,
                        true, castItem, triggeredByAura);
    else
    {
        CastSpell(target, trigger_spell_id, true, castItem, triggeredByAura);
    }

    if (cooldown && GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)this)->AddSpellCooldown(trigger_spell_id, 0, time(NULL) + cooldown);
    }

    return Combat::ProcResult::Ok;
}

