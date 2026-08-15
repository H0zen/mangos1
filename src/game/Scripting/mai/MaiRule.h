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

#ifndef MANGOS_MAI_RULE_H
#define MANGOS_MAI_RULE_H

#include "MaiRules.gen.h"
#include "MaiScript.h"

#include <string>
#include <vector>

/**
 * When a sequence starts.
 *
 * A sequence says what happens and when, from its own start. A rule says what
 * makes it start. That is the whole of the difference between the two systems
 * being folded together, and each of them spent real effort faking the other:
 * EventAI faked sequences with timers, the DB scripts faked rules by having
 * ten tables.
 */
namespace mai
{
    /**
     * A rule's flags, named. The values are `creature_ai_scripts`'s own.
     *
     * Written out here so that nothing which merely RUNS rules has to include
     * EventAI's headers to know what a bit means. The conversion reads that
     * table; the engine reads these.
     */
    enum RuleFlags : uint8
    {
        RuleRepeatable = 0x01,  ///< fires more than once without being re-armed
        RuleNormalOnly = 0x02,  ///< normal difficulty only
        RuleHeroicOnly = 0x04,  ///< heroic difficulty only

        /// Run ONE of the steps, chosen at random, rather than all of them.
        /// An EventAI-ism: with three fixed action slots and no way to say
        /// "then", picking one was the only randomness available.
        RuleRandomStep = 0x20,

        RuleDebugOnly  = 0x80
    };

    /**
     * One trigger, its condition, and what it runs.
     *
     * The steps are a Sequence rather than a list, and that is not a detail:
     * EventAI gave every row exactly three action slots -- because a table
     * needs a fixed width, not because three is a natural number of things to
     * do -- so a creature doing four things on aggro was two rows with the
     * same trigger, the second one a fiction told to get more columns. A rule
     * that starts a sequence has no such ceiling, and gets `wait` for free the
     * moment the steps are allowed times.
     */
    struct Rule
    {
        uint32   id = 0;
        RuleId   trigger = RuleId::None;

        /// Laid out in the order this trigger's ParamSpec table names them.
        Operand  operands[MaxOperands] = {};
        uint16   given = 0;

        /// The phases this rule does NOT fire in. Inverted, as the tables have
        /// it, so a converted row means what it meant.
        uint32   inversePhaseMask = 0;

        /// How soon to try again when a step's cast was REFUSED -- silenced,
        /// out of range, already casting. Zero, and every converted rule has
        /// zero, means the ordinary repeat: EventAI re-armed on FIRING and
        /// never learnt whether the cast worked, and 20,732 rules rest on
        /// that.
        ///
        /// ScriptDev re-arms only on success, so a refused cast is retried on
        /// the very next tick -- twenty times a second until it lands. This is
        /// the same idea with the interval written down instead of implied,
        /// which is what Pandemonius's author did by hand:
        ///
        ///     m_uiVoidBlastTimer = 500;   // it did not go off; soon, then
        uint32   retryMs = 0;

        /// What must be true as well, all of it. Empty on almost every rule
        /// converted from EventAI, because EventAI had no way to say it.
        std::vector<Guard> guards;

        /// Out of 100, and ZERO MEANS NEVER rather than always -- which is a
        /// trap worth the line: the loader reports a rule with 0 as an error
        /// and then keeps it, so a row written that way is loaded, valid, and
        /// silent for ever.
        uint8    chance = 100;
        uint8    flags = 0;

        Sequence steps;

        bool Has(std::size_t slot) const
        {
            return (given & (1u << slot)) != 0;
        }

        uint32 Param(std::size_t slot, uint32 fallback = 0) const
        {
            return Has(slot) ? operands[slot].u : fallback;
        }
    };

    /**
     * Every rule one creature entry has, and the state a running one needs.
     *
     * Kept per ENTRY, not per creature: the rules are the same for every
     * Onyxia in the world, and what differs between two of them is the timers
     * and the phase, which live on the Actor.
     */
    struct RuleSet
    {
        uint32            creature = 0;
        std::vector<Rule> rules;

        /// The names this creature's guards and set_state steps use, in the
        /// order they were first met. A rule stores the INDEX; this is what
        /// turns it back into a word for an error message or a GM command.
        ///
        /// Per entry rather than global because `phase` on one boss and
        /// `phase` on another are not the same number and must not share a
        /// slot -- and because eight slots per creature is plenty while eight
        /// across a world would not be.
        std::vector<std::string> stateNames;

        /**
         * Names that are not a creature's own memory, whatever they look like.
         *
         * Exactly one so far, and it earned the mechanism: `phase` is what
         * `set_phase` writes, which is `Actor::phases.current` and not a state
         * slot at all. Allowed as a state name, it read as a slot nothing ever
         * wrote -- so `phase=2` was a guard that could not become true, and it
         * was the guard both the schema and the manual used as their example.
         *
         * Refused in both directions: a guard saying `phase` means the phase
         * (GuardPhase), and a `set_state name=phase` is a load error rather
         * than a second thing with the same name.
         */
        static bool Reserved(std::string const& name)
        {
            return name == "phase";
        }

        /// The slot @a name has, interning it if this is the first time.
        /// @return MaxStates when the creature has run out, or when the name
        ///         is reserved -- both refused at load rather than silently
        ///         aliasing two things onto one word.
        std::size_t Intern(std::string const& name)
        {
            if (Reserved(name))
            {
                return MaxStates;
            }

            for (std::size_t slot = 0; slot < stateNames.size(); ++slot)
            {
                if (stateNames[slot] == name)
                {
                    return slot;
                }
            }

            if (stateNames.size() >= MaxStates)
            {
                return MaxStates;
            }

            stateNames.push_back(name);
            return stateNames.size() - 1;
        }
    };
}

#endif //MANGOS_MAI_RULE_H
