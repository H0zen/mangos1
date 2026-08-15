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

#include "ProcRegistry.h"

#include "ProcHandlers.h"
#include "SpellAuraDefines.h"
#include "Utilities/Errors.h"

#include <array>
#include <cstddef>

namespace Combat
{
    namespace
    {
        /// No handler for this aura type. Answering Ok rather than Failed
        /// keeps a holder's other effects from being marked as a failed proc.
        ProcResult NoHandler(ProcEvent const&)
        {
            return ProcResult::Ok;
        }

        /// This aura type is barred from procing.
        ProcResult CannotTrigger(ProcEvent const&)
        {
            return ProcResult::CantTrigger;
        }

        struct Registration
        {
            std::uint32_t auraType;
            ProcHandler   handler;
        };

        /**
         * The whole of it. Twenty-three aura types out of TOTAL_AURAS carry a
         * proc handler; the rest do nothing, and saying so once here beats
         * saying it 239 times in a dense array where a missing line shifts
         * every aura below it onto the wrong handler.
         */
        const Registration REGISTRATIONS[] =
        {
            { SPELL_AURA_DUMMY,                             &Procs::Dummy },
            { SPELL_AURA_MOD_FEAR,                          &Procs::RemoveByDamageChance },
            { SPELL_AURA_MOD_INVISIBILITY,                  &Procs::Invisibility },
            { SPELL_AURA_MOD_RESISTANCE,                    &Procs::ModResistance },
            { SPELL_AURA_MOD_ROOT,                          &Procs::RemoveByDamageChance },
            { SPELL_AURA_PROC_TRIGGER_SPELL,                &Procs::TriggerSpell },
            { SPELL_AURA_PROC_TRIGGER_DAMAGE,               &Procs::TriggerDamage },
            { SPELL_AURA_MOD_PACIFY_SILENCE,                &Procs::RemoveByDamageChance },
            { SPELL_AURA_MOD_CASTING_SPEED_NOT_STACK,       &Procs::CastingSpeedNotStack },
            { SPELL_AURA_MOD_POWER_COST_SCHOOL_PCT,         &Procs::PowerCostSchool },
            { SPELL_AURA_MOD_POWER_COST_SCHOOL,             &Procs::PowerCostSchool },
            { SPELL_AURA_REFLECT_SPELLS_SCHOOL,             &Procs::ReflectSpellsSchool },
            { SPELL_AURA_MECHANIC_IMMUNITY,                 &Procs::MechanicImmunity },
            { SPELL_AURA_MOD_POWER_REGEN,                   &CannotTrigger },
            { SPELL_AURA_MANA_SHIELD,                       &Procs::ManaShield },
            { SPELL_AURA_OVERRIDE_CLASS_SCRIPTS,            &Procs::OverrideClassScript },
            { SPELL_AURA_MOD_MECHANIC_RESISTANCE,           &Procs::MechanicImmunity },
            { SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS, &Procs::AttackPowerAttackerBonus },
            { SPELL_AURA_MOD_MELEE_HASTE,                   &Procs::Haste },
            { SPELL_AURA_RESIST_PUSHBACK,                   &CannotTrigger },
            { SPELL_AURA_MELEE_ATTACK_POWER_ATTACKER_BONUS, &Procs::AttackPowerAttackerBonus },
            { SPELL_AURA_PRAYER_OF_MENDING,                 &Procs::Mending },
            { SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE,     &Procs::TriggerSpell }
        };

        constexpr std::size_t REGISTRATION_COUNT =
            sizeof(REGISTRATIONS) / sizeof(REGISTRATIONS[0]);

        /// The sparse list above, expanded once into a dense array so the
        /// lookup on the proc path stays a single indexed read.
        class Table
        {
            public:
                Table()
                {
                    m_slots.fill(&NoHandler);

                    for (std::size_t i = 0; i < REGISTRATION_COUNT; ++i)
                    {
                        Registration const& entry = REGISTRATIONS[i];

                        MANGOS_ASSERT(entry.auraType < TOTAL_AURAS);
                        MANGOS_ASSERT(m_slots[entry.auraType] == &NoHandler);

                        m_slots[entry.auraType] = entry.handler;
                    }
                }

                ProcHandler At(std::uint32_t auraType) const
                {
                    return auraType < TOTAL_AURAS
                        ? m_slots[auraType]
                        : &NoHandler;
                }

            private:
                std::array<ProcHandler, TOTAL_AURAS> m_slots{};
        };

        Table const& Lookup()
        {
            static Table const table;
            return table;
        }
    }

    ProcHandler ProcHandlerFor(std::uint32_t auraType)
    {
        return Lookup().At(auraType);
    }

    std::uint32_t RegisteredProcHandlerCount()
    {
        return static_cast<std::uint32_t>(REGISTRATION_COUNT);
    }
}
