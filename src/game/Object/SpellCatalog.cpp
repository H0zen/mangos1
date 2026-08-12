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

/**
 * @file SpellCatalog.cpp
 * @brief Boot-time derivation of per-spell facts, and the catalog that holds them.
 *
 * The four Derive* functions carry bodies moved verbatim out of SpellMgr.cpp --
 * IsPositiveTarget, IsPositiveEffect, IsPassiveSpell and GetSpellSpecific -- so
 * that the classification a spell gets here is bit-for-bit the one it got
 * before. Only the plumbing changed: the DBC store lookup inside the recursive
 * case became a caller-supplied resolve callback, and the spell_elixir query
 * became an elixirMask argument. Both were the only two reasons those functions
 * could not stand alone.
 *
 * Nothing in this file may include Unit, Player, ObjectMgr or the database.
 * src/tests compiles it directly, and it links no server object at all.
 *
 * @see SpellCatalog.h for why the boundary is drawn here
 */

#include "SpellCatalog.h"

#include <algorithm>

namespace
{
    /// How far DeriveIsPositiveEffect will follow a chain of triggered spells.
    const uint32 MAX_POSITIVE_TRIGGER_DEPTH = 4;

    /// How many truncations get recorded in detail before only the count grows.
    const size_t MAX_RECORDED_TRUNCATIONS = 16;

    /// Written from Build() only, which is single-threaded at start-up.
    uint32 g_truncationCount = 0;
    std::vector<SpellTriggerTruncation> g_truncations;

    /**
     * @brief Maps a spell_elixir mask onto an exclusion class.
     *
     * Moved from SpellMgr::GetSpellElixirSpecific, which took a spell id and did
     * the map lookup itself. The lookup now happens in the caller so this stays
     * pure.
     */
    SpellSpecific ElixirSpecificFromMask(uint32 mask)
    {
        // flasks must have all bits set from ELIXIR_FLASK_MASK
        if ((mask & ELIXIR_FLASK_MASK) == ELIXIR_FLASK_MASK)
        {
            return SPELL_FLASK_ELIXIR;
        }
        else if (mask & ELIXIR_BATTLE_MASK)
        {
            return SPELL_BATTLE_ELIXIR;
        }
        else if (mask & ELIXIR_GUARDIAN_MASK)
        {
            return SPELL_GUARDIAN_ELIXIR;
        }
        else if (mask & ELIXIR_WELL_FED)
        {
            return SPELL_WELL_FED;
        }
        else
        {
            return SPELL_NORMAL;
        }
    }
}

bool DeriveIsPositiveTarget(uint32 targetA, uint32 targetB)
{
    switch (targetA)
    {
            // non-positive targets
        case TARGET_CHAIN_DAMAGE:
        case TARGET_ALL_ENEMY_IN_AREA:
        case TARGET_ALL_ENEMY_IN_AREA_INSTANT:
        case TARGET_IN_FRONT_OF_CASTER:
        case TARGET_ALL_ENEMY_IN_AREA_CHANNELED:
        case TARGET_CURRENT_ENEMY_COORDINATES:
        case TARGET_SINGLE_ENEMY:
            return false;
            // positive or dependent
        case TARGET_CASTER_COORDINATES:
            return (targetB == TARGET_ALL_PARTY || targetB == TARGET_ALL_FRIENDLY_UNITS_AROUND_CASTER);
        default:
            break;
    }
    if (targetB)
    {
        return DeriveIsPositiveTarget(targetB, 0);
    }
    return true;
}

bool DeriveIsPassiveSpell(SpellEntry const* spellInfo)
{
    return spellInfo->HasAttribute(SPELL_ATTR_PASSIVE);
}

