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

// GENERATED FROM eventai.map -- DO NOT EDIT.
// Regenerate with: python src/game/Scripting/mai/tools/gen_actions.py

#ifndef MANGOS_MAI_EVENTAI_MAP_GEN_H
#define MANGOS_MAI_EVENTAI_MAP_GEN_H

#include "MaiActions.gen.h"

#include <cstddef>

namespace mai
{
    /// A column that is not one of the verb's parameters.
    enum : uint8
    {
        MapUnused = 0xFF,   ///< EventAI does not use this column
        MapSelect = 0xFE,   ///< this column is the target selector
        MapNoPin  = 0xFF    ///< the verb pins no parameter
    };

    /// A shape no column mapping can express. Not composable: a
    /// verb has one of these or none.
    enum MapForm : uint8
    {
        FormPlain          = 0,
        FormEntryOrModel   = 1,
        FormSummonSpawn    = 2,
    };

    struct EventAiVerb
    {
        uint32      type;       ///< EventAI's action_type
        ActionId    action;
        uint8       slot[3];    ///< where param1..3 land
        uint8       flags;      ///< TargetFlags for the step
        uint8       pinSlot;
        uint32      pinValue;
        MapForm     form;
        char const* refused;    ///< why, when it does not map
    };

    inline constexpr EventAiVerb g_eventAiVerbs[] =
    {
        {   1, ActionId::Talk,
          { 0, 1, 2 }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {   4, ActionId::PlaySound,
          { 0, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {   5, ActionId::Emote,
          { 0, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {   9, ActionId::RandomSound,
          { 0, 1, 2 }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  10, ActionId::RandomEmote,
          { 0, 1, 2 }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {   2, ActionId::SetFaction,
          { 0, 1, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {   3, ActionId::MorphToEntryOrModel,
          { MapUnused, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormEntryOrModel, nullptr },
        {  43, ActionId::MountToEntryOrModel,
          { MapUnused, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormEntryOrModel, nullptr },
        {  36, ActionId::UpdateTemplate,
          { 0, 1, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  47, ActionId::StandState,
          { 0, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  11, ActionId::CastSpell,
          { 0, MapSelect, 1 }, 0x01, MapNoPin, 0, FormPlain, nullptr },
        {  12, ActionId::TempSummonCreature,
          { 0, MapSelect, 1 }, 0x01, MapNoPin, 0, FormPlain, nullptr },
        {  32, ActionId::TempSummonCreature,
          { 0, MapSelect, MapUnused }, 0x01, MapNoPin, 0, FormSummonSpawn, nullptr },
        {  49, ActionId::TempSummonCreature,
          { 0, MapSelect, MapUnused }, 0x01, MapNoPin, 0, FormSummonSpawn, nullptr },
        {  28, ActionId::RemoveAura,
          { MapSelect, 0, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  13, ActionId::ThreatChange,
          { 0, MapSelect, MapUnused }, 0x01, 1, 0, FormPlain, nullptr },
        {  14, ActionId::ThreatChange,
          { 0, MapUnused, MapUnused }, 0x00, 1, 1, FormPlain, nullptr },
        {  20, ActionId::AutoAttack,
          { 0, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  21, ActionId::CombatMovement,
          { 0, 1, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  29, ActionId::RangedMovement,
          { 0, 1, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  48, ActionId::ChangeMovement,
          { 0, 1, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  24, ActionId::Evade,
          { MapUnused, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  25, ActionId::FleeForAssist,
          { MapUnused, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  37, ActionId::Die,
          { MapUnused, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  38, ActionId::ZoneCombatPulse,
          { MapUnused, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  39, ActionId::CallForHelp,
          { 0, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  41, ActionId::DespawnSelf,
          { 0, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  42, ActionId::SetInvincibility,
          { 0, 1, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  40, ActionId::SetSheath,
          { 0, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  22, ActionId::SetPhase,
          { 0, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  23, ActionId::IncPhase,
          { 0, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  30, ActionId::RandomPhase,
          { 0, 1, 2 }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  31, ActionId::RandomPhaseRange,
          { 0, 1, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  17, ActionId::SetUnitField,
          { 0, 1, MapSelect }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  18, ActionId::SetUnitFlag,
          { 0, MapSelect, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  19, ActionId::RemoveUnitFlag,
          { 0, MapSelect, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  15, ActionId::QuestEvent,
          { 0, MapSelect, MapUnused }, 0x01, 1, 0, FormPlain, nullptr },
        {  26, ActionId::QuestEvent,
          { 0, 1, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  16, ActionId::CastEvent,
          { 0, 1, MapSelect }, 0x01, 2, 0, FormPlain, nullptr },
        {  27, ActionId::CastEvent,
          { 0, 1, MapUnused }, 0x00, 2, 1, FormPlain, nullptr },
        {  33, ActionId::KilledMonster,
          { 0, MapSelect, MapUnused }, 0x01, MapNoPin, 0, FormPlain, nullptr },
        {  34, ActionId::SetInstanceData,
          { 0, 1, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  35, ActionId::SetInstanceDataGuid,
          { 0, MapSelect, MapUnused }, 0x01, MapNoPin, 0, FormPlain, nullptr },
        {  45, ActionId::ThrowAiEvent,
          { 0, 1, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {  46, ActionId::SetThrowMask,
          { 0, MapUnused, MapUnused }, 0x00, MapNoPin, 0, FormPlain, nullptr },
        {   6, ActionId::None, { MapUnused, MapUnused, MapUnused }, 0, MapNoPin, 0,
          FormPlain,
          "unused in this schema -- EventAI's own header says so" },
        {   7, ActionId::None, { MapUnused, MapUnused, MapUnused }, 0, MapNoPin, 0,
          FormPlain,
          "unused in this schema -- EventAI's own header says so" },
        {   8, ActionId::None, { MapUnused, MapUnused, MapUnused }, 0, MapNoPin, 0,
          FormPlain,
          "unused in this schema -- EventAI's own header says so" },
        {  44, ActionId::None, { MapUnused, MapUnused, MapUnused }, 0, MapNoPin, 0,
          FormPlain,
          "carries a chance of its own, and chance in MAI belongs to the rule rather than to one of its steps" },
        {  50, ActionId::None, { MapUnused, MapUnused, MapUnused }, 0, MapNoPin, 0,
          FormPlain,
          "names a creature by bare guid with no entry, and the buddy-by-guid search needs the entry to build one" },
    };

    /// How @a type maps, or nullptr when nothing says.
    inline EventAiVerb const* EventAiVerbOf(uint32 type)
    {
        for (EventAiVerb const& verb : g_eventAiVerbs)
        {
            if (verb.type == type)
            {
                return &verb;
            }
        }

        return nullptr;
    }
}

#endif //MANGOS_MAI_EVENTAI_MAP_GEN_H
