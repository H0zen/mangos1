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

#include "ProcEvent.h"

#include "DBCStores.h"
#include "Log.h"
#include "Player.h"
#include "SpellAuras.h"
#include "Unit.h"

#include <ctime>

namespace Combat
{
    SpellEntry const* ProcEvent::AuraSpell() const
    {
        return aura ? aura->GetSpellProto() : nullptr;
    }

    std::int32_t ProcEvent::Amount() const
    {
        return aura ? aura->GetModifier()->m_amount : 0;
    }

    std::int32_t ProcEvent::BasePoints() const
    {
        return aura ? aura->GetBasePoints() : 0;
    }

    Player* ProcEvent::ActorPlayer() const
    {
        if (!actor || actor->GetTypeId() != TYPEID_PLAYER)
        {
            return nullptr;
        }

        return static_cast<Player*>(actor);
    }

    Item* ProcEvent::CastItem() const
    {
        Player* player = ActorPlayer();
        if (!player || !aura || !aura->GetCastItemGuid())
        {
            return nullptr;
        }

        return player->GetItemByGuid(aura->GetCastItemGuid());
    }

    bool ProcEvent::OnCooldown(std::uint32_t spellId) const
    {
        if (cooldown == 0)
        {
            return false;
        }

        Player* player = ActorPlayer();

        return player && player->HasSpellCooldown(spellId);
    }

    void ProcEvent::StartCooldown(std::uint32_t spellId) const
    {
        if (cooldown == 0)
        {
            return;
        }

        if (Player* player = ActorPlayer())
        {
            player->AddSpellCooldown(spellId, 0,
                                     time(nullptr) + time_t(cooldown));
        }
    }

    ProcResult ProcEvent::Trigger(Unit* victim, std::uint32_t spellId,
                                  std::int32_t const* basePoints,
                                  char const* caller) const
    {
        if (!actor || spellId == 0)
        {
            return ProcResult::Failed;
        }

        if (!sSpellStore.LookupEntry(spellId))
        {
            sLog.outError("%s: proc of spell %u triggers spell %u, which the "
                          "DBC does not have",
                          caller ? caller : "Combat::ProcEvent::Trigger",
                          AuraSpell() ? AuraSpell()->ID : 0, spellId);

            return ProcResult::Failed;
        }

        // A dead target takes nothing. The actor is exempt: a proc that heals
        // or shields the caster runs whatever state the caster is in.
        if (!victim || (victim != actor && !victim->IsAlive()))
        {
            return ProcResult::Failed;
        }

        if (OnCooldown(spellId))
        {
            return ProcResult::Failed;
        }

        Item* castItem = CastItem();

        if (basePoints)
        {
            actor->CastCustomSpell(victim, spellId, basePoints, nullptr,
                                   nullptr, true, castItem, aura);
        }
        else
        {
            actor->CastSpell(victim, spellId, true, castItem, aura);
        }

        StartCooldown(spellId);

        return ProcResult::Ok;
    }
}
