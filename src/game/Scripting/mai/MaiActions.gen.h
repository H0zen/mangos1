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

// GENERATED FROM actions.manifest -- DO NOT EDIT.
// Regenerate with: python src/game/Scripting/mai/tools/gen_actions.py

#ifndef MANGOS_MAI_ACTIONS_GEN_H
#define MANGOS_MAI_ACTIONS_GEN_H

#include "Platform/Define.h"

#include <cstddef>

namespace mai
{
    /// What a parameter is, and therefore what the loader checks
    /// it against before a script is allowed to run.
    enum class ParamType : uint8
    {
        Area,
        Bool,
        Creature,
        Emote,
        F32,
        Faction,
        Field,
        Flags,
        Gameobject,
        I32,
        Item,
        Mail,
        Map,
        Movie,
        Ms,
        Quest,
        Sound,
        Spell,
        Taxi,
        Text,
        U32,
    };

    struct ParamSpec
    {
        char const* name;
        ParamType   type;
        bool        optional;
    };

    /// The verbs. The numbers are the DB-script command ids,
    /// unchanged, so an existing row lowers by number.
    enum class ActionId : uint16
    {
        None = 0xFFFF,

        // say
        Talk                       = 0,
        Emote                      = 1,
        PlaySound                  = 16,
        PlayMovie                  = 19,

        // field
        FieldSet                   = 2,
        FlagSet                    = 4,
        FlagRemove                 = 5,
        MorphToEntryOrModel        = 23,
        MountToEntryOrModel        = 24,
        ChangeEntry                = 39,
        UpdateTemplate             = 44,
        SetEquipmentSlots          = 42,
        ModifyNpcFlags             = 29,
        SetFaction                 = 22,

        // move
        MoveTo                     = 3,
        TeleportTo                 = 6,
        Movement                   = 20,
        SetRun                     = 25,
        TurnTo                     = 36,
        MoveDynamic                = 37,
        SendTaxiPath               = 30,
        PauseWaypoints             = 32,
        SetFly                     = 59,
        StandState                 = 28,

        // combat
        CastSpell                  = 15,
        RemoveAura                 = 14,
        AttackStart                = 26,
        DespawnSelf                = 18,
        Respawn                    = 41,

        // world
        RespawnGo                  = 9,
        DespawnGo                  = 40,
        OpenDoor                   = 11,
        CloseDoor                  = 12,
        ActivateObject             = 13,
        ResetGo                    = 43,
        GoLockState                = 27,
        TempSummonCreature         = 10,
        SetActiveobject            = 21,

        // player
        QuestExplored              = 7,
        KillCredit                 = 8,
        CreateItem                 = 17,
        SendMail                   = 38,
        JoinLfg                    = 33,
        XpUser                     = 53,

        // flow
        TerminateScript            = 31,
        TerminateCond              = 34,
        SendAiEventAround          = 35,
    };

    inline constexpr ParamSpec g_paramsTalk[] =
    {
        { "text0", ParamType::Text, true },
        { "text1", ParamType::Text, true },
        { "text2", ParamType::Text, true },
        { "text3", ParamType::Text, true },
    };

    inline constexpr ParamSpec g_paramsEmote[] =
    {
        { "emote", ParamType::Emote, false },
    };

    inline constexpr ParamSpec g_paramsPlaySound[] =
    {
        { "sound", ParamType::Sound, false },
        { "flags", ParamType::Flags, true },
    };

    inline constexpr ParamSpec g_paramsPlayMovie[] =
    {
        { "movie", ParamType::Movie, false },
    };

