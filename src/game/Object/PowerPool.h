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

#ifndef MANGOS_H_POWERPOOL
#define MANGOS_H_POWERPOOL

#include <cstdint>

/**
 * @brief When a resource pool next moves, and by how much.
 *
 * Held by value on Unit. Depends on nothing -- not on Unit, not on the world,
 * not on a DBC -- so a tick can be asserted on a bare struct with written-down
 * numbers.
 */

/// How long a pool waits between ticks. Two seconds is the interval the mana
/// and energy amounts below are already expressed per.
constexpr std::uint32_t REGEN_TIME_FULL = 2000;

/**
 * @brief The countdown to a unit's next resource tick.
 *
 * Advance() saturates at zero rather than wrapping, which matters because the
 * remaining time is unsigned and an update can be longer than what is left of
 * the interval.
 */
class PowerPool
{
    public:
        PowerPool() = default;

        explicit PowerPool(std::uint32_t period) : m_untilTick(period) {}

        void Advance(std::uint32_t elapsed)
        {
            m_untilTick = elapsed >= m_untilTick ? 0 : m_untilTick - elapsed;
        }

        bool TickDue() const
        {
            return m_untilTick == 0;
        }

        void ArmTick(std::uint32_t period)
        {
            m_untilTick = period;
        }

        std::uint32_t UntilTick() const
        {
            return m_untilTick;
        }

    private:
        std::uint32_t m_untilTick = 0;
};

/// Which direction a pool moves when left alone. Rage is the only one that
/// drains, and it is the reason the tick cannot simply add.
enum class PoolBehaviour : std::uint8_t
{
    Fills,
    Drains
};

/**
 * @brief Everything one tick of one pool is computed from.
 *
 * `base` is the amount the pool would move by before any aura touches it; the
 * flat bonus is added to it and the percentage multiplies the sum.
 */
struct RegenTick
{
    PoolBehaviour behaviour = PoolBehaviour::Fills;
    std::uint32_t current   = 0;
    std::uint32_t maximum   = 0;
    float         base      = 0.0f;
    float         flatBonus = 0.0f;
    float         percent   = 1.0f; ///< 1.0 is "unchanged"
};

/**
 * @brief What the pool holds after the tick.
 *
 * A pool that fills stops at its maximum; a pool that drains stops at zero.
 * An amount that comes out negative moves nothing, so a hostile modifier
 * cannot turn regeneration into a drain or a drain into regeneration.
 */
std::uint32_t RegeneratedValue(RegenTick const& tick);

#endif
