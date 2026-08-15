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

#include "HitTable.h"

#include <algorithm>

namespace Combat
{
    namespace
    {
        /// Accumulates bands left to right, saturating at a full probability.
        /// A table can be over-subscribed -- enough avoidance and there is no
        /// room left for a normal hit -- and saturating is what keeps the
        /// later bands empty instead of letting a bound wrap past the roll.
        class Accumulator
        {
            public:
                explicit Accumulator(std::array<Hundredths, OUTCOME_COUNT>& out)
                    : m_out(out)
                {
                }

                void Add(Outcome outcome, Hundredths width)
                {
                    if (width > 0)
                    {
                        m_running = std::min(m_running + width,
                                             HUNDRED_PERCENT);
                    }
                    m_out[Index(outcome)] = m_running;
                }

                void Fill(Outcome outcome)
                {
                    m_running = HUNDRED_PERCENT;
                    m_out[Index(outcome)] = m_running;
                }

                void Carry(Outcome outcome)
                {
                    m_out[Index(outcome)] = m_running;
                }

            private:
                std::array<Hundredths, OUTCOME_COUNT>& m_out;
                Hundredths m_running = 0;
        };
    }

    HitTable HitTable::OneRoll(Matchup const& m)
    {
        HitTable table;
        Accumulator sum(table.m_bound);

        if (m.evading)
        {
            // Nothing else can happen, and nothing else is asked.
            sum.Fill(Outcome::Evade);
            sum.Carry(Outcome::Miss);
            sum.Carry(Outcome::Dodge);
            sum.Carry(Outcome::Parry);
            sum.Carry(Outcome::Glancing);
            sum.Carry(Outcome::Block);
            sum.Carry(Outcome::Crit);
            sum.Carry(Outcome::Crushing);
            sum.Carry(Outcome::Normal);
            return table;
        }

        sum.Carry(Outcome::Evade);
        sum.Add(Outcome::Miss, m.miss);

        if (m.sittingCrit)
        {
            // Miss still applies -- a sitting target can be missed -- but
            // everything between it and crit is skipped, and crit takes the
            // rest of the table.
            sum.Carry(Outcome::Dodge);
            sum.Carry(Outcome::Parry);
            sum.Carry(Outcome::Glancing);
            sum.Carry(Outcome::Block);
            sum.Fill(Outcome::Crit);
            sum.Carry(Outcome::Crushing);
            sum.Carry(Outcome::Normal);
            return table;
        }

        sum.Add(Outcome::Dodge, m.dodge);
        sum.Add(Outcome::Parry, m.parry);
        sum.Add(Outcome::Glancing, m.glance);
        sum.Add(Outcome::Block, m.block);
        sum.Add(Outcome::Crit, m.crit);
        sum.Add(Outcome::Crushing, m.crush);
        sum.Fill(Outcome::Normal);

        return table;
    }

    HitTable HitTable::TwoRoll(Matchup const& m)
    {
        HitTable table;
        Accumulator sum(table.m_bound);

        if (m.evading)
        {
            sum.Fill(Outcome::Evade);
            sum.Carry(Outcome::Miss);
            sum.Carry(Outcome::Dodge);
            sum.Carry(Outcome::Parry);
            sum.Carry(Outcome::Glancing);
            sum.Carry(Outcome::Block);
            sum.Carry(Outcome::Crit);
            sum.Carry(Outcome::Crushing);
            sum.Carry(Outcome::Normal);
            return table;
        }

        sum.Carry(Outcome::Evade);
        sum.Add(Outcome::Miss, m.miss);
        sum.Add(Outcome::Dodge, m.dodge);
        sum.Add(Outcome::Parry, m.parry);

        // Empty on purpose. A special does not glance, does not crush, and
        // rolls its block and its crit separately -- the caller does that.
        sum.Carry(Outcome::Glancing);
        sum.Carry(Outcome::Block);
        sum.Carry(Outcome::Crit);
        sum.Carry(Outcome::Crushing);
        sum.Fill(Outcome::Normal);

        return table;
    }

    Outcome HitTable::Resolve(Hundredths roll) const
    {
        for (std::size_t i = 0; i < OUTCOME_COUNT; ++i)
        {
            if (roll < m_bound[i])
            {
                return static_cast<Outcome>(i);
            }
        }

        // Unreachable for a roll in range: Normal's bound is a full
        // probability. A roll handed in out of range lands here.
        return Outcome::Normal;
    }

    Hundredths HitTable::Bound(Outcome outcome) const
    {
        return m_bound[Index(outcome)];
    }

    Hundredths HitTable::Band(Outcome outcome) const
    {
        const std::size_t i = Index(outcome);
        const Hundredths below = i == 0 ? 0 : m_bound[i - 1];
        return m_bound[i] - below;
    }
}
