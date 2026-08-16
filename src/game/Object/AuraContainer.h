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

#ifndef MANGOS_H_AURACONTAINER
#define MANGOS_H_AURACONTAINER

#include "Platform/Define.h"
#include "SpellAuraDefines.h"
// For TrackedAuraType and MAX_TRACKED_AURA_TYPES, which the tracked-target
// table below is sized and keyed by. Inherited from whoever included this
// first until a build without precompiled headers asked for it here.
#include "SharedDefines.h"
#include "ObjectGuid.h"

#include <list>
#include <map>
#include <utility>

class Aura;
class SpellAuraHolder;
class Unit;
struct SpellEntry;

/**
 * @brief Every aura on a unit, and the one rule that makes removing one safe.
 *
 * Held by value on Unit. The types below live here rather than in Unit.h
 * because they describe this and nothing else.
 *
 * THE CURSOR IS THE POINT. Updating a holder can remove it -- an expiring
 * aura, a proc that dispels itself, a periodic tick that kills the caster --
 * so the walk holds a cursor that is advanced BEFORE the holder is updated,
 * and any removal that lands on the cursor must move it on first. Those two
 * halves are one rule, and while they lived in two files three hundred lines
 * apart, getting them out of step meant iterating a freed node.
 *
 * Here the erase moves the cursor itself and there is no way to do one without
 * the other.
 */
class AuraContainer
{
    public:
        typedef std::multimap<uint32 /*spellId*/, SpellAuraHolder*> HolderMap;
        typedef std::pair<HolderMap::iterator, HolderMap::iterator> Bounds;
        typedef std::pair<HolderMap::const_iterator,
                          HolderMap::const_iterator> ConstBounds;
        typedef std::list<SpellAuraHolder*> HolderList;
        typedef std::list<Aura*> AuraList;
        typedef std::map<SpellEntry const*, ObjectGuid> TrackedTargetMap;

        explicit AuraContainer(Unit* owner)
            : m_owner(owner)
        {
            m_cursor = m_holders.end();
        }

        HolderMap&       Holders()       { return m_holders; }
        HolderMap const& Holders() const { return m_holders; }

        Bounds BoundsOf(uint32 spellId)
        {
            return m_holders.equal_range(spellId);
        }

        ConstBounds BoundsOf(uint32 spellId) const
        {
            return m_holders.equal_range(spellId);
        }

        bool Has(uint32 spellId) const
        {
            return m_holders.find(spellId) != m_holders.end();
        }

        void Insert(uint32 spellId, SpellAuraHolder* holder)
        {
            m_holders.insert(HolderMap::value_type(spellId, holder));
        }

        /**
         * @brief Take one holder out of the map.
         *
         * Moves the update cursor off it first, which is the whole reason this
         * is a method rather than a call to erase.
         *
         * @return false when the holder was not in the map.
         */
        bool Erase(SpellAuraHolder* holder);

        /**
         * @brief Run every holder's periodic update.
         *
         * The cursor advances before each holder is touched, so a holder that
         * removes itself -- or any other -- while updating cannot leave the
         * walk pointing at a freed node.
         */
        void UpdateHolders(uint32 diff);

        /// The auras of one type, which is how every modifier is totalled.
        AuraList&       ByType(AuraType type)       { return m_modAuras[type]; }
        AuraList const& ByType(AuraType type) const { return m_modAuras[type]; }

        TrackedTargetMap& Tracked(TrackedAuraType type)
        {
            return m_tracked[type];
        }

        TrackedTargetMap const& Tracked(TrackedAuraType type) const
        {
            return m_tracked[type];
        }

        /**
         * @brief Hold a removed aura until it is safe to delete.
         *
         * A modifier being applied can remove the aura carrying it, and the
         * stack is still inside that aura's own code. Deletion waits for
         * Cleanup(), which the owner calls where nothing is left on the stack.
         */
        void QueueDelete(Aura* aura)                { m_deletedAuras.push_back(aura); }
        void QueueDelete(SpellAuraHolder* holder)   { m_deletedHolders.push_back(holder); }

        bool HasDeletions() const
        {
            return !m_deletedAuras.empty() || !m_deletedHolders.empty();
        }

        /// Delete everything queued. Safe to call when nothing is queued.
        void Cleanup();

    private:
        Unit* m_owner;                  ///< non-owning

        HolderMap m_holders;
        HolderMap::iterator m_cursor;   ///< end() when no walk is in progress

        AuraList m_modAuras[TOTAL_AURAS];

        AuraList   m_deletedAuras;
        HolderList m_deletedHolders;

        TrackedTargetMap m_tracked[MAX_TRACKED_AURA_TYPES];
};

#endif
