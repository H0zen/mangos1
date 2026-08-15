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

#ifndef MANGOS_ROSTER_H
#define MANGOS_ROSTER_H

#include "Platform/Define.h"

#include <cassert>
#include <cstddef>
#include <vector>

/**
 * @brief Who is driving a unit, who is waiting behind them, and what happens when one
 *        of them stops -- with none of it knowing what a movement generator is.
 *
 * This is the part of the old MotionMaster that was never a stack. It was WRITTEN as
 * one -- `class MotionMaster : private std::stack<MovementGenerator*>` -- and then
 * needed four near-identical clean-and-expire functions, a pair of flag bits and a
 * side list of things awaiting deletion, because the one thing a stack cannot survive
 * is being popped while something is iterating it. Which is exactly what happens here:
 * a generator's own Update calls MovementExpired on the master that is updating it, and
 * a generator's Finalize may push a replacement before the old one has been freed.
 *
 * So the rules got extracted, and they turn out to be small and worth asserting:
 *
 *   * the ACTIVE entry is the last one added that has not stopped;
 *   * removing an entry while the roster is being driven does not free it -- it is
 *     retired, and released later, at a point the caller chooses;
 *   * an entry added DURING a removal is not itself removed by that removal, which is
 *     what makes "Finalize pushes a replacement" safe rather than a race with the
 *     cleanup that provoked it.
 *
 * Templated on the payload so that the rules can be tested against a counter instead of
 * a Creature. That is the whole reason the rules are here rather than in the master:
 * they were only ever observable by standing up a world.
 *
 * NOT an arbiter yet. The active entry is the newest, not the most important, which is
 * what the stack meant and therefore what today's behaviour is. Ranks replace that
 * selection rule and nothing else -- one function, on top of a lifecycle that will by
 * then have tests.
 */
namespace Helm
{
    /**
     * @brief How much an entry's claim on a unit is worth.
     *
     * The old master already ranked things; it just did it as two special cases in the
     * middle of a push. "HomeMovement is not that important, delete it if meanwhile a
     * new comes" and "DistractMovement interrupted by any other movement" are rank
     * statements, written as an enum switch, applying to exactly the two types someone
     * had needed them for.
     *
     * As an order they generalise, and the ordering answers a question tracing push
     * order cannot: what beats what.
     */
    enum class Rank : uint8
    {
        /// Idle, wander, a waypoint patrol -- what the unit does when nothing else is
        /// happening. Always at the bottom, never displaced, only covered.
        Routine = 0,

        /// Go somewhere because something asked: a point move, seeking assistance,
        /// returning home, an effect.
        Errand = 1,

        /// Chase and follow. Above an errand because a creature in combat that has been
        /// told to walk somewhere should still be fighting.
        Combat = 2,

        /// Feared, confused. Nothing outranks losing control of yourself, and that is
        /// the one ordering here that is a DECISION rather than a description: under
        /// the old rule a chase pushed onto a feared creature took over, because it
        /// arrived later.
        Panic = 3
    };

    template <class T>
    class Roster
    {
        public:
            /// Nothing is driving.
            bool Empty() const { return m_entries.empty(); }

            std::size_t Size() const { return m_entries.size(); }

            /**
             * @brief The entry currently driving.
             *
             * Only call it when the roster is not empty; there is no null payload to
             * return and inventing one would put the check in every caller.
             */
            T const& Active() const
            {
                assert(!Empty());
                return m_entries[ActiveIndex()].payload;
            }
            T& Active()
            {
                assert(!Empty());
                return m_entries[ActiveIndex()].payload;
            }

            /// What the driving entry is worth.
            Rank ActiveRank() const { return m_entries[ActiveIndex()].rank; }

            /// An entry and what it is worth. Iteration yields payloads, not these:
            /// nothing outside needs the rank to walk the list.
            struct Slot
            {
                T    payload;
                Rank rank;
            };

            /// Oldest first, yielding payloads, so every existing loop reads unchanged.
            class Cursor
            {
                public:
                    explicit Cursor(typename std::vector<Slot>::const_iterator it)
                        : m_it(it) {}
                    T const& operator*() const { return m_it->payload; }
                    Cursor& operator++() { ++m_it; return *this; }
                    bool operator!=(Cursor const& o) const { return m_it != o.m_it; }
                private:
                    typename std::vector<Slot>::const_iterator m_it;
            };

            Cursor begin() const { return Cursor(m_entries.begin()); }
            Cursor end() const { return Cursor(m_entries.end()); }

            /// Newest first. What a search for "the most recent entry of some kind"
            /// wants -- the waypoint generator parked under whatever is driving now.
            class ReverseCursor
            {
                public:
                    explicit ReverseCursor(
                        typename std::vector<Slot>::const_reverse_iterator it)
                        : m_it(it) {}
                    T const& operator*() const { return m_it->payload; }
                    ReverseCursor& operator++() { ++m_it; return *this; }
                    bool operator!=(ReverseCursor const& o) const
                    {
                        return m_it != o.m_it;
                    }
                private:
                    typename std::vector<Slot>::const_reverse_iterator m_it;
            };

