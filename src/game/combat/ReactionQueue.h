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

#ifndef MANGOS_COMBAT_REACTIONQUEUE_H
#define MANGOS_COMBAT_REACTIONQUEUE_H

#include "ObjectGuid.h"
#include "combat/pure/CombatConstants.h"
#include "combat/pure/CombatTypes.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <variant>

namespace Combat
{
    /// One more swing, from a proc that granted it. Windfury is this, not two
    /// instant casts fired from inside the swing that produced them.
    struct ExtraSwing
    {
        Hand          hand  = Hand::Main;
        std::uint32_t count = 1;

        /// Build one from a grant, bounded. Depth limits the next generation
        /// of reactions; nothing else limits the width of this one, and each
        /// swing costs a full profile, table and commit.
        static ExtraSwing Granted(Hand hand, std::uint32_t count)
        {
            return ExtraSwing{
                hand, std::min(count, Constants::MAX_EXTRA_ATTACKS)};
        }
    };

    /// A spell a proc wants cast. Carried as an id, not a Spell object, so
    /// nothing is allocated until the queue actually runs it -- and nothing
    /// is allocated at all if the target died first.
    struct ProcCast
    {
        std::uint32_t spellId    = 0;
        std::int32_t  basePoints = 0;
        bool          triggered  = true;
    };

    /// Thorns and its relatives. The amount is snapshotted at commit time so
    /// the queue never walks a live aura list while that list is being
    /// changed by the damage it is dealing.
    struct DamageShield
    {
        std::uint32_t spellId    = 0;
        std::uint32_t amount     = 0;
        std::uint32_t schoolMask = 0;
    };

    /// The proc machinery, deferred.
    ///
    /// This is the ordering fix in one struct. The old path called
    /// ProcDamageAndSpell between sending the combat log and applying the
    /// damage, so a proc that killed the target made the log a lie and left
    /// the rest of the swing running on a corpse. The masks are captured at
    /// commit time and the run happens after the health has moved.
    struct ProcTrigger
    {
        std::uint32_t attackerMask = 0;
        std::uint32_t victimMask   = 0;
        std::uint32_t extraMask    = 0;
        std::uint32_t damage       = 0;
        Hand          hand         = Hand::Main;
    };

    /// Weapon enchants and poisons.
    struct ItemCombat
    {
        Hand hand = Hand::Main;
    };

    /// The creature daze, spell 1604.
    struct Daze
    {
        std::uint32_t spellId = 1604;
    };

    /**
     * @brief A consequence of a strike, to be run after the strike is done.
     *
     * Both ends are guids. The queue outlives the pointers that produced it
     * on purpose: a proc that despawns its target used to leave
     * damageInfo.target dangling and the rest of the swing running on it.
     */
    struct Reaction
    {
        ObjectGuid   source;
        ObjectGuid   target;
        std::uint8_t depth = 0;

        std::variant<ExtraSwing, ProcTrigger, ProcCast, DamageShield,
                     ItemCombat, Daze> what;
    };

    /**
     * @brief Runs one reaction. Supplied by the caller so the queue itself
     *        knows nothing about the world.
     *
     * A sink may push further reactions; they arrive at depth + 1 and are
     * subject to the same limits.
     */
    class ReactionSink
    {
        public:
            virtual ~ReactionSink() = default;
            virtual void Run(Reaction const& reaction,
                             class ReactionQueue& queue) = 0;
    };

    /**
     * @brief Deferred consequences, bounded in depth and in count.
     *
     * The old path ran every consequence inline, in the middle of the swing
     * that caused it: procs before the damage landed, damage shields inside a
     * loop over the victim's aura list, extra attacks by recursing into
     * AttackerStateUpdate. Each of those is a way for the world to change
     * underneath a function that is still reading it.
     *
     * Here they are values in a queue, drained after the strike has been
     * committed, with both ends re-resolved when they run. Depth stops a
     * proc-that-procs-itself; the per-drain budget stops a wide fan-out from
     * eating a tick.
     */
    class ReactionQueue
    {
        public:
            /// A reaction may cause a reaction. It may not cause a third.
            static constexpr std::uint8_t MAX_DEPTH = 2;

            /// Ceiling on how many reactions one drain will run. Reached only
            /// by something pathological; crossing it is counted, not ignored.
            static constexpr std::size_t MAX_PER_DRAIN = 64;

            /// @return false when depth or budget refused it.
            bool Push(Reaction reaction);

            /// Run everything pending, including whatever the sinks add.
            void Drain(ReactionSink& sink);

            /**
             * @brief Forget every pending reaction that names this unit.
             *
             * Called the moment a unit dies or leaves the world. This is what
             * makes the queue safe without any lifetime tracking: a reaction
             * whose source or target is gone is not run against a stale guid,
             * it stops existing.
             */
            void DropInvolving(ObjectGuid guid);

            void Clear();

            std::size_t Pending() const
            {
                return m_pending.size();
            }

            /// Reactions refused since the last Clear, for the log.
            std::uint32_t Refused() const
            {
                return m_refused;
            }

        private:
            std::deque<Reaction> m_pending;
            std::size_t          m_ranThisDrain = 0;
            std::uint32_t        m_refused      = 0;
            bool                 m_draining     = false;
    };
}

#endif
