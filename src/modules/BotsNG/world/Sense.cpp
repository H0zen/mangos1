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

#include "Sense.h"
#include "Squad.h"

#include "Item.h"
#include "ItemPrototype.h"
#include "ObjectLookup.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "SpellMgr.h"

#include <algorithm>

namespace bots
{
    namespace
    {
        /**
         * The three spell categories that name what a consumable is FOR.
         *
         * Read out of this world's own `item_template`, not remembered from
         * another core: food is 11 and drink is 59 on every conjured and
         * vendor consumable checked, and the healthstone is 1153 -- which is
         * NOT the potions' category 4. That distinction is the whole reason
         * the survival layer may spend a stone and still have a potion left,
         * so it is worth having looked rather than assumed.
         */
        constexpr std::uint32_t CategoryFood        = 11;
        constexpr std::uint32_t CategoryDrink       = 59;
        constexpr std::uint32_t CategoryHealthstone = 1153;

        std::uint8_t Percent(std::uint32_t part, std::uint32_t whole)
        {
            if (whole == 0)
            {
                return 0;
            }
            return static_cast<std::uint8_t>((part * 100) / whole);
        }

        PowerKind KindOf(Powers power)
        {
            switch (power)
            {
                case POWER_MANA:      return PowerKind::Mana;
                case POWER_RAGE:      return PowerKind::Rage;
                case POWER_FOCUS:     return PowerKind::Focus;
                case POWER_ENERGY:    return PowerKind::Energy;
                case POWER_HAPPINESS: return PowerKind::Happiness;
                default:              return PowerKind::None;
            }
        }

        /**
         * The item's on-use spell, which is not always the first one.
         *
         * A healthstone carries nothing in slot zero and its spell in slot
         * one. A classifier that reads `Spells[0]` finds nothing, decides the
         * item is not a consumable, and the bot dies with a stone in its bag
         * -- silently, because nothing anywhere was wrong enough to log.
         */
        _Spell const* OnUseSpell(ItemPrototype const* proto)
        {
            for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                if (proto->Spells[i].SpellId != 0 &&
                    proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
                {
                    return &proto->Spells[i];
                }
            }
            return nullptr;
        }

        /**
         * What this item is for.
         *
         * Category first, because a category is the game's own statement about
         * what a thing is. Effects only as a fallback, for the potions that
         * share one category and differ in what they do.
         */
        Use Classify(ItemPrototype const* proto, _Spell const* use)
        {
            if (proto->Class != ITEM_CLASS_CONSUMABLE || !use)
            {
                return Use::None;
            }

            if (proto->SubClass == ITEM_SUBCLASS_BANDAGE)
            {
                return Use::Bandage;
            }

            switch (use->SpellCategory)
            {
                case CategoryFood:        return Use::Food;
                case CategoryDrink:       return Use::Drink;
                case CategoryHealthstone: return Use::Healthstone;
                default: break;
            }

            SpellEntry const* spell = sSpellStore.LookupEntry(use->SpellId);
            if (!spell)
            {
                return Use::None;
            }

            for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if (spell->Effect[i] == SPELL_EFFECT_HEAL)
                {
                    return Use::Heal;
                }
                if (spell->Effect[i] == SPELL_EFFECT_ENERGIZE &&
                    spell->EffectMiscValue[i] == POWER_MANA)
                {
                    return Use::Mana;
                }
            }

            return Use::None;
        }

        /// Every item in the bags and the backpack, equipment excluded: a bot
        /// does not drink its shoulders.
        template <typename Visit>
        void ForEachCarried(Player* bot, Visit&& visit)
        {
            for (std::uint8_t slot = INVENTORY_SLOT_ITEM_START;
                 slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    visit(item);
                }
            }

            for (std::uint8_t bag = INVENTORY_SLOT_BAG_START;
                 bag < INVENTORY_SLOT_BAG_END; ++bag)
            {
                Bag* container = static_cast<Bag*>(
                    bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag));
                if (!container)
                {
                    continue;
                }

