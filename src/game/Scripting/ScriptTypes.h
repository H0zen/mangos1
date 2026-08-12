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

#ifndef MANGOS_SCRIPT_TYPES_H
#define MANGOS_SCRIPT_TYPES_H

#include "Platform/Define.h"
#include "Utilities/Errors.h"

#include <cstddef>
#include <string>

/**
 * The seam between the world and whatever runs scripts.
 *
 * Three headers make up the whole core-facing surface: this one carries the
 * payload vocabulary, ScriptEvents.h names the events, ScriptHost.h is what a
 * call site includes. No engine type appears in any of them, and none of them
 * pulls in an engine header. The engines themselves are reachable only through
 * the single non-template scripting::detail::Dispatch() in ScriptHost.cpp.
 */
namespace scripting
{
    /**
     * Identity of a game object as seen by a script engine.
     *
     * Deliberately not a pointer. An engine may hold a value across ticks while
     * the object it names dies in between; a reference is resolved through the
     * world at the moment it is used, so a dead object resolves to nothing
     * instead of to freed memory. It is a plain aggregate so that it can live
     * inside Arg's union.
     */
    struct Ref
    {
        uint64 guid;    ///< ObjectGuid raw value; 0 when empty

        bool IsEmpty() const { return guid == 0; }
    };

    /// What a Handle or a Borrow names. Kept in the payload so an engine can
    /// tell a guild id from a quest id without consulting the event.
    enum class Domain : uint8
    {
        None = 0,

        // Handle domains: a stable id that is simply not an ObjectGuid.
        //
        // AuctionHouse sits here by kind and is used as a BORROW by the
        // manifest (`house:b.ahouse`), which is not a contradiction: an
        // auction house object has no id worth handing out, only the entry in
        // it does. The grouping describes what a domain names, not which of
        // the two boxes a given event chose to put it in.
        Guild, Group, Quest, Map, BattleGround, Auction,
        AuctionHouse, ItemTemplate, SpellInfo, AreaTrigger, Weather,

        // Borrow domains: things with no identity to give at all.
        //
        // Channel is here and not above, which is not obvious: a channel looks
        // like it has an id, but Channel::GetChannelId() returns the DBC id --
        // 1 for General, 2 for Trade -- and it is 0 for every custom channel.
        // Treating that as a handle would collapse every custom channel on the
        // server into one identity.
        Spell, Aura, AuraEffect, Packet, CastTargets, Proc, Damage,
        Dispel, SpellDestination, Session, ObjectList, ObjectSlot, Channel
    };

    /**
     * A stable identity that is not an ObjectGuid.
     *
     * A guild, a group, a quest, a map, an auction. Roughly a tenth of every
     * argument the engines take is one of these, and none of them fits Ref --
     * which is why Ref alone was never going to be enough.
     */
    struct Handle
    {
        uint64 id;
        Domain domain;

        bool IsEmpty() const { return domain == Domain::None; }
    };

    /**
     * A pointer that is valid only while the emitting call is on the stack.
     *
     * A Spell in flight, an Aura, a WorldPacket, a ProcEventInfo: these have no
     * identity to hand out and no lifetime an engine can reason about. Today
     * they are passed to scripts as raw pointers, which is the largest
     * correctness hole in the current engines -- a Lua script that stores one
     * and reads it on the next tick is reading freed memory, and nothing in
     * the system says otherwise.
     *
     * The epoch closes it. The host bumps its epoch after every dispatch
     * returns, so a Borrow kept past the call no longer matches and the engine
     * reports a script error instead of dereferencing. The pointer is still a
     * pointer; what changes is that using it late is *detected*.
     */
    struct Borrow
    {
        void*  target;
        uint32 epoch;
        Domain domain;

        bool IsEmpty() const { return target == nullptr; }
    };

    /**
     * One value crossing the seam.
     *
     * Arguments travel in an array owned by the caller's frame, so a hook that
     * changes a value writes back into the slot it was handed: there is no
     * separate out-parameter mechanism, and no pointer to a game object
     * anywhere in the payload. Text is the one exception and it is deliberate
     * -- a string is not a world object with a lifetime of its own, it is an
     * in/out parameter living in the emitting frame for the length of the call.
     */
    struct Arg
    {
        enum class Kind : uint8
        {
            Empty,      ///< nothing was placed in this slot
            Signed,     ///< int64
            Number,     ///< uint64
            Real,       ///< double
            Flag,       ///< bool
            Entity,     ///< Ref
            Named,      ///< Handle
            Lent,       ///< Borrow, valid for this call only
            Text        ///< std::string, edited in place
        };

        Arg() : m_kind(Kind::Empty), m_number(0) {}

        static Arg FromSigned(int64 value)
        {
            Arg arg;
            arg.m_kind = Kind::Signed;
            arg.m_signed = value;
            return arg;
        }

        static Arg FromNumber(uint64 value)
        {
            Arg arg;
            arg.m_kind = Kind::Number;
            arg.m_number = value;
            return arg;
        }

        static Arg FromReal(double value)
        {
            Arg arg;
            arg.m_kind = Kind::Real;
            arg.m_real = value;
            return arg;
        }

        static Arg FromFlag(bool value)
        {
            Arg arg;
            arg.m_kind = Kind::Flag;
            arg.m_flag = value;
            return arg;
        }

