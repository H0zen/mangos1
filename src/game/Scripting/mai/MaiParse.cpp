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

// Reading a step back out of the table.
//
// `params` is "name=value name=value", and every name and type in it comes
// from the manifest -- which is what makes this readable to a person and still
// checkable by a machine. A misspelt parameter is a load error naming the
// parameter and the verb; a value of the wrong shape is the same. Neither is
// expressible in a schema of six columns called datalong.
//
// The parse is strict on purpose. Silently ignoring an unrecognised name would
// turn a typo into a step that quietly does less than it says, which is the
// exact failure the old six columns had and the whole reason for this format.

#include "MaiParse.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mai
{
    namespace
    {
        bool IsSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        /// The slot @a name occupies in @a spec, or the arity when it has none.
        std::size_t SlotOf(ActionSpec const& spec, char const* name,
                           std::size_t length)
        {
            for (std::size_t slot = 0; slot < spec.arity; ++slot)
            {
                char const* candidate = spec.params[slot].name;
                if (std::strlen(candidate) == length &&
                    std::strncmp(candidate, name, length) == 0)
                {
                    return slot;
                }
            }
            return spec.arity;
        }
    }

    ActionId ActionNamed(char const* name)
    {
        if (!name)
        {
            return ActionId::None;
        }

        for (ActionSpec const& spec : g_actionSpecs)
        {
            if (std::strcmp(spec.name, name) == 0)
            {
                return spec.id;
            }
        }

        return ActionId::None;
    }

    bool Parse(char const* action, char const* params, Step& out,
               std::string& error)
    {
        char buffer[256];

        ActionId const id = ActionNamed(action);
        if (id == ActionId::None)
        {
            std::snprintf(buffer, sizeof(buffer),
                          "'%s' is not an action MAI has",
                          action ? action : "");
            error = buffer;
            return false;
        }

        ActionSpec const* spec = SpecOf(id);
        out = Step();
        out.action = id;

        char const* at = params ? params : "";
        while (*at)
        {
            while (IsSpace(*at))
            {
                ++at;
            }
            if (!*at)
            {
                break;
            }

            char const* name = at;
            while (*at && *at != '=' && !IsSpace(*at))
            {
                ++at;
            }
            std::size_t const length = std::size_t(at - name);

            if (*at != '=')
            {
                std::snprintf(buffer, sizeof(buffer),
                              "%s: '%.*s' has no value; the form is name=value",
                              spec->name, int(length), name);
                error = buffer;
                return false;
            }
            ++at;

            char const* value = at;
            while (*at && !IsSpace(*at))
            {
                ++at;
            }

            std::size_t const slot = SlotOf(*spec, name, length);
            if (slot == spec->arity)
            {
                // Refused, not ignored. An ignored name is a typo that becomes
                // a step quietly doing less than it says -- the failure mode
                // the six datalong columns had, and the reason for this format.
                std::snprintf(buffer, sizeof(buffer),
                              "%s has no parameter '%.*s'",
                              spec->name, int(length), name);
                error = buffer;
                return false;
            }

            std::string const text(value, std::size_t(at - value));
            ParamType const type = spec->params[slot].type;

            char* end = nullptr;
            if (type == ParamType::F32)
            {
                out.operands[slot].f = float(std::strtod(text.c_str(), &end));
            }
            else if (type == ParamType::I32 || type == ParamType::Text)
            {
                out.operands[slot].i = int32(std::strtol(text.c_str(), &end,
                                                         10));
            }
            else
            {
                out.operands[slot].u = uint32(std::strtoul(text.c_str(), &end,
                                                           10));
            }

            if (end == text.c_str() || (end && *end))
            {
                std::snprintf(buffer, sizeof(buffer),
                              "%s.%s is '%s', which is not a number",
                              spec->name, spec->params[slot].name,
                              text.c_str());
                error = buffer;
                return false;
            }

            out.given |= uint8(1u << slot);
        }

        // A parameter the verb requires and the row did not give.
        for (std::size_t slot = 0; slot < spec->arity; ++slot)
        {
            if (!spec->params[slot].optional && !out.Has(slot))
            {
                std::snprintf(buffer, sizeof(buffer),
                              "%s needs %s and the row does not give it",
                              spec->name, spec->params[slot].name);
                error = buffer;
                return false;
            }
        }

        return true;
    }
}
