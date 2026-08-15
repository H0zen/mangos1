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

#include "SpellFactsStore.h"

#include "DBCStores.h"
#include "Log.h"
#include "SpellMgr.h"

#include <cmath>

namespace Combat
{
    namespace
    {
        /// Reports are capped per field: a decoder that is wrong is wrong for
        /// thousands of spells, and the first twenty say everything the log
        /// needs to.
        constexpr std::uint32_t REPORTS_PER_FIELD = 20;

        bool NearlyEqual(float a, float b)
        {
            return std::fabs(a - b) < 0.001f;
        }

        void DecodeEffect(SpellEntry const& entry, SpellEffectIndex index,
                          SpellEffectFacts& out)
        {
            const std::uint32_t effect = entry.Effect[index];
            if (effect == 0)
            {
                return;
            }

            out.present      = true;
            out.effect       = effect;
            out.auraType     = entry.EffectAura[index];
            out.basePoints   = entry.EffectBasePoints[index];
            out.dieSides     = entry.EffectDieSides[index];
            out.targetA      = entry.ImplicitTargetA[index];
            out.targetB      = entry.ImplicitTargetB[index];
            out.mechanic     = entry.EffectMechanic[index];
            out.triggerSpell = entry.EffectTriggerSpell[index];
            out.amplitudeMs  = static_cast<std::int32_t>(
                entry.EffectAuraPeriod[index]);

            // The reason this store exists, in one line: the row carries an
            // INDEX, and every question about how far the effect reaches used
            // to pay for a second lookup in another store.
            out.radius = GetSpellRadius(
                sSpellRadiusStore.LookupEntry(entry.EffectRadiusIndex[index]));

            // Positive/negative stays out of stage C on purpose. It is a
            // two-hundred-line heuristic in the live code and correcting it
            // is a rule change, not a decode; stage D moves it with its own
            // shadow. The field is left false rather than filled in wrongly.
        }
    }

    SpellFactsStore& SpellFactsStore::Instance()
    {
        static SpellFactsStore instance;
        return instance;
    }

    void SpellFactsStore::Load()
    {
        const std::uint32_t rows = sSpellStore.GetNumRows();

        m_facts.assign(rows + 1, SpellFacts());
        m_known = 0;

        for (std::uint32_t id = 0; id < rows; ++id)
        {
            SpellEntry const* entry = sSpellStore.LookupEntry(id);
            if (!entry)
            {
                continue;
            }

            SpellFacts& facts = m_facts[id];

            facts.known       = true;
            facts.id          = entry->ID;
            facts.family      = entry->SpellClassSet;
            facts.familyFlags = entry->SpellClassMask.Flags;
            facts.schoolMask  = entry->SchoolMask;
            facts.dispelType  = entry->DispelType;
            facts.mechanic    = entry->Mechanic;

            facts.durationMs    = GetSpellDuration(entry);
            facts.maxDurationMs = GetSpellMaxDuration(entry);
            facts.castTimeMs    = GetSpellCastTime(entry);

            SpellRangeEntry const* range =
                sSpellRangeStore.LookupEntry(entry->RangeIndex);
            facts.rangeMin = GetSpellMinRange(range);
            facts.rangeMax = GetSpellMaxRange(range);

            facts.recoveryTimeMs = GetSpellRecoveryTime(entry);

            facts.channeled =
                entry->ChannelInterruptFlags != 0 ||
                entry->HasAttribute(SPELL_ATTR_EX_CHANNELED_1) ||
                entry->HasAttribute(SPELL_ATTR_EX_CHANNELED_2);

            facts.passive        = entry->HasAttribute(SPELL_ATTR_PASSIVE);
            facts.breaksStealth  = !entry->HasAttribute(SPELL_ATTR_EX_NOT_BREAK_STEALTH);
            facts.deathPersistent =
                entry->HasAttribute(SPELL_ATTR_EX3_DEATH_PERSISTENT);

            facts.mechanicMask = MechanicBit(entry->Mechanic);

            for (std::uint32_t i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                const SpellEffectIndex index = SpellEffectIndex(i);
                DecodeEffect(*entry, index, facts.effects[i]);

                if (!facts.effects[i].present)
                {
                    continue;
                }

                ++facts.effectCount;
                facts.mechanicMask |= MechanicBit(facts.effects[i].mechanic);

                if (facts.effects[i].radius > 0.0f)
                {
                    facts.areaOfEffect = true;
                }
            }

            ++m_known;
        }

        sLog.outString("Materialised facts for %u spells.", m_known);
    }

    std::uint32_t SpellFactsStore::Audit() const
    {
        std::uint32_t mismatches = 0;
        std::uint32_t reported[6] = {0, 0, 0, 0, 0, 0};

        auto report = [&](std::size_t field, char const* name,
                          std::uint32_t id, char const* detail)
        {
            ++mismatches;
            if (reported[field]++ < REPORTS_PER_FIELD)
            {
                sLog.outError("SpellFacts audit: spell %u %s %s",
                              id, name, detail);
            }
        };

        for (SpellFacts const& facts : m_facts)
        {
            if (!facts.known)
            {
                continue;
            }

            SpellEntry const* entry = sSpellStore.LookupEntry(facts.id);
            if (!entry)
            {
                report(0, "duration", facts.id, "row vanished after load");
                continue;
            }

            if (facts.durationMs != GetSpellDuration(entry))
            {
                report(0, "duration", facts.id, "differs from the live query");
            }
            if (facts.maxDurationMs != GetSpellMaxDuration(entry))
            {
                report(1, "max duration", facts.id, "differs from the live query");
            }
            if (facts.castTimeMs != GetSpellCastTime(entry))
            {
                report(2, "cast time", facts.id, "differs from the live query");
            }

            SpellRangeEntry const* range =
                sSpellRangeStore.LookupEntry(entry->RangeIndex);

            if (!NearlyEqual(facts.rangeMin, GetSpellMinRange(range)) ||
                !NearlyEqual(facts.rangeMax, GetSpellMaxRange(range)))
            {
                report(3, "range", facts.id, "differs from the live query");
            }

            for (std::uint32_t i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                const float live = GetSpellRadius(sSpellRadiusStore.LookupEntry(
                    entry->EffectRadiusIndex[SpellEffectIndex(i)]));

                const bool present =
                    entry->Effect[SpellEffectIndex(i)] != 0;

                if (present && !NearlyEqual(facts.effects[i].radius, live))
                {
                    report(4, "effect radius", facts.id,
                           "differs from the live query");
                }
            }

            if (facts.recoveryTimeMs != GetSpellRecoveryTime(entry))
            {
                report(5, "recovery time", facts.id,
                       "differs from the live query");
            }
        }

        if (mismatches == 0)
        {
            sLog.outString("SpellFacts audit: %u spells, every field agrees "
                           "with the live query.", m_known);
        }
        else
        {
            sLog.outError("SpellFacts audit: %u mismatching field(s). The "
                          "store is NOT safe to read from.", mismatches);
        }

        return mismatches;
    }
}