                for (std::uint32_t slot = 0; slot < container->GetBagSize();
                     ++slot)
                {
                    if (Item* item = container->GetItemByPos(
                            static_cast<std::uint8_t>(slot)))
                    {
                        visit(item);
                    }
                }
            }
        }

        std::uint32_t CooldownLeftMs(Player* bot, std::uint32_t spellId)
        {
            // The core stores cooldowns to the second, so this is as precise
            // as the question can be answered. It is not precise enough to
            // decide a rotation on, which is why nothing here uses it for the
            // global cooldown.
            return static_cast<std::uint32_t>(
                bot->GetSpellCooldownDelay(spellId)) * 1000u;
        }
    }

    void Senses::Watch(std::vector<std::uint32_t> spells)
    {
        m_watched = std::move(spells);
        std::sort(m_watched.begin(), m_watched.end());
        m_watched.erase(std::unique(m_watched.begin(), m_watched.end()),
                        m_watched.end());
    }

    void Senses::CastStarted(SpellEntry const* spellInfo, std::uint32_t nowMs)
    {
        if (!spellInfo)
        {
            return;
        }

        // StartRecoveryTime is the spell's own statement of what it costs the
        // global cooldown, and zero means it costs nothing -- an instant that
        // does not lock the bar. The old module answered this question with
        // two configuration constants and never looked at the spell.
        if (spellInfo->StartRecoveryTime > 0)
        {
            m_gcdEndsMs = nowMs + spellInfo->StartRecoveryTime;
        }
    }

    void Senses::RefreshBags()
    {
        m_bagCount = 0;
        ForEachCarried(m_bot, [this](Item* item)
        {
            if (m_bagCount == MaxCarried)
            {
                return;
            }

            ItemPrototype const* proto = item->GetProto();
            _Spell const* use = OnUseSpell(proto);
            const Use kind = Classify(proto, use);
            if (kind == Use::None)
            {
                return;
            }

            Carried& carried = m_bag[m_bagCount++];
            carried.itemEntry = proto->ItemId;
            carried.count     = item->GetCount();
            carried.minLevel  = proto->RequiredLevel;
            carried.use       = kind;
            carried.cooldownLeftMs = use
                ? CooldownLeftMs(m_bot, use->SpellId)
                : 0;
        });

        m_bagsDirty = false;
    }

    void Senses::Refresh(Perception& out, Tuning const& tune,
                         std::uint64_t tick, std::uint32_t nowMs,
                         Squad const* squad)
    {
        out.tick        = tick;
        out.nowMs       = nowMs;
        out.tune        = tune;
        out.lastRefusal = m_lastRefusal;

        Self& self = out.self;
        self = Self();

        self.guid      = m_bot->GetObjectGuid().GetRawValue();
        self.level     = static_cast<std::uint8_t>(m_bot->GetLevel());
        self.health    = m_bot->GetHealth();
        self.maxHealth = m_bot->GetMaxHealth();
        self.healthPct = Percent(self.health, self.maxHealth);

        const Powers power = m_bot->GetPowerType();
        self.powerKind = KindOf(power);
        self.power     = m_bot->GetPower(power);
        self.powerPct  = Percent(self.power, m_bot->GetMaxPower(power));

        self.inCombat = m_bot->IsInCombat();
        self.dead     = !m_bot->IsAlive();
        self.mounted  = m_bot->IsMounted();
        self.moving   = m_bot->isMoving();

        // Fear and confusion belong here beside the stun for one reason: from
        // the decision's point of view they are the same fact -- orders will
        // not be obeyed -- and a layer that had to enumerate them would get the
        // list wrong the first time somebody added a new one.
        self.stunned = m_bot->hasUnitState(UNIT_STAT_STUNNED |
                                           UNIT_STAT_CONFUSED |
                                           UNIT_STAT_FLEEING);

        self.gcdLeftMs = m_gcdEndsMs > nowMs ? m_gcdEndsMs - nowMs : 0;

        if (Spell* casting = m_bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        {
            self.castingSpell = casting->m_spellInfo->Id;
        }
        else if (Spell* channelled =
                     m_bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        {
            self.castingSpell = channelled->m_spellInfo->Id;
        }

        self.at.x   = m_bot->GetPositionX();
        self.at.y   = m_bot->GetPositionY();
        self.at.z   = m_bot->GetPositionZ();
        self.facing = m_bot->GetOrientation();

        for (auto const& entry : m_bot->GetSpellAuraHolderMap())
        {
            if (self.auraCount == MaxAuras)
            {
                break;
            }

            SpellAuraHolder const* holder = entry.second;
            if (!holder)
            {
                continue;
            }

            Aura& aura = self.auras[self.auraCount++];
            aura.spell   = holder->GetId();
            aura.stacks  = static_cast<std::uint8_t>(holder->GetStackAmount());
            aura.msLeft  = holder->GetAuraDuration() > 0
                ? static_cast<std::uint32_t>(holder->GetAuraDuration())
                : 0;
            aura.harmful = !holder->IsPositive();

            const std::uint32_t category = holder->GetSpellProto()->Category;
            if (category == CategoryFood)
            {
                self.eating = true;
            }
            else if (category == CategoryDrink)
            {
                self.drinking = true;
            }
        }

        for (std::uint32_t spell : m_watched)
        {
            if (self.cooldownCount == MaxCooldowns)
            {
                break;
            }
            const std::uint32_t left = CooldownLeftMs(m_bot, spell);
            if (left == 0)
            {
                continue;
            }
            Cooldown& cooldown = self.cooldowns[self.cooldownCount++];
            cooldown.spell  = spell;
            cooldown.msLeft = left;
        }

        if (m_bagsDirty)
        {
            RefreshBags();
        }
        self.carried      = m_bag;
        self.carriedCount = m_bagCount;

        // -- what else is out there ------------------------------------------

        out.allyCount = 0;
        out.foeCount  = 0;
        out.target.reset();
        out.alliesTruncated = false;
        out.foesTruncated   = false;

        if (squad)
        {
            squad->CopyInto(out, self.guid);
            for (std::size_t i = 0; i < out.allyCount; ++i)
            {
                Unit* ally = ObjectLookup::GetUnit(
                    *m_bot, ObjectGuid(out.allies[i].guid));
                if (!ally)
                {
                    continue;
                }
                out.allies[i].distance = m_bot->GetDistance(ally);
                out.allies[i].inLineOfSight = m_bot->IsWithinLOSInMap(ally);
            }
        }

        auto describe = [this](Unit* unit, Foe& foe)
        {
            foe.guid      = unit->GetObjectGuid().GetRawValue();
            foe.level     = static_cast<std::uint8_t>(unit->GetLevel());
            foe.healthPct = Percent(unit->GetHealth(), unit->GetMaxHealth());
            foe.distance  = m_bot->GetDistance(unit);
            foe.casting   = unit->IsNonMeleeSpellCasted(false);
            foe.isPlayer  = unit->GetTypeId() == TYPEID_PLAYER;
            foe.inLineOfSight = m_bot->IsWithinLOSInMap(unit);
            foe.attackingMe = unit->GetVictim() == m_bot;
        };

        for (Unit* attacker : m_bot->getAttackers())
        {
            if (out.foeCount == MaxFoes)
            {
                out.foesTruncated = true;
                break;
            }
            if (attacker && attacker->IsAlive())
            {
                describe(attacker, out.foes[out.foeCount++]);
            }
        }

        if (Unit* selected = ObjectLookup::GetUnit(*m_bot,
                                                   m_bot->GetSelectionGuid()))
        {
            if (selected->IsAlive() && m_bot->CanAttack(selected))
            {
                Foe chosen;
                describe(selected, chosen);
                out.target = chosen;
            }
        }
    }
}
