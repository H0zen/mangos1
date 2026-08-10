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
        U64,
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

        // threat
        ThreatChange               = 96,
        CallForHelp                = 97,
        FleeForAssist              = 98,
        ZoneCombatPulse            = 99,

        // ai
        SetPhase                   = 100,
        IncPhase                   = 101,
        RandomPhase                = 102,
        AutoAttack                 = 103,
        CombatMovement             = 104,
        RangedMovement             = 105,
        ChangeMovement             = 106,
        Evade                      = 107,
        Die                        = 108,
        SetInvincibility           = 109,
        ThrowAiEvent               = 110,
        SetThrowMask               = 111,

        // instance
        SetInstanceData            = 112,
        SetInstanceData64          = 113,

        // unit
        SetUnitField               = 114,
        SetUnitFlag                = 115,
        RemoveUnitFlag             = 116,
        SetSheath                  = 117,
        EmoteTarget                = 118,

        // quest
        QuestEvent                 = 119,
        CastEvent                  = 120,
        KilledMonster              = 121,
    };

    inline constexpr ParamSpec g_actionParamsTalk[] =
    {
        { "text0", ParamType::Text, true },
        { "text1", ParamType::Text, true },
        { "text2", ParamType::Text, true },
        { "text3", ParamType::Text, true },
    };

    inline constexpr ParamSpec g_actionParamsEmote[] =
    {
        { "emote", ParamType::Emote, false },
    };

    inline constexpr ParamSpec g_actionParamsPlaySound[] =
    {
        { "sound", ParamType::Sound, false },
        { "flags", ParamType::Flags, true },
    };

    inline constexpr ParamSpec g_actionParamsPlayMovie[] =
    {
        { "movie", ParamType::Movie, false },
    };

    inline constexpr ParamSpec g_actionParamsFieldSet[] =
    {
        { "field", ParamType::Field, false },
        { "value", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_actionParamsFlagSet[] =
    {
        { "field", ParamType::Field, false },
        { "value", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_actionParamsFlagRemove[] =
    {
        { "field", ParamType::Field, false },
        { "value", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_actionParamsMorphToEntryOrModel[] =
    {
        { "entry", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_actionParamsMountToEntryOrModel[] =
    {
        { "entry", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_actionParamsChangeEntry[] =
    {
        { "entry", ParamType::Creature, false },
    };

    inline constexpr ParamSpec g_actionParamsUpdateTemplate[] =
    {
        { "entry", ParamType::Creature, false },
        { "faction", ParamType::Faction, false },
    };

    inline constexpr ParamSpec g_actionParamsSetEquipmentSlots[] =
    {
        { "reset_default", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_actionParamsModifyNpcFlags[] =
    {
        { "flag", ParamType::Flags, false },
        { "change", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_actionParamsSetFaction[] =
    {
        { "faction", ParamType::Faction, false },
        { "flags", ParamType::Flags, true },
    };

    inline constexpr ParamSpec g_actionParamsMoveTo[] =
    {
        { "speed", ParamType::U32, true },
        { "x", ParamType::F32, true },
        { "y", ParamType::F32, true },
        { "z", ParamType::F32, true },
        { "o", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_actionParamsTeleportTo[] =
    {
        { "map", ParamType::Map, false },
        { "x", ParamType::F32, true },
        { "y", ParamType::F32, true },
        { "z", ParamType::F32, true },
        { "o", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_actionParamsMovement[] =
    {
        { "type", ParamType::U32, false },
        { "wander_distance", ParamType::U32, true },
    };

    inline constexpr ParamSpec g_actionParamsSetRun[] =
    {
        { "run", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_actionParamsTurnTo[] =
    {
        { "target", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_actionParamsMoveDynamic[] =
    {
        { "max_dist", ParamType::F32, false },
        { "min_dist", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_actionParamsSendTaxiPath[] =
    {
        { "path", ParamType::Taxi, false },
    };

    inline constexpr ParamSpec g_actionParamsPauseWaypoints[] =
    {
        { "pause", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_actionParamsSetFly[] =
    {
        { "enable", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_actionParamsStandState[] =
    {
        { "state", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_actionParamsCastSpell[] =
    {
        { "spell", ParamType::Spell, false },
        { "flags", ParamType::Flags, true },
    };

    inline constexpr ParamSpec g_actionParamsRemoveAura[] =
    {
        { "spell", ParamType::Spell, false },
    };

    inline constexpr ParamSpec g_actionParamsDespawnSelf[] =
    {
        { "delay", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_actionParamsRespawnGo[] =
    {
        { "guid", ParamType::U32, false },
        { "despawn_delay", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_actionParamsDespawnGo[] =
    {
        { "guid", ParamType::U32, false },
        { "respawn_time", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_actionParamsOpenDoor[] =
    {
        { "guid", ParamType::U32, false },
        { "reset_delay", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_actionParamsCloseDoor[] =
    {
        { "guid", ParamType::U32, false },
        { "reset_delay", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_actionParamsGoLockState[] =
    {
        { "state", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_actionParamsTempSummonCreature[] =
    {
        { "entry", ParamType::Creature, false },
        { "despawn_delay", ParamType::Ms, true },
        { "x", ParamType::F32, true },
        { "y", ParamType::F32, true },
        { "z", ParamType::F32, true },
        { "o", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_actionParamsSetActiveobject[] =
    {
        { "activate", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_actionParamsQuestExplored[] =
    {
        { "quest", ParamType::Quest, false },
        { "distance", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_actionParamsKillCredit[] =
    {
        { "entry", ParamType::Creature, false },
        { "group_credit", ParamType::Bool, true },
    };

    inline constexpr ParamSpec g_actionParamsCreateItem[] =
    {
        { "item", ParamType::Item, false },
        { "amount", ParamType::U32, true },
    };

    inline constexpr ParamSpec g_actionParamsSendMail[] =
    {
        { "template", ParamType::Mail, false },
        { "alt_sender", ParamType::Creature, true },
    };

    inline constexpr ParamSpec g_actionParamsJoinLfg[] =
    {
        { "area", ParamType::Area, false },
    };

    inline constexpr ParamSpec g_actionParamsXpUser[] =
    {
        { "flags", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_actionParamsTerminateScript[] =
    {
        { "entry", ParamType::Creature, true },
        { "search_dist", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_actionParamsTerminateCond[] =
    {
        { "condition", ParamType::U32, false },
        { "fail_quest", ParamType::Quest, true },
    };

    inline constexpr ParamSpec g_actionParamsSendAiEventAround[] =
    {
        { "event", ParamType::U32, false },
        { "radius", ParamType::F32, false },
    };

    inline constexpr ParamSpec g_actionParamsThreatChange[] =
    {
        { "percent", ParamType::I32, false },
        { "all", ParamType::Bool, true },
    };

    inline constexpr ParamSpec g_actionParamsCallForHelp[] =
    {
        { "radius", ParamType::F32, false },
    };

    inline constexpr ParamSpec g_actionParamsSetPhase[] =
    {
        { "phase", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_actionParamsIncPhase[] =
    {
        { "by", ParamType::I32, false },
    };

    inline constexpr ParamSpec g_actionParamsRandomPhase[] =
    {
        { "a", ParamType::U32, false },
        { "b", ParamType::U32, true },
        { "c", ParamType::U32, true },
    };

    inline constexpr ParamSpec g_actionParamsAutoAttack[] =
    {
        { "enable", ParamType::Bool, false },
    };

    inline constexpr ParamSpec g_actionParamsCombatMovement[] =
    {
        { "enable", ParamType::Bool, false },
        { "melee", ParamType::Bool, true },
    };

    inline constexpr ParamSpec g_actionParamsRangedMovement[] =
    {
        { "distance", ParamType::F32, false },
        { "angle", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_actionParamsChangeMovement[] =
    {
        { "type", ParamType::U32, false },
        { "wander_distance", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_actionParamsSetInvincibility[] =
    {
        { "hp", ParamType::U32, false },
        { "percent", ParamType::Bool, true },
    };

    inline constexpr ParamSpec g_actionParamsThrowAiEvent[] =
    {
        { "event", ParamType::U32, false },
        { "radius", ParamType::F32, true },
    };

    inline constexpr ParamSpec g_actionParamsSetThrowMask[] =
    {
        { "mask", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_actionParamsSetInstanceData[] =
    {
        { "field", ParamType::U32, false },
        { "value", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_actionParamsSetInstanceData64[] =
    {
        { "field", ParamType::U32, false },
        { "low", ParamType::U32, false },
        { "high", ParamType::U32, true },
    };

    inline constexpr ParamSpec g_actionParamsSetUnitField[] =
    {
        { "field", ParamType::Field, false },
        { "value", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_actionParamsSetUnitFlag[] =
    {
        { "value", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_actionParamsRemoveUnitFlag[] =
    {
        { "value", ParamType::Flags, false },
    };

    inline constexpr ParamSpec g_actionParamsSetSheath[] =
    {
        { "state", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_actionParamsEmoteTarget[] =
    {
        { "emote", ParamType::Emote, false },
    };

    inline constexpr ParamSpec g_actionParamsQuestEvent[] =
    {
        { "quest", ParamType::Quest, false },
        { "all", ParamType::Bool, true },
    };

    inline constexpr ParamSpec g_actionParamsCastEvent[] =
    {
        { "creature", ParamType::Creature, false },
        { "spell", ParamType::Spell, false },
        { "all", ParamType::Bool, true },
    };

    inline constexpr ParamSpec g_actionParamsKilledMonster[] =
    {
        { "creature", ParamType::Creature, false },
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
        { ActionId::Talk, "talk", g_actionParamsTalk, 4, 0, FacetTexts },
        { ActionId::Emote, "emote", g_actionParamsEmote, 1, 1, 0 },
        { ActionId::PlaySound, "play_sound", g_actionParamsPlaySound, 2, 2, 0 },
        { ActionId::PlayMovie, "play_movie", g_actionParamsPlayMovie, 1, 1, 0 },
        { ActionId::FieldSet, "field_set", g_actionParamsFieldSet, 2, 2, 0 },
        { ActionId::FlagSet, "flag_set", g_actionParamsFlagSet, 2, 2, 0 },
        { ActionId::FlagRemove, "flag_remove", g_actionParamsFlagRemove, 2, 2, 0 },
        { ActionId::MorphToEntryOrModel, "morph_to_entry_or_model", g_actionParamsMorphToEntryOrModel, 1, 1, 0 },
        { ActionId::MountToEntryOrModel, "mount_to_entry_or_model", g_actionParamsMountToEntryOrModel, 1, 1, 0 },
        { ActionId::ChangeEntry, "change_entry", g_actionParamsChangeEntry, 1, 1, 0 },
        { ActionId::UpdateTemplate, "update_template", g_actionParamsUpdateTemplate, 2, 2, 0 },
        { ActionId::SetEquipmentSlots, "set_equipment_slots", g_actionParamsSetEquipmentSlots, 1, 1, 0 },
        { ActionId::ModifyNpcFlags, "modify_npc_flags", g_actionParamsModifyNpcFlags, 2, 2, 0 },
        { ActionId::SetFaction, "set_faction", g_actionParamsSetFaction, 2, 2, 0 },
        { ActionId::MoveTo, "move_to", g_actionParamsMoveTo, 5, 1, FacetAt },
        { ActionId::TeleportTo, "teleport_to", g_actionParamsTeleportTo, 5, 1, FacetAt },
        { ActionId::Movement, "movement", g_actionParamsMovement, 2, 2, 0 },
        { ActionId::SetRun, "set_run", g_actionParamsSetRun, 1, 1, 0 },
        { ActionId::TurnTo, "turn_to", g_actionParamsTurnTo, 1, 1, 0 },
        { ActionId::MoveDynamic, "move_dynamic", g_actionParamsMoveDynamic, 2, 2, 0 },
        { ActionId::SendTaxiPath, "send_taxi_path", g_actionParamsSendTaxiPath, 1, 1, 0 },
        { ActionId::PauseWaypoints, "pause_waypoints", g_actionParamsPauseWaypoints, 1, 1, 0 },
        { ActionId::SetFly, "set_fly", g_actionParamsSetFly, 1, 1, 0 },
        { ActionId::StandState, "stand_state", g_actionParamsStandState, 1, 1, 0 },
        { ActionId::CastSpell, "cast_spell", g_actionParamsCastSpell, 2, 2, 0 },
        { ActionId::RemoveAura, "remove_aura", g_actionParamsRemoveAura, 1, 1, 0 },
        { ActionId::AttackStart, "attack_start", nullptr, 0, 0, 0 },
        { ActionId::DespawnSelf, "despawn_self", g_actionParamsDespawnSelf, 1, 1, 0 },
        { ActionId::Respawn, "respawn", nullptr, 0, 0, 0 },
        { ActionId::RespawnGo, "respawn_go", g_actionParamsRespawnGo, 2, 2, 0 },
        { ActionId::DespawnGo, "despawn_go", g_actionParamsDespawnGo, 2, 2, 0 },
        { ActionId::OpenDoor, "open_door", g_actionParamsOpenDoor, 2, 2, 0 },
        { ActionId::CloseDoor, "close_door", g_actionParamsCloseDoor, 2, 2, 0 },
        { ActionId::ActivateObject, "activate_object", nullptr, 0, 0, 0 },
        { ActionId::ResetGo, "reset_go", nullptr, 0, 0, 0 },
        { ActionId::GoLockState, "go_lock_state", g_actionParamsGoLockState, 1, 1, 0 },
        { ActionId::TempSummonCreature, "temp_summon_creature", g_actionParamsTempSummonCreature, 6, 2, FacetAt },
        { ActionId::SetActiveobject, "set_activeobject", g_actionParamsSetActiveobject, 1, 1, 0 },
        { ActionId::QuestExplored, "quest_explored", g_actionParamsQuestExplored, 2, 2, 0 },
        { ActionId::KillCredit, "kill_credit", g_actionParamsKillCredit, 2, 2, 0 },
        { ActionId::CreateItem, "create_item", g_actionParamsCreateItem, 2, 2, 0 },
        { ActionId::SendMail, "send_mail", g_actionParamsSendMail, 2, 2, 0 },
        { ActionId::JoinLfg, "join_lfg", g_actionParamsJoinLfg, 1, 1, 0 },
        { ActionId::XpUser, "xp_user", g_actionParamsXpUser, 1, 1, 0 },
        { ActionId::TerminateScript, "terminate_script", g_actionParamsTerminateScript, 2, 2, 0 },
        { ActionId::TerminateCond, "terminate_cond", g_actionParamsTerminateCond, 2, 2, 0 },
        { ActionId::SendAiEventAround, "send_ai_event_around", g_actionParamsSendAiEventAround, 2, 2, 0 },
        { ActionId::ThreatChange, "threat_change", g_actionParamsThreatChange, 2, 2, 0 },
        { ActionId::CallForHelp, "call_for_help", g_actionParamsCallForHelp, 1, 1, 0 },
        { ActionId::FleeForAssist, "flee_for_assist", nullptr, 0, 0, 0 },
        { ActionId::ZoneCombatPulse, "zone_combat_pulse", nullptr, 0, 0, 0 },
        { ActionId::SetPhase, "set_phase", g_actionParamsSetPhase, 1, 1, 0 },
        { ActionId::IncPhase, "inc_phase", g_actionParamsIncPhase, 1, 1, 0 },
        { ActionId::RandomPhase, "random_phase", g_actionParamsRandomPhase, 3, 3, 0 },
        { ActionId::AutoAttack, "auto_attack", g_actionParamsAutoAttack, 1, 1, 0 },
        { ActionId::CombatMovement, "combat_movement", g_actionParamsCombatMovement, 2, 2, 0 },
        { ActionId::RangedMovement, "ranged_movement", g_actionParamsRangedMovement, 2, 2, 0 },
        { ActionId::ChangeMovement, "change_movement", g_actionParamsChangeMovement, 2, 2, 0 },
        { ActionId::Evade, "evade", nullptr, 0, 0, 0 },
        { ActionId::Die, "die", nullptr, 0, 0, 0 },
        { ActionId::SetInvincibility, "set_invincibility", g_actionParamsSetInvincibility, 2, 2, 0 },
        { ActionId::ThrowAiEvent, "throw_ai_event", g_actionParamsThrowAiEvent, 2, 2, 0 },
        { ActionId::SetThrowMask, "set_throw_mask", g_actionParamsSetThrowMask, 1, 1, 0 },
        { ActionId::SetInstanceData, "set_instance_data", g_actionParamsSetInstanceData, 2, 2, 0 },
        { ActionId::SetInstanceData64, "set_instance_data64", g_actionParamsSetInstanceData64, 3, 3, 0 },
        { ActionId::SetUnitField, "set_unit_field", g_actionParamsSetUnitField, 2, 2, 0 },
        { ActionId::SetUnitFlag, "set_unit_flag", g_actionParamsSetUnitFlag, 1, 1, 0 },
        { ActionId::RemoveUnitFlag, "remove_unit_flag", g_actionParamsRemoveUnitFlag, 1, 1, 0 },
        { ActionId::SetSheath, "set_sheath", g_actionParamsSetSheath, 1, 1, 0 },
        { ActionId::EmoteTarget, "emote_target", g_actionParamsEmoteTarget, 1, 1, 0 },
        { ActionId::QuestEvent, "quest_event", g_actionParamsQuestEvent, 2, 2, 0 },
        { ActionId::CastEvent, "cast_event", g_actionParamsCastEvent, 3, 3, 0 },
        { ActionId::KilledMonster, "killed_monster", g_actionParamsKilledMonster, 1, 1, 0 },
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
}

#endif //MANGOS_MAI_ACTIONS_GEN_H