        static Arg FromEntity(Ref value)
        {
            Arg arg;
            arg.m_kind = Kind::Entity;
            arg.m_entity = value;
            return arg;
        }

        static Arg FromNamed(Handle value)
        {
            Arg arg;
            arg.m_kind = Kind::Named;
            arg.m_named = value;
            return arg;
        }

        static Arg FromLent(Borrow value)
        {
            Arg arg;
            arg.m_kind = Kind::Lent;
            arg.m_lent = value;
            return arg;
        }

        static Arg FromText(std::string& value)
        {
            Arg arg;
            arg.m_kind = Kind::Text;
            arg.m_text = &value;
            return arg;
        }

        Kind GetKind() const { return m_kind; }

        int64 AsSigned() const
        {
            MANGOS_ASSERT(m_kind == Kind::Signed);
            return m_signed;
        }

        uint64 AsNumber() const
        {
            MANGOS_ASSERT(m_kind == Kind::Number);
            return m_number;
        }

        double AsReal() const
        {
            MANGOS_ASSERT(m_kind == Kind::Real);
            return m_real;
        }

        bool AsFlag() const
        {
            MANGOS_ASSERT(m_kind == Kind::Flag);
            return m_flag;
        }

        Ref AsEntity() const
        {
            MANGOS_ASSERT(m_kind == Kind::Entity);
            return m_entity;
        }

        Handle AsNamed() const
        {
            MANGOS_ASSERT(m_kind == Kind::Named);
            return m_named;
        }

        Borrow AsLent() const
        {
            MANGOS_ASSERT(m_kind == Kind::Lent);
            return m_lent;
        }

        std::string& AsText() const
        {
            MANGOS_ASSERT(m_kind == Kind::Text);
            return *m_text;
        }

    private:
        Kind m_kind;
        union
        {
            int64        m_signed;
            uint64       m_number;
            double       m_real;
            bool         m_flag;
            Ref          m_entity;
            Handle       m_named;
            Borrow       m_lent;
            std::string* m_text;    ///< in/out; owned by the caller's frame
        };
    };

    /**
     * A thing exactly one engine may own.
     *
     * Distinct from an event on purpose. An event is something the world did
     * and everyone may watch; a role is an object the world hands to a single
     * owner and then calls back, tick after tick. Two engines cannot both
     * drive one creature, so this is settled by auction rather than by chain.
     */
    enum class RoleId : uint8
    {
        CreatureAI,     ///< drives one creature
        GameObjectAI,   ///< drives one game object
        InstanceData    ///< drives one instance map
    };

    /**
     * What an engine offers for a role. Higher wins; ties go to the engine
     * configured first.
     *
     * This replaces two mechanisms with one. Creature AI is decided today by
     * whichever #ifdef nests outermost, and the AI registry decides by asking
     * each factory to score itself with Permit(). Those are the same mechanism
     * -- the first is just the second with the scores baked into link order.
     * Making the score a number is what turned "whichever engine the outermost
     * #ifdef named" from an accident of the preprocessor into something an
     * operator can configure.
     */
    enum : int
    {
        NoBid = -1,         ///< this engine does not want the role
        BidFallback = 0,    ///< take it only if nobody else will
        BidNormal = 100,
        BidStrong = 1000    ///< an explicit, per-object binding
    };

    /**
     * How far the world has got with its own loading.
     *
     * An engine's tables are its own, but WHEN they can be read is not: most
     * of them are checked against world data as they load, and a check can
     * only run once the data it names exists. Those dependencies were spelled
     * out as comments next to fifteen load calls in World.cpp -- "must be
     * before gossip menu options", "must be after load Creature/Gameobject
     * (Template/Data) and QuestTemplate" -- which made the world responsible
     * for knowing the load order of every engine's tables.
     *
     * The world announces where it has got to instead. Each engine decides
     * what that means for its own tables, and a new engine with a new
     * dependency adds a case rather than a line in World.cpp.
     *
     * There were two more, BeforeGossip and AfterWaypoints, and they went with
     * the coupling that justified them: the gossip and waypoint loaders used
     * to cross-check a `script_id` against an engine's table, so the engine
     * had to have read it by then. Nothing does that now -- which sequences
     * exist is the engine's business and a dangling id is a no-op, not a
     * dropped row -- and no engine subscribed to either. AfterWaypoints was
     * also emitted BEFORE the waypoints loaded, so as a statement about the
     * world's progress it was simply false. Add a phase back when an engine
     * needs one, and name it after what has actually happened.
     */
    enum class LoadPhase : uint8
    {
        Bindings,        ///< nothing world-specific yet; script names may bind
        AfterTemplates,  ///< creature and gameobject templates, and quests
        Final            ///< every world table is in place
    };

    /**
     * What the world does with a hook's answer.
     *
     * Three outcomes, because the single bool the engines return today means
     * three different things at once and no caller can tell which was meant:
     * "I refused this", "I produced the behaviour myself", and, as a side
     * effect of the order the #ifdefs happen to nest in, "no other engine gets
     * a turn". A Lua gossip script silently disabling the C++ one for the same
     * NPC is that conflation, not a bug anybody wrote on purpose.
     */
    enum class Verdict : uint8
    {
        Continue,   ///< carry on with the action that raised the event
        Handled,    ///< an engine produced the behaviour; skip the core default
        Cancel      ///< the scripts refused it; do not proceed at all
    };
}

#endif //MANGOS_SCRIPT_TYPES_H
