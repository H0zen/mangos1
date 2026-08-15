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

#include "combat/pure/ProcPoints.h"

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

        /**
         * Base 10, or base 16 when the value says so with `0x`.
         *
         * A `flags` parameter IS a bitmask -- the manifest says so and prints
         * it in hex -- so `value=0x02000000` is the form a person writes and
         * the form every hand port used. Read in base 10 it stops at the `x`,
         * and the row is refused with "which is not a number" over a value
         * that is perfectly well formed. Seven of Nefarian's rules and four
         * aura scripts went that way.
         *
         * NOT `strtoul`'s base 0, which would also read a leading zero as
         * octal: `010` means ten to everyone writing a table and eight to C.
         * Nothing in the manifest is octal, so nothing here should guess it.
         */
        int BaseOf(std::string const& text)
        {
            std::size_t at = 0;
            if (at < text.size() && (text[at] == '+' || text[at] == '-'))
            {
                ++at;
            }

            return (at + 1 < text.size() && text[at] == '0' &&
                    (text[at + 1] == 'x' || text[at + 1] == 'X')) ? 16 : 10;
        }

        /// The slot @a name occupies in @a spec, or the arity when it has none.
        ///
        /// Templated over the spec, and so is everything below it. ActionSpec
        /// and RuleSpec are the same shape because ONE generator emits both --
        /// id, name, params, arity, own, facets -- so a rule's parameters are
        /// read by the very code that reads a step's rather than by a copy of
        /// it that drifts.
        template <class Spec>
        std::size_t SlotOf(Spec const& spec, char const* name,
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

        /// fills in is an operand array and a given mask, which is all a step
        /// and a rule have in common -- and all this needs of either.
        template <class Spec>
        bool Fill(Spec const& spec, char const* params, Operand* operands,
                  uint16& given, RuleSet* owner, std::string& error)
            {
            char buffer[256];

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
                                  spec.name, int(length), name);
                    error = buffer;
                    return false;
                }
                ++at;

                char const* value = at;
                while (*at && !IsSpace(*at))
                {
                    ++at;
                }

                std::size_t const slot = SlotOf(spec, name, length);
                if (slot == spec.arity)
                {
                    // Refused, not ignored. An ignored name is a typo that becomes
                    // a step quietly doing less than it says -- the failure mode
                    // the six datalong columns had, and the reason for this format.
                    std::snprintf(buffer, sizeof(buffer),
                                  "%s has no parameter '%.*s'",
                                  spec.name, int(length), name);
                    error = buffer;
                    return false;
                }

                std::string const text(value, std::size_t(at - value));
                ParamType const type = spec.params[slot].type;

                if (type == ParamType::State)
                {
                    // A name, not a number, and the only parameter type that
                    // is. Interning needs the creature it belongs to, which is
                    // why the caller has to supply one.
                    if (!owner)
                    {
                        std::snprintf(buffer, sizeof(buffer),
                                      "%s.%s names a remembered value and "
                                      "nothing said whose",
                                      spec.name, spec.params[slot].name);
                        error = buffer;
                        return false;
                    }

                    if (RuleSet::Reserved(text))
                    {
                        std::snprintf(buffer, sizeof(buffer),
                                      "%s.%s is '%s', which is not one of this "
                                      "creature's remembered numbers -- use "
                                      "set_phase, and guard on it by name",
                                      spec.name, spec.params[slot].name,
                                      text.c_str());
                        error = buffer;
                        return false;
                    }

                    std::size_t const at = owner->Intern(text);
                    if (at >= MaxStates)
                    {
                        std::snprintf(buffer, sizeof(buffer),
                                      "%s.%s is '%s' and this creature already "
                                      "remembers %u things",
                                      spec.name, spec.params[slot].name,
                                      text.c_str(), uint32(MaxStates));
                        error = buffer;
                        return false;
                    }

                    operands[slot].u = uint32(at);
                    given |= uint16(1u << slot);
                    continue;
                }

                if (type == ParamType::PointsSource ||
                    type == ParamType::PointsScale)
                {
                    // A word from a closed set. Refused by name, with the set
                    // it had to come from, because "damge" is otherwise a
                    // spell that computes its damage from nothing.
                    bool known = false;

                    if (type == ParamType::PointsSource)
                    {
                        Combat::PointsSource source = Combat::PointsSource::None;
                        known = Combat::ParsePointsSource(text.c_str(), source);
                        operands[slot].u = uint32(source);
                    }
                    else
                    {
                        Combat::PointsScale scale = Combat::PointsScale::Literal;
                        known = Combat::ParsePointsScale(text.c_str(), scale);
                        operands[slot].u = uint32(scale);
                    }

                    if (!known)
                    {
                        std::snprintf(buffer, sizeof(buffer),
                                      "%s.%s is '%s', which is not one of the "
                                      "names this parameter takes",
                                      spec.name, spec.params[slot].name,
                                      text.c_str());
                        error = buffer;
                        return false;
                    }

                    given |= uint16(1u << slot);
                    continue;
                }

                char* end = nullptr;
                if (type == ParamType::F32)
                {
                    operands[slot].f = float(std::strtod(text.c_str(), &end));
                }
                else if (type == ParamType::I32 || type == ParamType::Text)
                {
                    operands[slot].i = int32(std::strtol(text.c_str(), &end,
                                                             BaseOf(text)));
                }
                else
                {
                    operands[slot].u = uint32(std::strtoul(text.c_str(), &end,
                                                               BaseOf(text)));
                }

                if (end == text.c_str() || (end && *end))
                {
                    std::snprintf(buffer, sizeof(buffer),
                                  "%s.%s is '%s', which is not a number",
                                  spec.name, spec.params[slot].name,
                                  text.c_str());
                    error = buffer;
                    return false;
                }

                given |= uint16(1u << slot);
            }

            // A parameter the verb requires and the row did not give.
            for (std::size_t slot = 0; slot < spec.arity; ++slot)
            {
                if (!spec.params[slot].optional && !(given & (1u << slot)))
                {
                    std::snprintf(buffer, sizeof(buffer),
                                  "%s needs %s and the row does not give it",
                                  spec.name, spec.params[slot].name);
                    error = buffer;
                    return false;
                }
            }

            return true;
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
        ActionId const id = ActionNamed(action);
        if (id == ActionId::None)
        {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer),
                          "'%s' is not an action MAI has", action ? action : "");
            error = buffer;
            return false;
        }

        out = Step();
        out.action = id;
        return Fill(*SpecOf(id), params, out.operands, out.given, nullptr,
                    error);
    }

    RuleId RuleNamed(char const* name)
    {
        if (!name)
        {
            return RuleId::None;
        }

        for (RuleSpec const& spec : g_ruleSpecs)
        {
            if (std::strcmp(spec.name, name) == 0)
            {
                return spec.id;
            }
        }

        return RuleId::None;
    }

    bool Parse(char const* trigger, char const* params, Rule& out,
               std::string& error)
    {
        RuleId const id = RuleNamed(trigger);
        if (id == RuleId::None)
        {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer),
                          "'%s' is not a trigger MAI has",
                          trigger ? trigger : "");
            error = buffer;
            return false;
        }

        out = Rule();
        out.trigger = id;
        return Fill(*SpecOf(id), params, out.operands, out.given, nullptr,
                    error);
    }

    bool Parse(char const* action, char const* params, Step& out,
               RuleSet& owner, std::string& error)
    {
        ActionId const id = ActionNamed(action);
        if (id == ActionId::None)
        {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer),
                          "'%s' is not an action MAI has", action ? action : "");
            error = buffer;
            return false;
        }

        out = Step();
        out.action = id;
        return Fill(*SpecOf(id), params, out.operands, out.given, &owner,
                    error);
    }

    bool ParseGuards(char const* text, std::vector<Guard>& out,
                     RuleSet* owner, std::string& error)
    {
        char buffer[192];

        char const* at = text ? text : "";
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
            while (*at && *at != '=' && *at != '!' && *at != '<' &&
                   *at != '>' && !IsSpace(*at))
            {
                ++at;
            }
            std::string const held(name, std::size_t(at - name));

            // The six a comparison has. Longest first: `<=` must be tried
            // before `<`, or every `<=` reads as `<` followed by a value
            // beginning with `=`.
            Compare op = CompareEq;
            if (at[0] == '!' && at[1] == '=') { op = CompareNe; at += 2; }
            else if (at[0] == '<' && at[1] == '=') { op = CompareLe; at += 2; }
            else if (at[0] == '>' && at[1] == '=') { op = CompareGe; at += 2; }
            else if (at[0] == '<') { op = CompareLt; at += 1; }
            else if (at[0] == '>') { op = CompareGt; at += 1; }
            else if (at[0] == '=') { op = CompareEq; at += 1; }
            else
            {
                std::snprintf(buffer, sizeof(buffer),
                              "guard '%s' has no comparison; the form is "
                              "name=value, or != < <= > >=", held.c_str());
                error = buffer;
                return false;
            }

            char const* value = at;
            while (*at && !IsSpace(*at))
            {
                ++at;
            }
            std::string const number(value, std::size_t(at - value));

            Guard guard;

            // A prefix asks something other than the creature's own memory.
            // The colon is what tells them apart, and a state may not be
            // called `instance`, `aura` or `target_aura` for the same reason a
            // column may not be called *.
            static struct { char const* prefix; GuardOf of; } const kPrefixes[] =
            {
                { "instance:",    GuardInstance },
                { "aura:",        GuardAura },
                { "target_aura:", GuardTargetAura },
            };

            bool prefixed = false;
            for (auto const& known : kPrefixes)
            {
                std::size_t const length = std::strlen(known.prefix);
                if (held.size() <= length ||
                    held.compare(0, length, known.prefix) != 0)
                {
                    continue;
                }

                std::string const field = held.substr(length);
                char* stop = nullptr;
                guard.of = known.of;
                guard.subject = uint32(std::strtoul(field.c_str(), &stop, 10));

                if (field.empty() || (stop && *stop))
                {
                    std::snprintf(buffer, sizeof(buffer),
                                  "guard '%s' does not name a number after "
                                  "'%s'", held.c_str(), known.prefix);
                    error = buffer;
                    return false;
                }

                prefixed = true;
                break;
            }

            // A reserved word: it looks like a bare state name and is not one.
            // `phase` is what `set_phase` writes, and interned as a state it
            // was a guard on a slot nothing ever wrote -- always zero, for
            // ever, in the one example the schema and the manual both used.
            //
            // Answerable with no owner, unlike a state, because it is the
            // creature's own and a sequence the world started still has none.
            if (!prefixed && RuleSet::Reserved(held))
            {
                guard.of = GuardPhase;
                guard.subject = 0;
                prefixed = true;
            }

            if (!prefixed)
            {
                // A bare name is one of the creature's own remembered numbers,
                // and a sequence the world starts has no creature to ask. The
                // same refusal `set_state` gives for the same reason, in the
                // same words, so a guard and a step disagreeing about whose
                // memory they mean is one message and not two.
                if (!owner)
                {
                    std::snprintf(buffer, sizeof(buffer),
                                  "guard names '%s', which is a remembered "
                                  "value, and nothing said whose",
                                  held.c_str());
                    error = buffer;
                    return false;
                }

                std::size_t const slot = owner->Intern(held);
                if (slot >= MaxStates)
                {
                    std::snprintf(buffer, sizeof(buffer),
                                  "guard names '%s' and this creature already "
                                  "remembers %u things",
                                  held.c_str(), uint32(MaxStates));
                    error = buffer;
                    return false;
                }

                guard.of = GuardState;
                guard.subject = uint32(slot);
            }

            char* end = nullptr;
            guard.op = op;
            guard.value = uint32(std::strtoul(number.c_str(), &end, 10));

            if (number.empty() || (end && *end))
            {
                std::snprintf(buffer, sizeof(buffer),
                              "guard on '%s' compares against '%s', which is "
                              "not a number", held.c_str(), number.c_str());
                error = buffer;
                return false;
            }

            out.push_back(guard);
        }

        return true;
    }

    bool ParseGuards(char const* text, Rule& out, RuleSet& owner,
                     std::string& error)
    {
        return ParseGuards(text, out.guards, &owner, error);
    }
}
