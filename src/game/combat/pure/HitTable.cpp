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
        /**
         * @brief Accumulates bands left to right, saturating at a full
         *        probability.
         *
         * A table can be over-subscribed -- enough avoidance and there is no
         * room left for a normal hit -- and saturating is what keeps the later
         * bands empty instead of letting a bound wrap past the roll.
         *
         * Every outcome must be given a bound, in order, so that Resolve can
         * be a straight scan. Seal() fills whatever is left, which is how a
         * short-circuit like evade is expressed without a second code path.
         */
        class Accumulator
        {
            public:
                explicit Accumulator(std::array<Hundredths, OUTCOME_COUNT>& out)
                    : m_out(out)
                {
                }

                /// Give this outcome a band of the given width.
                void Add(Outcome outcome, Hundredths width)
                {
                    if (width > 0)
                    {
                        m_running = std::min(m_running + width,
                                             HUNDRED_PERCENT);
                    }
                    Stamp(outcome);
                }

                /// Give this outcome everything that is left.
                void Take(Outcome outcome)
                {
                    m_running = HUNDRED_PERCENT;
                    Stamp(outcome);
                }

                /// Give this outcome an empty band.
                void Skip(Outcome outcome)
                {
                    Stamp(outcome);
                }

                /// Close the table: every outcome from here on is empty, and
                /// the last one carries the full probability so a roll always
                /// lands.
                void Seal()
                {
                    while (m_next < OUTCOME_COUNT)
                    {
                        m_out[m_next] = m_running;
                        ++m_next;
                    }
                    m_out[OUTCOME_COUNT - 1] = HUNDRED_PERCENT;
                }

            private:
                void Stamp(Outcome outcome)
                {
                    const std::size_t i = Index(outcome);
                    while (m_next <= i)
                    {
                        m_out[m_next] = m_running;
                        ++m_next;
                    }
                }

                std::array<Hundredths, OUTCOME_COUNT>& m_out;
                Hundredths  m_running = 0;
                std::size_t m_next    = 0;
        };

        /// Evade and immunity are whole-table answers. Neither is a special
        /// case in Resolve: they are simply a band that covers everything.
        bool ShortCircuit(Matchup const& m, Accumulator& sum)
        {
            if (m.evading)
            {
                sum.Take(Outcome::Evade);
                sum.Seal();
                return true;
            }

            if (m.immune)
            {
                sum.Skip(Outcome::Evade);
                sum.Take(Outcome::Immune);
                sum.Seal();
                return true;
            }

            sum.Skip(Outcome::Evade);
            sum.Skip(Outcome::Immune);
            return false;
        }
    }

    HitTable HitTable::OneRoll(Matchup const& m)
    {
        HitTable table;
        Accumulator sum(table.m_bound);

        if (ShortCircuit(m, sum))
        {
            return table;
        }

        sum.Add(Outcome::Miss, m.miss);

        if (m.sittingCrit)
        {
            // Miss still applies -- a sitting target can be missed -- but
            // everything between it and crit is skipped, and crit takes the
            // rest of the table.
            sum.Skip(Outcome::Dodge);
            sum.Skip(Outcome::Parry);
            sum.Skip(Outcome::Glancing);
            sum.Skip(Outcome::Block);
            sum.Take(Outcome::Crit);
            sum.Seal();
            return table;
        }

        sum.Add(Outcome::Dodge, m.dodge);
        sum.Add(Outcome::Parry, m.parry);
        sum.Add(Outcome::Glancing, m.glance);
        sum.Add(Outcome::Block, m.block);
        sum.Add(Outcome::Crit, m.crit);
        sum.Add(Outcome::Crushing, m.crush);
        sum.Take(Outcome::Normal);

        return table;
    }

    HitTable HitTable::TwoRoll(Matchup const& m)
    {
        HitTable table;
        Accumulator sum(table.m_bound);

        if (ShortCircuit(m, sum))
        {
            return table;
        }

        sum.Add(Outcome::Miss, m.miss);
        sum.Add(Outcome::Dodge, m.dodge);
        sum.Add(Outcome::Parry, m.parry);

        // Empty on purpose. A special does not glance, does not crush, and
        // rolls its block and its crit separately -- the caller does that.
        sum.Skip(Outcome::Glancing);
        sum.Skip(Outcome::Block);
        sum.Skip(Outcome::Crit);
        sum.Skip(Outcome::Crushing);
        sum.Take(Outcome::Normal);

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

        // Unreachable for a roll in range: the last bound is a full
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