            ReverseCursor rbegin() const { return ReverseCursor(m_entries.rbegin()); }
            ReverseCursor rend() const { return ReverseCursor(m_entries.rend()); }

            /// Put `entry` in the roster at `rank`. WHETHER IT DRIVES is Active()'s
            /// answer, not this one -- which is the whole difference from a push.
            void Add(T const& entry, Rank rank = Rank::Routine)
            {
                Slot slot;
                slot.payload = entry;
                slot.rank = rank;
                m_entries.push_back(slot);
            }

            /**
             * @brief Mark the roster as being driven, or no longer being driven.
             *
             * While driving, removals retire rather than release, because the thing
             * being removed may be the one whose call stack we are standing in.
             */
            void BeginDriving() { ++m_driving; }
            void EndDriving() { if (m_driving) { --m_driving; } }
            bool Driving() const { return m_driving != 0; }

            /**
             * @brief Remove the active entry.
             *
             * @param floor Entries at or below this count are never removed -- the
             *        master keeps a default behaviour at the bottom that nothing may
             *        expire, and expressing that as a floor keeps the rule in one place
             *        instead of in four `size() > 1` tests.
             * @return False when the floor stopped it.
             */
            bool RemoveActive(std::size_t floor = 1)
            {
                if (m_entries.size() <= floor)
                {
                    return false;
                }

                // The driving entry, which is no longer necessarily the last one.
                const std::size_t at = ActiveIndex();
                Retire(m_entries[at].payload);
                m_entries.erase(m_entries.begin() + std::ptrdiff_t(at));
                return true;
            }

            /**
             * @brief Remove every entry down to `floor`.
             *
             * @return How many were removed.
             */
            std::size_t RemoveAbove(std::size_t floor)
            {
                std::size_t removed = 0;
                while (m_entries.size() > floor)
                {
                    Retire(m_entries.back().payload);
                    m_entries.pop_back();
                    ++removed;
                }
                return removed;
            }

            /**
             * @brief Remove the active entry, and any directly beneath it for which
             *        `pred` holds.
             *
             * The old expire did this for the chase and follow generators, whose habit
             * of stacking on one another meant that expiring the top left another of
             * the same kind underneath, and the unit resumed chasing something it had
             * just been told to stop chasing. Naming it as a predicate makes the rule
             * visible; leaving it inline made it look like an accident.
             */
            template <class Pred>
            std::size_t RemoveActiveAnd(Pred pred, std::size_t floor = 1)
            {
                if (!RemoveActive(floor))
                {
                    return 0;
                }

                std::size_t removed = 1;
                while (m_entries.size() > floor && pred(Active()))
                {
                    RemoveActive(floor);
                    ++removed;
                }
                return removed;
            }

            /**
             * @brief Entries removed while driving, awaiting release.
             *
             * The caller frees them when it is safe to, and calls TakeRetired to clear
             * the list. Nothing here owns a payload: a Roster of pointers does not
             * delete them, because whether a payload may be deleted at all is a
             * question about the payload -- one of the master's generators is a shared
             * static and must never be.
             */
            std::vector<T> const& Retired() const { return m_retired; }

            bool HasRetired() const { return !m_retired.empty(); }

            /// Hand the retired list over and forget it.
            std::vector<T> TakeRetired()
            {
                std::vector<T> taken;
                taken.swap(m_retired);
                return taken;
            }

            /// Forget everything, retiring nothing. For teardown, where the payloads
            /// are about to be disposed of wholesale anyway.
            void Abandon()
            {
                m_entries.clear();
                m_retired.clear();
                m_driving = 0;
            }

        private:
            /// Removed entries always land here, driving or not. The roster does not
            /// decide when a payload may be released -- Driving() tells the caller
            /// whether it is standing in one of them, and the caller drains the list
            /// when it is not.
            void Retire(T const& entry) { m_retired.push_back(entry); }

            /**
             * @brief Where the driving entry sits.
             *
             * Highest rank wins; among equals the newest. Walking forwards and taking
             * `>=` is what makes "newest among equals" fall out with no second test --
             * and it is why a roster of uniform rank behaves exactly as the stack this
             * replaces, which is what let the change land without touching behaviour.
             */
            std::size_t ActiveIndex() const
            {
                std::size_t best = 0;
                for (std::size_t i = 1; i < m_entries.size(); ++i)
                {
                    if (m_entries[i].rank >= m_entries[best].rank)
                    {
                        best = i;
                    }
                }
                return best;
            }

            std::vector<Slot> m_entries;
            std::vector<T>    m_retired;
            uint32         m_driving = 0;
    };
}

#endif // MANGOS_ROSTER_H