    inline constexpr ParamSpec g_paramsFieldSet[] =
    {
        { "field", ParamType::Field, false },
        { "value", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_paramsFlagSet[] =
    {
        { "field", ParamType::Field, false },
        { "value", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_paramsFlagRemove[] =
    {
        { "field", ParamType::Field, false },
        { "value", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_paramsMorphToEntryOrModel[] =
    {
        { "entry", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_paramsMountToEntryOrModel[] =
    {
        { "entry", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_paramsChangeEntry[] =
    {
        { "entry", ParamType::Creature, false },
    };

    inline constexpr ParamSpec g_paramsUpdateTemplate[] =
    {
        { "entry", ParamType::Creature, false },
        { "faction", ParamType::Faction, false },
    };

    inline constexpr ParamSpec g_paramsSetEquipmentSlots[] =
    {
        { "reset_default", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_paramsModifyNpcFlags[] =
    {
        { "flag", ParamType::Flags, false },
        { "change", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_paramsSetFaction[] =
    {
        { "faction", ParamType::Faction, false },
        { "flags", ParamType::Flags, true },
    };

    inline constexpr ParamSpec g_paramsMoveTo[] =
    {
        { "speed", ParamType::U32, true },
        { "x", ParamType::F32, true },
        { "y", ParamType::F32, true },
        { "z", ParamType::F32, true },
        { "o", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_paramsTeleportTo[] =
    {
        { "map", ParamType::Map, false },
        { "x", ParamType::F32, true },
        { "y", ParamType::F32, true },
        { "z", ParamType::F32, true },
        { "o", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_paramsMovement[] =
    {
        { "type", ParamType::U32, false },
        { "wander_distance", ParamType::U32, true },
    };

    inline constexpr ParamSpec g_paramsSetRun[] =
    {
        { "run", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_paramsTurnTo[] =
    {
        { "target", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_paramsMoveDynamic[] =
    {
        { "max_dist", ParamType::F32, false },
        { "min_dist", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_paramsSendTaxiPath[] =
    {
        { "path", ParamType::Taxi, false },
    };

    inline constexpr ParamSpec g_paramsPauseWaypoints[] =
    {
        { "pause", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_paramsSetFly[] =
    {
        { "enable", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_paramsStandState[] =
    {
        { "state", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_paramsCastSpell[] =
    {
        { "spell", ParamType::Spell, false },
        { "flags", ParamType::Flags, true },
    };

    inline constexpr ParamSpec g_paramsRemoveAura[] =
    {
        { "spell", ParamType::Spell, false },
    };

    inline constexpr ParamSpec g_paramsDespawnSelf[] =
    {
        { "delay", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_paramsRespawnGo[] =
    {
        { "guid", ParamType::U32, false },
        { "despawn_delay", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_paramsDespawnGo[] =
    {
        { "guid", ParamType::U32, false },
        { "respawn_time", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_paramsOpenDoor[] =
    {
        { "guid", ParamType::U32, false },
        { "reset_delay", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_paramsCloseDoor[] =
    {
        { "guid", ParamType::U32, false },
        { "reset_delay", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_paramsGoLockState[] =
    {
        { "state", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_paramsTempSummonCreature[] =
    {
        { "entry", ParamType::Creature, false },
        { "despawn_delay", ParamType::Ms, true },
        { "x", ParamType::F32, true },
        { "y", ParamType::F32, true },
        { "z", ParamType::F32, true },
        { "o", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_paramsSetActiveobject[] =
    {
        { "activate", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_paramsQuestExplored[] =
    {
        { "quest", ParamType::Quest, false },
        { "distance", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_paramsKillCredit[] =
    {
        { "entry", ParamType::Creature, false },
        { "group_credit", ParamType::Bool, true },
    };

    inline constexpr ParamSpec g_paramsCreateItem[] =
    {
        { "item", ParamType::Item, false },
        { "amount", ParamType::U32, true },
    };

    inline constexpr ParamSpec g_paramsSendMail[] =
    {
        { "template", ParamType::Mail, false },
        { "alt_sender", ParamType::Creature, true },
    };

    inline constexpr ParamSpec g_paramsJoinLfg[] =
    {
        { "area", ParamType::Area, false },
    };

    inline constexpr ParamSpec g_paramsXpUser[] =
    {
        { "flags", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_paramsTerminateScript[] =
    {
        { "entry", ParamType::Creature, true },
        { "search_dist", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_paramsTerminateCond[] =
    {
        { "condition", ParamType::U32, false },
        { "fail_quest", ParamType::Quest, true },
    };

    inline constexpr ParamSpec g_paramsSendAiEventAround[] =
    {
        { "event", ParamType::U32, false },
        { "radius", ParamType::F32, false },
    };

    /// Which shared facets an action carries, and therefore
    /// where its own parameters stop and theirs begin. The
    /// lowering from `dbscripts_on_*` reads this instead of
    /// having a case per verb: a DB row is two datalongs plus
    /// exactly these facets, so knowing which ones apply is
    /// the whole of the mapping.
    enum Facet : uint8
    {
        FacetAt         = 1 << 0,
        FacetTexts      = 1 << 1,
    };

    struct ActionSpec
    {
        ActionId          id;
        char const*       name;       ///< "cast_spell"
        ParamSpec const*  params;
        std::size_t       arity;      ///< own parameters + facets
        std::size_t       own;        ///< how many are the verb's own
        uint8             facets;     ///< a mask of Facet
    };

    inline constexpr ActionSpec g_actionSpecs[] =
    {
        { ActionId::Talk, "talk", g_paramsTalk, 4, 0, FacetTexts },
        { ActionId::Emote, "emote", g_paramsEmote, 1, 1, 0 },
        { ActionId::PlaySound, "play_sound", g_paramsPlaySound, 2, 2, 0 },
        { ActionId::PlayMovie, "play_movie", g_paramsPlayMovie, 1, 1, 0 },
        { ActionId::FieldSet, "field_set", g_paramsFieldSet, 2, 2, 0 },
        { ActionId::FlagSet, "flag_set", g_paramsFlagSet, 2, 2, 0 },
        { ActionId::FlagRemove, "flag_remove", g_paramsFlagRemove, 2, 2, 0 },
        { ActionId::MorphToEntryOrModel, "morph_to_entry_or_model", g_paramsMorphToEntryOrModel, 1, 1, 0 },
        { ActionId::MountToEntryOrModel, "mount_to_entry_or_model", g_paramsMountToEntryOrModel, 1, 1, 0 },
        { ActionId::ChangeEntry, "change_entry", g_paramsChangeEntry, 1, 1, 0 },
        { ActionId::UpdateTemplate, "update_template", g_paramsUpdateTemplate, 2, 2, 0 },
        { ActionId::SetEquipmentSlots, "set_equipment_slots", g_paramsSetEquipmentSlots, 1, 1, 0 },
        { ActionId::ModifyNpcFlags, "modify_npc_flags", g_paramsModifyNpcFlags, 2, 2, 0 },
        { ActionId::SetFaction, "set_faction", g_paramsSetFaction, 2, 2, 0 },
        { ActionId::MoveTo, "move_to", g_paramsMoveTo, 5, 1, FacetAt },
        { ActionId::TeleportTo, "teleport_to", g_paramsTeleportTo, 5, 1, FacetAt },
        { ActionId::Movement, "movement", g_paramsMovement, 2, 2, 0 },
        { ActionId::SetRun, "set_run", g_paramsSetRun, 1, 1, 0 },
        { ActionId::TurnTo, "turn_to", g_paramsTurnTo, 1, 1, 0 },
        { ActionId::MoveDynamic, "move_dynamic", g_paramsMoveDynamic, 2, 2, 0 },
        { ActionId::SendTaxiPath, "send_taxi_path", g_paramsSendTaxiPath, 1, 1, 0 },
        { ActionId::PauseWaypoints, "pause_waypoints", g_paramsPauseWaypoints, 1, 1, 0 },
        { ActionId::SetFly, "set_fly", g_paramsSetFly, 1, 1, 0 },
        { ActionId::StandState, "stand_state", g_paramsStandState, 1, 1, 0 },
        { ActionId::CastSpell, "cast_spell", g_paramsCastSpell, 2, 2, 0 },
        { ActionId::RemoveAura, "remove_aura", g_paramsRemoveAura, 1, 1, 0 },
        { ActionId::AttackStart, "attack_start", nullptr, 0, 0, 0 },
        { ActionId::DespawnSelf, "despawn_self", g_paramsDespawnSelf, 1, 1, 0 },
        { ActionId::Respawn, "respawn", nullptr, 0, 0, 0 },
        { ActionId::RespawnGo, "respawn_go", g_paramsRespawnGo, 2, 2, 0 },
        { ActionId::DespawnGo, "despawn_go", g_paramsDespawnGo, 2, 2, 0 },
        { ActionId::OpenDoor, "open_door", g_paramsOpenDoor, 2, 2, 0 },
        { ActionId::CloseDoor, "close_door", g_paramsCloseDoor, 2, 2, 0 },
        { ActionId::ActivateObject, "activate_object", nullptr, 0, 0, 0 },
        { ActionId::ResetGo, "reset_go", nullptr, 0, 0, 0 },
        { ActionId::GoLockState, "go_lock_state", g_paramsGoLockState, 1, 1, 0 },
        { ActionId::TempSummonCreature, "temp_summon_creature", g_paramsTempSummonCreature, 6, 2, FacetAt },
        { ActionId::SetActiveobject, "set_activeobject", g_paramsSetActiveobject, 1, 1, 0 },
        { ActionId::QuestExplored, "quest_explored", g_paramsQuestExplored, 2, 2, 0 },
        { ActionId::KillCredit, "kill_credit", g_paramsKillCredit, 2, 2, 0 },
        { ActionId::CreateItem, "create_item", g_paramsCreateItem, 2, 2, 0 },
        { ActionId::SendMail, "send_mail", g_paramsSendMail, 2, 2, 0 },
        { ActionId::JoinLfg, "join_lfg", g_paramsJoinLfg, 1, 1, 0 },
        { ActionId::XpUser, "xp_user", g_paramsXpUser, 1, 1, 0 },
        { ActionId::TerminateScript, "terminate_script", g_paramsTerminateScript, 2, 2, 0 },
        { ActionId::TerminateCond, "terminate_cond", g_paramsTerminateCond, 2, 2, 0 },
        { ActionId::SendAiEventAround, "send_ai_event_around", g_paramsSendAiEventAround, 2, 2, 0 },
    };

    /// The shape of @a id, or nullptr when nothing carries it.
    inline ActionSpec const* SpecOf(ActionId id)
    {
        for (ActionSpec const& spec : g_actionSpecs)
        {
            if (spec.id == id)
            {
                return &spec;
            }
        }

        return nullptr;
    }

    /// The verb @a name names, or ActionId::None.
    inline ActionId ActionNamed(char const* name);
}

#endif //MANGOS_MAI_ACTIONS_GEN_H
