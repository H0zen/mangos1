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

// GENERATED FROM rules.manifest -- DO NOT EDIT.
// Regenerate with: python src/game/Scripting/mai/tools/gen_actions.py

#ifndef MANGOS_MAI_RULES_GEN_H
#define MANGOS_MAI_RULES_GEN_H

#include "MaiActions.gen.h"

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
    enum class RuleId : uint16
    {
        None = 0xFFFF,

        // combat
        TimerInCombat              = 0,
        TimerOoc                   = 1,
        HealthBelow                = 2,
        ManaBelow                  = 3,
        Aggro                      = 4,
        KilledUnit                 = 5,
        Died                       = 6,
        Evaded                     = 7,
        HitBySpell                 = 8,
        TargetInRange              = 9,
        EnergyBelow                = 32,

        // sight
        SawUnit                    = 10,
        Spawned                    = 11,
        ReachedWaypoint            = 31,
        ReachedHome                = 21,
        ReceivedEmote              = 22,
        ReceivedAiEvent            = 30,

        // target
        TargetHealthBelow          = 12,
        TargetCasting              = 13,
        TargetManaBelow            = 18,
        TargetHasAura              = 24,
        TargetMissingAura          = 28,

        // self
        HasAura                    = 23,
        MissingAura                = 27,
        Timer                      = 29,

        // friendly
        FriendlyHurt               = 14,
        FriendlyControlled         = 15,
        FriendlyMissingBuff        = 16,

        // summons
        SummonedUnit               = 17,
        SummonDied                 = 25,
        SummonDespawned            = 26,

        // quests
        QuestAccepted              = 19,
        QuestCompleted             = 20,

        // world
        QuestStarted               = 64,
        QuestFinished              = 65,
        SpellEffect                = 66,
        GameobjectUsed             = 67,
        GameobjectTemplateUsed     = 68,
        CreatureDied               = 69,
        ReachedMovementPoint       = 70,
        GossipOptionChosen         = 71,
        EventRaised                = 72,
    };

    inline constexpr ParamSpec g_ruleParamsTimerInCombat[] =
    {
        { "initial", ParamType::Ms, false },
        { "initial_max", ParamType::Ms, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsTimerOoc[] =
    {
        { "initial", ParamType::Ms, false },
        { "initial_max", ParamType::Ms, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsHealthBelow[] =
    {
        { "percent_max", ParamType::U32, false },
        { "percent_min", ParamType::U32, false },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsManaBelow[] =
    {
        { "percent_max", ParamType::U32, false },
        { "percent_min", ParamType::U32, false },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsKilledUnit[] =
    {
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsHitBySpell[] =
    {
        { "spell", ParamType::Spell, true },
        { "school", ParamType::Flags, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsTargetInRange[] =
    {
        { "min", ParamType::F32, false },
        { "max", ParamType::F32, false },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsEnergyBelow[] =
    {
        { "percent_max", ParamType::U32, false },
        { "percent_min", ParamType::U32, false },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsSawUnit[] =
    {
        { "in_combat", ParamType::Bool, false },
        { "range", ParamType::F32, false },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsSpawned[] =
    {
        { "condition", ParamType::U32, true },
        { "value", ParamType::U32, true },
    };

    inline constexpr ParamSpec g_ruleParamsReachedWaypoint[] =
    {
        { "waypoint", ParamType::U32, true },
        { "path", ParamType::U32, true },
    };

    inline constexpr ParamSpec g_ruleParamsReceivedEmote[] =
    {
        { "emote", ParamType::Emote, false },
        { "condition", ParamType::U32, true },
        { "value", ParamType::U32, true },
    };

    inline constexpr ParamSpec g_ruleParamsReceivedAiEvent[] =
    {
        { "event", ParamType::U32, false },
        { "sender", ParamType::Creature, true },
    };

    inline constexpr ParamSpec g_ruleParamsTargetHealthBelow[] =
    {
        { "percent_max", ParamType::U32, false },
        { "percent_min", ParamType::U32, false },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsTargetCasting[] =
    {
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsTargetManaBelow[] =
    {
        { "percent_max", ParamType::U32, false },
        { "percent_min", ParamType::U32, false },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsTargetHasAura[] =
    {
        { "spell", ParamType::Spell, false },
        { "stacks", ParamType::U32, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsTargetMissingAura[] =
    {
        { "spell", ParamType::Spell, false },
        { "stacks", ParamType::U32, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsHasAura[] =
    {
        { "spell", ParamType::Spell, false },
        { "stacks", ParamType::U32, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsMissingAura[] =
    {
        { "spell", ParamType::Spell, false },
        { "stacks", ParamType::U32, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsTimer[] =
    {
        { "initial", ParamType::Ms, false },
        { "initial_max", ParamType::Ms, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsFriendlyHurt[] =
    {
        { "radius", ParamType::F32, false },
        { "missing_hp", ParamType::U32, false },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsFriendlyControlled[] =
    {
        { "radius", ParamType::F32, false },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsFriendlyMissingBuff[] =
    {
        { "radius", ParamType::F32, false },
        { "spell", ParamType::Spell, false },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsSummonedUnit[] =
    {
        { "creature", ParamType::Creature, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsSummonDied[] =
    {
        { "creature", ParamType::Creature, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsSummonDespawned[] =
    {
        { "creature", ParamType::Creature, true },
        { "repeat", ParamType::Ms, true },
        { "repeat_max", ParamType::Ms, true },
    };

    inline constexpr ParamSpec g_ruleParamsQuestAccepted[] =
    {
        { "quest", ParamType::Quest, false },
    };

    inline constexpr ParamSpec g_ruleParamsQuestCompleted[] =
    {
        { "quest", ParamType::Quest, false },
    };

    inline constexpr ParamSpec g_ruleParamsQuestStarted[] =
    {
        { "quest", ParamType::Quest, false },
    };

    inline constexpr ParamSpec g_ruleParamsQuestFinished[] =
    {
        { "quest", ParamType::Quest, false },
    };

    inline constexpr ParamSpec g_ruleParamsSpellEffect[] =
    {
        { "spell", ParamType::Spell, false },
    };

    inline constexpr ParamSpec g_ruleParamsGameobjectUsed[] =
    {
        { "guid", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_ruleParamsGameobjectTemplateUsed[] =
    {
        { "entry", ParamType::Gameobject, false },
    };

    inline constexpr ParamSpec g_ruleParamsCreatureDied[] =
    {
        { "creature", ParamType::Creature, false },
    };

    inline constexpr ParamSpec g_ruleParamsReachedMovementPoint[] =
    {
        { "point", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_ruleParamsGossipOptionChosen[] =
    {
        { "menu", ParamType::U32, false },
    };

    inline constexpr ParamSpec g_ruleParamsEventRaised[] =
    {
        { "event", ParamType::U32, false },
    };

    /// Which shared facets an action carries, and therefore
    /// where its own parameters stop and theirs begin. The
    /// lowering from `dbscripts_on_*` reads this instead of
    /// having a case per verb: a DB row is two datalongs plus
    /// exactly these facets, so knowing which ones apply is
    /// the whole of the mapping.
    enum Facet : uint8
    {
    };

    struct RuleSpec
    {
        RuleId            id;
        char const*       name;       ///< "cast_spell"
        ParamSpec const*  params;
        std::size_t       arity;      ///< own parameters + facets
        std::size_t       own;        ///< how many are the verb's own
        uint8             facets;     ///< a mask of Facet
    };

    inline constexpr RuleSpec g_ruleSpecs[] =
    {
        { RuleId::TimerInCombat, "timer_in_combat", g_ruleParamsTimerInCombat, 4, 4, 0 },
        { RuleId::TimerOoc, "timer_ooc", g_ruleParamsTimerOoc, 4, 4, 0 },
        { RuleId::HealthBelow, "health_below", g_ruleParamsHealthBelow, 4, 4, 0 },
        { RuleId::ManaBelow, "mana_below", g_ruleParamsManaBelow, 4, 4, 0 },
        { RuleId::Aggro, "aggro", nullptr, 0, 0, 0 },
        { RuleId::KilledUnit, "killed_unit", g_ruleParamsKilledUnit, 2, 2, 0 },
        { RuleId::Died, "died", nullptr, 0, 0, 0 },
        { RuleId::Evaded, "evaded", nullptr, 0, 0, 0 },
        { RuleId::HitBySpell, "hit_by_spell", g_ruleParamsHitBySpell, 4, 4, 0 },
        { RuleId::TargetInRange, "target_in_range", g_ruleParamsTargetInRange, 4, 4, 0 },
        { RuleId::EnergyBelow, "energy_below", g_ruleParamsEnergyBelow, 4, 4, 0 },
        { RuleId::SawUnit, "saw_unit", g_ruleParamsSawUnit, 4, 4, 0 },
        { RuleId::Spawned, "spawned", g_ruleParamsSpawned, 2, 2, 0 },
        { RuleId::ReachedWaypoint, "reached_waypoint", g_ruleParamsReachedWaypoint, 2, 2, 0 },
        { RuleId::ReachedHome, "reached_home", nullptr, 0, 0, 0 },
        { RuleId::ReceivedEmote, "received_emote", g_ruleParamsReceivedEmote, 3, 3, 0 },
        { RuleId::ReceivedAiEvent, "received_ai_event", g_ruleParamsReceivedAiEvent, 2, 2, 0 },
        { RuleId::TargetHealthBelow, "target_health_below", g_ruleParamsTargetHealthBelow, 4, 4, 0 },
        { RuleId::TargetCasting, "target_casting", g_ruleParamsTargetCasting, 2, 2, 0 },
        { RuleId::TargetManaBelow, "target_mana_below", g_ruleParamsTargetManaBelow, 4, 4, 0 },
        { RuleId::TargetHasAura, "target_has_aura", g_ruleParamsTargetHasAura, 4, 4, 0 },
        { RuleId::TargetMissingAura, "target_missing_aura", g_ruleParamsTargetMissingAura, 4, 4, 0 },
        { RuleId::HasAura, "has_aura", g_ruleParamsHasAura, 4, 4, 0 },
        { RuleId::MissingAura, "missing_aura", g_ruleParamsMissingAura, 4, 4, 0 },
        { RuleId::Timer, "timer", g_ruleParamsTimer, 4, 4, 0 },
        { RuleId::FriendlyHurt, "friendly_hurt", g_ruleParamsFriendlyHurt, 4, 4, 0 },
        { RuleId::FriendlyControlled, "friendly_controlled", g_ruleParamsFriendlyControlled, 3, 3, 0 },
        { RuleId::FriendlyMissingBuff, "friendly_missing_buff", g_ruleParamsFriendlyMissingBuff, 4, 4, 0 },
        { RuleId::SummonedUnit, "summoned_unit", g_ruleParamsSummonedUnit, 3, 3, 0 },
        { RuleId::SummonDied, "summon_died", g_ruleParamsSummonDied, 3, 3, 0 },
        { RuleId::SummonDespawned, "summon_despawned", g_ruleParamsSummonDespawned, 3, 3, 0 },
        { RuleId::QuestAccepted, "quest_accepted", g_ruleParamsQuestAccepted, 1, 1, 0 },
        { RuleId::QuestCompleted, "quest_completed", g_ruleParamsQuestCompleted, 1, 1, 0 },
        { RuleId::QuestStarted, "quest_started", g_ruleParamsQuestStarted, 1, 1, 0 },
        { RuleId::QuestFinished, "quest_finished", g_ruleParamsQuestFinished, 1, 1, 0 },
        { RuleId::SpellEffect, "spell_effect", g_ruleParamsSpellEffect, 1, 1, 0 },
        { RuleId::GameobjectUsed, "gameobject_used", g_ruleParamsGameobjectUsed, 1, 1, 0 },
        { RuleId::GameobjectTemplateUsed, "gameobject_template_used", g_ruleParamsGameobjectTemplateUsed, 1, 1, 0 },
        { RuleId::CreatureDied, "creature_died", g_ruleParamsCreatureDied, 1, 1, 0 },
        { RuleId::ReachedMovementPoint, "reached_movement_point", g_ruleParamsReachedMovementPoint, 1, 1, 0 },
        { RuleId::GossipOptionChosen, "gossip_option_chosen", g_ruleParamsGossipOptionChosen, 1, 1, 0 },
        { RuleId::EventRaised, "event_raised", g_ruleParamsEventRaised, 1, 1, 0 },
    };

    /// The shape of @a id, or nullptr when nothing carries it.
    inline RuleSpec const* SpecOf(RuleId id)
    {
        for (RuleSpec const& spec : g_ruleSpecs)
        {
            if (spec.id == id)
            {
                return &spec;
            }
        }

        return nullptr;
    }
}

#endif //MANGOS_MAI_RULES_GEN_H