SpellSpecific DeriveSpellSpecific(SpellEntry const* spellInfo, uint32 elixirMask)
{
    switch (spellInfo->SpellClassSet)
    {
        case SPELLFAMILY_GENERIC:
        {
            // Aspect of the Beast
            if (spellInfo->ID == 13161)
            {
                return SPELL_ASPECT;
            }

            // Food / Drinks (mostly)
            if (spellInfo->AuraInterruptFlags & AURA_INTERRUPT_FLAG_NOT_SEATED)
            {
                bool food = false;
                bool drink = false;
                for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    switch (spellInfo->EffectAura[i])
                    {
                            // Food
                        case SPELL_AURA_MOD_REGEN:
                        case SPELL_AURA_OBS_MOD_HEALTH:
                            food = true;
                            break;
                            // Drink
                        case SPELL_AURA_MOD_POWER_REGEN:
                        case SPELL_AURA_OBS_MOD_MANA:
                            drink = true;
                            break;
                        default:
                            break;
                    }
                }

                if (food && drink)
                {
                    return SPELL_FOOD_AND_DRINK;
                }
                else if (food)
                {
                    return SPELL_FOOD;
                }
                else if (drink)
                {
                    return SPELL_DRINK;
                }
            }
            else
            {
                // Well Fed buffs (must be exclusive with Food / Drink replenishment effects, or else Well Fed will cause them to be removed)
                // SpellIcon 2560 is Spell 46687, does not have this flag
                if (spellInfo->HasAttribute(SPELL_ATTR_EX2_FOOD_BUFF) || spellInfo->SpellIconID == 2560)
                {
                    return SPELL_WELL_FED;
                }
            }
            break;
        }
        case SPELLFAMILY_MAGE:
        {
            // family flags 18(Molten), 25(Frost/Ice), 28(Mage)
            if (spellInfo->SpellClassMask & UI64LIT(0x12040000))
            {
                return SPELL_MAGE_ARMOR;
            }

            if ((spellInfo->SpellClassMask & UI64LIT(0x1000000)) && spellInfo->EffectAura[EFFECT_INDEX_0] == SPELL_AURA_MOD_CONFUSE)
            {
                return SPELL_MAGE_POLYMORPH;
            }

            break;
        }
        case SPELLFAMILY_WARRIOR:
        {
            if (spellInfo->SpellClassMask & UI64LIT(0x00008000010000))
            {
                return SPELL_POSITIVE_SHOUT;
            }

            break;
        }
        case SPELLFAMILY_WARLOCK:
        {
            // only warlock curses have this
            if (spellInfo->DispelType == DISPEL_CURSE)
            {
                return SPELL_CURSE;
            }

            // family flag 37 (only part spells have family name)
            if (spellInfo->IsFitToFamilyMask(UI64LIT(0x0000002000000000)))
            {
                return SPELL_WARLOCK_ARMOR;
            }

            break;
        }
        case SPELLFAMILY_PRIEST:
        {
            // "Well Fed" buff from Blessed Sunfruit, Blessed Sunfruit Juice, Alterac Spring Water
            if (spellInfo->HasAttribute(SPELL_ATTR_CASTABLE_WHILE_SITTING) &&
                (spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_AUTOATTACK) &&
                (spellInfo->SpellIconID == 52 || spellInfo->SpellIconID == 79))
                {
                    return SPELL_WELL_FED;
                }
            break;
        }
        case SPELLFAMILY_HUNTER:
        {
            // only hunter stings have this
            if (spellInfo->DispelType == DISPEL_POISON)
            {
                return SPELL_STING;
            }

            // only hunter aspects have this (one have generic family)
            if (spellInfo->IsFitToFamilyMask(UI64LIT(0x0044000000380000)))
            {
                return SPELL_ASPECT;
            }

            break;
        }
        case SPELLFAMILY_PALADIN:
        {
            if (IsSealSpell(spellInfo))
            {
                return SPELL_SEAL;
            }

            if (spellInfo->IsFitToFamilyMask(UI64LIT(0x0000000010000100)))
            {
                return SPELL_BLESSING;
            }

            if (spellInfo->IsFitToFamilyMask(UI64LIT(0x00000820180400)) && spellInfo->HasAttribute(SPELL_ATTR_EX3_TRIGGERED_CAN_TRIGGER_SPECIAL))
            {
                return SPELL_JUDGEMENT;
            }

            for (int i = 0; i < 3; ++i)
            {
                // only paladin auras have this
                if (spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AREA_AURA_PARTY)
                {
                    return SPELL_AURA;
                }
            }
            break;
        }
        case SPELLFAMILY_SHAMAN:
        {
            if (IsElementalShield(spellInfo))
            {
                return SPELL_ELEMENTAL_SHIELD;
            }

            break;
        }

        case SPELLFAMILY_POTION:
            return ElixirSpecificFromMask(elixirMask);
    }

    // only warlock armor/skin have this (in additional to family cases)
    if (spellInfo->SpellVisualID == 130 && spellInfo->SpellIconID == 89)
    {
        return SPELL_WARLOCK_ARMOR;
    }

    // Tracking spells (exclude Well Fed, some other always allowed cases)
    if ((IsSpellHaveAura(spellInfo, SPELL_AURA_TRACK_CREATURES) ||
         IsSpellHaveAura(spellInfo, SPELL_AURA_TRACK_RESOURCES)  ||
         IsSpellHaveAura(spellInfo, SPELL_AURA_TRACK_STEALTHED)) &&
            (spellInfo->HasAttribute(SPELL_ATTR_EX_UNAUTOCASTABLE_BY_CHARMED) || spellInfo->HasAttribute(SPELL_ATTR_EX6_UNK10)))
         {
             return SPELL_TRACKER;
         }

    // elixirs can have different families, but potion most ofc.
    if (SpellSpecific sp = ElixirSpecificFromMask(elixirMask))
    {
        return sp;
    }

    return SPELL_NORMAL;
}

