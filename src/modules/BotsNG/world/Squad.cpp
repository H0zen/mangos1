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

#include "Squad.h"

#include "Group.h"
#include "ObjectMgr.h"
#include "Player.h"

#include <vector>

namespace bots
{
    namespace
    {
        std::uint8_t Percent(std::uint32_t part, std::uint32_t whole)
        {
            if (whole == 0)
            {
                return 0;
            }
            return static_cast<std::uint8_t>((part * 100) / whole);
        }

        /**
         * Whether @a member is holding things by being hit by them.
         *
         * Deliberately not a talent-tree reading or a configured role: a bot
         * decides who to heal from what is happening, and what is happening is
         * that something is hitting this one. A protection warrior standing at
         * the back is not the tank of this fight however he is specced.
         */
        bool LooksLikeTheTank(Player* member)
        {
            return member->getAttackers().size() >= 2;
        }
    }

    void Squad::Refresh(Player* anyMember, std::uint64_t tick)
    {
        if (m_tick == tick)
        {
            return;
        }
        m_tick = tick;
        m_count = 0;
        m_truncated = false;

        Group* group = anyMember->GetGroup();
        if (!group)
        {
            return;
        }

        Group::MemberSlotList const& slots = group->GetMemberSlots();
        for (Group::member_citerator itr = slots.begin(); itr != slots.end();
             ++itr)
        {
            if (m_count == MaxAllies)
            {
                m_truncated = true;
                break;
            }

            Player* member = sObjectMgr.GetPlayer(itr->guid);
            if (!member)
            {
                // Offline, or on a map this member cannot see. Left out rather
                // than entered as a dead ally: a healer must not spend the
                // fight noticing somebody who is not there.
                continue;
            }

            Ally& ally = m_allies[m_count++];
            ally.guid      = itr->guid.GetRawValue();
            ally.level     = static_cast<std::uint8_t>(member->GetLevel());
            ally.healthPct = Percent(member->GetHealth(),
                                     member->GetMaxHealth());

            const Powers power = member->GetPowerType();
            ally.powerPct = Percent(member->GetPower(power),
                                    member->GetMaxPower(power));

            ally.dead     = !member->IsAlive();
            ally.inCombat = member->IsInCombat();
            ally.isTank   = LooksLikeTheTank(member);

            // Distance and line of sight are relative to whoever is asking, so
            // they cannot live in a shared roster. CopyInto fills them.
            ally.distance      = 0.0f;
            ally.inLineOfSight = true;
        }
    }

    void Squad::CopyInto(Perception& out, EntityId self) const
    {
        out.allyCount = 0;
        for (std::size_t i = 0; i < m_count; ++i)
        {
            if (m_allies[i].guid == self)
            {
                continue;
            }
            out.allies[out.allyCount++] = m_allies[i];
        }
        out.alliesTruncated = m_truncated;
    }

    Squad* SquadBoard::For(Player* bot, std::uint64_t tick)
    {
        Group* group = bot->GetGroup();
        if (!group)
        {
            return nullptr;
        }

        Squad& squad = m_squads[group->GetId()];
        squad.Refresh(bot, tick);
        return &squad;
    }

    void SquadBoard::Sweep(std::uint64_t tick, std::uint64_t staleTicks)
    {
        for (auto itr = m_squads.begin(); itr != m_squads.end();)
        {
            if (tick > itr->second.LastTick() &&
                tick - itr->second.LastTick() > staleTicks)
            {
                itr = m_squads.erase(itr);
            }
            else
            {
                ++itr;
            }
        }
    }
}