bool DeriveIsPositiveEffect(SpellEntry const* spellproto, SpellEffectIndex effIndex,
                            SpellEntry const* (*resolve)(void const* ctx, uint32 spellId),
                            void const* ctx, uint32 depth)
{
    switch (spellproto->Effect[effIndex])
    {
        case SPELL_EFFECT_DUMMY:
            // some explicitly required dummy effect sets
            switch (spellproto->ID)
            {
                case 28441:                                 // AB Effect 000
                    return false;
                case 10258:                                 // Awaken Vault Warder
                case 18153:                                 // Kodo Kombobulator
                case 32312:                                 // Move 1
                case 37388:                                 // Move 2
                    return true;
                default:
                    break;
            }
            break;
        case SPELL_EFFECT_SCRIPT_EFFECT:
            // some explicitly required script effect sets
            switch (spellproto->ID)
            {
                case 46650:                                 // Open Brutallus Back Door
                    return true;
                case 5249 : // Ice Block - fixed trap dire maul
                    return false;
                default:
                    break;
            }
            break;
            // always positive effects (check before target checks that provided non-positive result in some case for positive effects)
        case SPELL_EFFECT_HEAL:
        case SPELL_EFFECT_LEARN_SPELL:
        case SPELL_EFFECT_SKILL_STEP:
        case SPELL_EFFECT_HEAL_PCT:
        case SPELL_EFFECT_ENERGIZE_PCT:
        case SPELL_EFFECT_QUEST_COMPLETE:
        case SPELL_EFFECT_KILL_CREDIT_GROUP:
            return true;

            // non-positive aura use
        case SPELL_EFFECT_APPLY_AURA:
        case SPELL_EFFECT_APPLY_AREA_AURA_FRIEND:
        {
            switch (spellproto->EffectAura[effIndex])
            {
                case SPELL_AURA_DUMMY:
                {
                    // dummy aura can be positive or negative dependent from casted spell
                    switch (spellproto->ID)
                    {
                        case 13139:                         // net-o-matic special effect
                        case 18172:                         // Quest Kodo Roundup player debuff
                        case 23445:                         // evil twin
                        case 35679:                         // Protectorate Demolitionist
                        case 37695:                         // Stanky
                        case 38637:                         // Nether Exhaustion (red)
                        case 38638:                         // Nether Exhaustion (green)
                        case 38639:                         // Nether Exhaustion (blue)
                        case 44689:                         // Relay Race Accept Hidden Debuff - DND
                            return false;
                            // some spells have unclear target modes for selection, so just make effect positive
                        case 27184:
                        case 27190:
                        case 27191:
                        case 27201:
                        case 27202:
                        case 27203:
                            return true;
                        default:
                            break;
                    }
                }   break;
                case SPELL_AURA_MOD_DAMAGE_DONE:            // dependent from base point sign (negative -> negative)
                case SPELL_AURA_MOD_RESISTANCE:
                case SPELL_AURA_MOD_STAT:
                case SPELL_AURA_MOD_SKILL:
                case SPELL_AURA_MOD_DODGE_PERCENT:
                case SPELL_AURA_MOD_HEALING_PCT:
                case SPELL_AURA_MOD_HEALING_DONE:
                    if (spellproto->CalculateSimpleValue(effIndex) < 0)
                    {
                        return false;
                    }
                    break;
                case SPELL_AURA_MOD_DAMAGE_TAKEN:           // dependent from bas point sign (positive -> negative)
                case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
                    if (spellproto->CalculateSimpleValue(effIndex) < 0)
                    {
                        return true;
                    }
                    // let check by target modes (for Amplify Magic cases/etc)
                    break;
                case SPELL_AURA_MOD_SPELL_CRIT_CHANCE:
                case SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT:
                case SPELL_AURA_MOD_DAMAGE_PERCENT_DONE:
                    if (spellproto->CalculateSimpleValue(effIndex) > 0)
                    {
                        return true;                         // some expected positive spells have SPELL_ATTR_EX_NEGATIVE or unclear target modes
                    }
                    break;
                case SPELL_AURA_ADD_TARGET_TRIGGER:
                    return true;
                case SPELL_AURA_PERIODIC_TRIGGER_SPELL:
                    if (spellproto->ID != spellproto->EffectTriggerSpell[effIndex])
                    {
                        uint32 spellTriggeredId = spellproto->EffectTriggerSpell[effIndex];

                        if (resolve && depth >= MAX_POSITIVE_TRIGGER_DEPTH)
                        {
                            ++g_truncationCount;
                            if (g_truncations.size() < MAX_RECORDED_TRUNCATIONS)
                            {
                                SpellTriggerTruncation cut;
                                cut.spellId = spellproto->ID;
                                cut.effIndex = uint32(effIndex);
                                cut.triggeredId = spellTriggeredId;
                                g_truncations.push_back(cut);
                            }
                        }

                        SpellEntry const* spellTriggeredProto =
                            (resolve && depth < MAX_POSITIVE_TRIGGER_DEPTH)
                            ? resolve(ctx, spellTriggeredId) : NULL;

                        // Only a direct self-trigger is excluded above, so a
                        // two-spell cycle would recurse without end. That never
                        // mattered while this ran per query on whatever the game
                        // happened to ask about; it matters now that the catalog
                        // evaluates every row at boot. Past the cap we decline to
                        // judge the chain, which is the same answer an unknown
                        // triggered spell already produced.
                        if (spellTriggeredProto)
                        {
                            // non-positive targets of main spell return early
                            for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
                            {
                                // if non-positive trigger cast targeted to positive target this main cast is non-positive
                                // this will place this spell auras as debuffs
                                if (spellTriggeredProto->Effect[i] &&
                                    DeriveIsPositiveTarget(spellTriggeredProto->ImplicitTargetA[i], spellTriggeredProto->ImplicitTargetB[i]) &&
                                    !DeriveIsPositiveEffect(spellTriggeredProto, SpellEffectIndex(i), resolve, ctx, depth + 1))
                                    {
                                        return false;
                                    }
                            }
                        }
                    }
                    break;
                case SPELL_AURA_PROC_TRIGGER_SPELL:
                    // many positive auras have negative triggered spells at damage for example and this not make it negative (it can be canceled for example)
                    break;
                case SPELL_AURA_MOD_STUN:                   // have positive and negative spells, we can't sort its correctly at this moment.
                    if (effIndex == EFFECT_INDEX_0 && spellproto->Effect[EFFECT_INDEX_1] == 0 && spellproto->Effect[EFFECT_INDEX_2] == 0)
                    {
                        return false;                        // but all single stun aura spells is negative
                    }

                    // Petrification
                    if (spellproto->ID == 17624)
                    {
                        return false;
                    }
                    break;
                case SPELL_AURA_MOD_PACIFY_SILENCE:
                    if (spellproto->ID == 24740)            // Wisp Costume
                    {
                        return true;
                    }
                    return false;
                case SPELL_AURA_MOD_ROOT:
                case SPELL_AURA_MOD_SILENCE:
                case SPELL_AURA_GHOST:
                case SPELL_AURA_PERIODIC_LEECH:
                case SPELL_AURA_MOD_STALKED:
                case SPELL_AURA_PERIODIC_DAMAGE_PERCENT:
                    return false;
                case SPELL_AURA_PERIODIC_DAMAGE:            // used in positive spells also.
                    // part of negative spell if casted at self (prevent cancel)
                    if (spellproto->ImplicitTargetA[effIndex] == TARGET_SELF ||
                            spellproto->ImplicitTargetA[effIndex] == TARGET_SELF2)
                            {
                                return false;
                            }
                    break;
                case SPELL_AURA_MOD_DECREASE_SPEED:         // used in positive spells also
                    // part of positive spell if casted at self
                    if ((spellproto->ImplicitTargetA[effIndex] == TARGET_SELF ||
                            spellproto->ImplicitTargetA[effIndex] == TARGET_SELF2) &&
                            spellproto->SpellClassSet == SPELLFAMILY_GENERIC)
                            {
                                return false;
                            }
                    // but not this if this first effect (don't found better check)
                    if (spellproto->HasAttribute(SPELL_ATTR_AURA_IS_DEBUFF) && effIndex == EFFECT_INDEX_0)
                    {
                        return false;
                    }
                    break;
                case SPELL_AURA_TRANSFORM:
                    // some spells negative
                    switch (spellproto->ID)
                    {
                        case 36897:                         // Transporter Malfunction (race mutation to horde)
                        case 36899:                         // Transporter Malfunction (race mutation to alliance)
                            return false;
                    }
                    break;
                case SPELL_AURA_MOD_SCALE:
                    // some spells negative
                    switch (spellproto->ID)
                    {
                        case 802:                           // Mutate Bug, wrongly negative by target modes
                        case 38449:                         // Blessing of the Tides
                            return true;
                        case 36900:                         // Soul Split: Evil!
                        case 36901:                         // Soul Split: Good
                        case 36893:                         // Transporter Malfunction (decrease size case)
                        case 36895:                         // Transporter Malfunction (increase size case)
                            return false;
                    }
                    break;
                case SPELL_AURA_MECHANIC_IMMUNITY:
                {
                    // non-positive immunities
                    switch (spellproto->EffectMiscValue[effIndex])
                    {
                        case MECHANIC_BANDAGE:
                        case MECHANIC_SHIELD:
                        case MECHANIC_MOUNT:
                        case MECHANIC_INVULNERABILITY:
                            return false;
                        default:
                            break;
                    }
                }   break;
                case SPELL_AURA_ADD_FLAT_MODIFIER:          // mods
                case SPELL_AURA_ADD_PCT_MODIFIER:
                {
                    // non-positive mods
                    switch (spellproto->EffectMiscValue[effIndex])
                    {
                        case SPELLMOD_COST:                 // dependent from bas point sign (negative -> positive)
                            if (spellproto->CalculateSimpleValue(effIndex) > 0)
                            {
                                return false;
                            }
                            break;
                        default:
                            break;
                    }
                }   break;
                case SPELL_AURA_MOD_MELEE_HASTE:
                {
                    switch (spellproto->ID)
                    {
                        case 38449:                         // Blessing of the Tides
                            return true;
                        default:
                            break;
                    }
                    break;
                }
                case SPELL_AURA_FORCE_REACTION:
                {
                    if (spellproto->ID == 42792)            // Recently Dropped Flag (prevent cancel)
                    {
                        return false;
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }

        default:
            break;
    }

    // non-positive targets
    if (!DeriveIsPositiveTarget(spellproto->ImplicitTargetA[effIndex], spellproto->ImplicitTargetB[effIndex]))
    {
        return false;
    }

    // AttributesEx check
    if (spellproto->HasAttribute(SPELL_ATTR_EX_CANT_BE_REFLECTED))
    {
        return false;
    }

    // ok, positive
    return true;
}

uint32 GetPositiveTriggerTruncationCount()
{
    return g_truncationCount;
}

std::vector<SpellTriggerTruncation> const& GetPositiveTriggerTruncations()
{
    return g_truncations;
}

void ResetPositiveTriggerTruncations()
{
    g_truncationCount = 0;
    g_truncations.clear();
}

SpellCatalog& SpellCatalog::Instance()
{
    static SpellCatalog instance;
    return instance;
}

/**
 * @brief Fills the fields derivable from this spell's own DBC row alone.
 */
void SpellCatalog::DeriveLocal(SpellInfo& info) const
{
    SpellEntry const* proto = info.dbc;

    info.id = proto->ID;
    info.passive = DeriveIsPassiveSpell(proto);

    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (proto->Effect[i] == SPELL_EFFECT_NONE)
        {
            continue;
        }

        info.effectMask |= uint8(1 << i);

        if (IsAreaAuraEffect(proto->Effect[i]) ||
            proto->Effect[i] == SPELL_EFFECT_APPLY_AURA)
        {
            info.auraEffectMask |= uint8(1 << i);
        }

        const uint32 auraType = proto->EffectAura[i];
        if (auraType != 0 && auraType < TOTAL_AURAS)
        {
            info.auraTypes[auraType >> 6] |= uint64(1) << (auraType & 63);
        }
    }
}

/**
 * @brief Fills the fields that need other spells or the SQL overrides.
 *
 * Runs in a second pass because the positivity of a periodic-trigger aura is
 * judged from the spell it triggers, which must already be registered.
 */
void SpellCatalog::DeriveCrossReferenced(SpellInfo& info,
                                         SpellCatalogSources const& sources) const
{
    SpellEntry const* proto = info.dbc;

    // Resolve recursive lookups against the catalog being built, not the DBC
    // store: pass one has already filled every dbc pointer, and this keeps the
    // derivation free of any dependency on DBCStores.
    SpellEntry const* (*resolve)(void const*, uint32) =
        [](void const* ctx, uint32 spellId) -> SpellEntry const*
        {
            SpellCatalog const* self = static_cast<SpellCatalog const*>(ctx);
            return self->Get(spellId).dbc;
        };

    bool allPositive = true;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (!(info.effectMask & (1 << i)))
        {
            continue;
        }

        if (DeriveIsPositiveEffect(proto, SpellEffectIndex(i), resolve, this))
        {
            info.positiveMask |= uint8(1 << i);
        }
        else
        {
            allPositive = false;
        }
    }
    info.positive = allPositive;

    const uint32 elixirMask =
        sources.GetElixirMask ? sources.GetElixirMask(sources.ctx, info.id) : 0;
    info.specific = DeriveSpellSpecific(proto, elixirMask);

    info.procEvent =
        sources.GetProcEvent ? sources.GetProcEvent(sources.ctx, info.id) : nullptr;
    info.bonus =
        sources.GetBonus ? sources.GetBonus(sources.ctx, info.id) : nullptr;

    // An override, not a union: IsTriggeredAtSpellProcEvent takes the SQL value
    // whenever it is nonzero and only then falls back to the DBC column. Or-ing
    // the two would let a spell proc on flags the override was written to
    // remove.
    info.procFlags = (info.procEvent && info.procEvent->procFlags)
                     ? info.procEvent->procFlags
                     : proto->ProcTypeMask;
}

void SpellCatalog::Build(std::vector<SpellEntry const*> const& entries,
                         SpellCatalogSources const& sources)
{
    ResetPositiveTriggerTruncations();

    m_entries.clear();
    m_slotById.clear();
    m_count = 0;
    m_built = false;

    uint32 maxId = 0;
    uint32 usable = 0;
    for (SpellEntry const* proto : entries)
    {
        if (!proto)
        {
            continue;
        }
        maxId = std::max(maxId, proto->ID);
        ++usable;
    }

    m_slotById.assign(size_t(maxId) + 1, INVALID_SLOT);
    m_entries.resize(usable);

    // Pass one: register every row, so that pass two can resolve cross
    // references against the catalog itself.
    uint32 slot = 0;
    for (SpellEntry const* proto : entries)
    {
        if (!proto)
        {
            continue;
        }

        // A duplicate id would leave one of the two rows unreachable; keep the
        // first and let the caller's loader own the complaint.
        if (m_slotById[proto->ID] != INVALID_SLOT)
        {
            continue;
        }

        SpellInfo& info = m_entries[slot];
        info.dbc = proto;
        DeriveLocal(info);

        m_slotById[proto->ID] = slot;
        ++slot;
    }

    m_entries.resize(slot);
    m_count = slot;

    // Pass two: everything that may consult another spell or the SQL overrides.
    for (SpellInfo& info : m_entries)
    {
        DeriveCrossReferenced(info, sources);
    }

    m_built = true;
}
