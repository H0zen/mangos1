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

/**
 * @file SpellStackOverrides.h
 * @brief The spell pairs whose stacking verdict cannot be derived.
 *
 * IsNoStackSpellDueToSpell ends in a generic rule -- two spells with the same
 * icon, or two ranks of one spell, or two spells with identical effects, do not
 * stack. Ahead of that sits a switch on the two spell families holding roughly
 * seventy hand-written exceptions: Thunderfury against its own proc, a visual
 * against the spell it belongs to, one trinket against another. None of them is
 * derivable from the DBC row, because what they encode is design intent rather
 * than data.
 *
 * They are data here instead, for the same reasons the positive overrides are
 * (@see SpellPositiveOverrides.h): the list becomes visible, countable and
 * editable without reading the derivation, and adding one stops meaning finding
 * the right nested case label in a six-hundred-line switch.
 *
 * CONTEXT IS PART OF THE KEY. Every rule lived inside one branch of the family
 * switch and was only ever reached for that combination of families. A row
 * therefore names its branch, and the derivation only consults the rows whose
 * branch it is actually in -- keying on the ids alone would let a rule fire for
 * family pairings it never used to touch.
 *
 * Rows are matched against plain facts rather than a SpellEntry, so the matcher
 * can be driven from a test without a DBC store or a running server.
 *
 * @see SpellMgr::IsNoStackSpellDueToSpell, the only consumer
 */

#ifndef MANGOS_H_SPELLSTACKOVERRIDES
#define MANGOS_H_SPELLSTACKOVERRIDES

#include "Platform/Define.h"

#include <cstddef>

/**
 * @brief Which branch of the family switch a row belongs to.
 *
 * Named as (family of spell 1)_(family of spell 2). ANY means the rule sat
 * after the inner switch, where the second family had already stopped being
 * tested.
 */
enum StackOverrideContext
{
    SOC_GENERIC_GENERIC,      ///< both SPELLFAMILY_GENERIC
    SOC_GENERIC_MAGE,         ///< spell 1 GENERIC, spell 2 MAGE
    SOC_GENERIC_WARRIOR,      ///< spell 1 GENERIC, spell 2 WARRIOR
    SOC_GENERIC_DRUID,        ///< spell 1 GENERIC, spell 2 DRUID
    SOC_GENERIC_ROGUE,        ///< spell 1 GENERIC, spell 2 ROGUE
    SOC_GENERIC_HUNTER,       ///< spell 1 GENERIC, spell 2 HUNTER
    SOC_GENERIC_PALADIN,      ///< spell 1 GENERIC, spell 2 PALADIN
    SOC_GENERIC_ANY,          ///< spell 1 GENERIC, after the inner switch
};

/**
 * @brief How a row decides whether it applies to a pair of spells.
 *
 * SOME OF THESE ARE DIRECTIONAL AND SOME ARE NOT, and confusing the two is the
 * mistake this enum is shaped to prevent. The original code was a switch on
 * spell 1's family, so a rule about two families was written twice -- once
 * under each family, with the operands swapped. The kinds whose names mention a
 * side (ID1, ICON2, ...) reproduce one of those halves and must NOT fire for
 * the reversed pair; the ones named PAIR or BOTH are genuinely symmetric.
 */
enum StackOverrideMatch
{
    /// Symmetric on ids: {a, b} == {spell 1 id, spell 2 id}, either way round.
    SOM_ID_PAIR,
    /// Directional: spell 1 id == a and spell 2 id == b. Never the reverse.
    SOM_ID1_ID2,
    /// Both spells carry SpellIconID == a.
    SOM_ICON_BOTH,
    /**
     * Both spells carry SpellIconID == a, and their SpellVisualIDs are b and c
     * in either order. Used where an effect's visual has to be told apart from
     * the effect itself, and only the visual id separates them.
     */
    SOM_ICON_BOTH_VISUAL_PAIR,
    /// Both icons == a, and spell 1's visual == b.
    SOM_ICON_BOTH_VISUAL1,
    /// Both icons == a, and spell 2's visual == b.
    SOM_ICON_BOTH_VISUAL2,
    /// Directional: spell 1 id == a, spell 2 icon == b.
    SOM_ID1_ICON2,
    /// Directional: spell 1 icon == a and visual == b, spell 2 id == c.
    SOM_ICON1_VISUAL1_ID2,
    /// Directional: spell 1 id == a, spell 2 icon == b and visual == c.
    SOM_ID1_ICON2_VISUAL2,
    /// Directional: spell 1 id == a, spell 2's family mask intersects `mask`.
    SOM_ID1_MASK2,
};

/**
 * @brief The spell facts a stacking rule is allowed to test.
 *
 * A deliberately small window onto SpellEntry. Nothing here needs the DBC, so a
 * test can build a pair by hand.
 */
struct StackSpellFacts
{
    uint32 id = 0;
    uint32 iconId = 0;
    uint32 visualId = 0;
    uint64 familyMask = 0;
};

/// One recorded stacking verdict.
struct SpellStackOverride
{
    StackOverrideContext context;
    StackOverrideMatch match;
    uint32 a;
    uint32 b;                 ///< unused by SOM_ICON_BOTH
    uint32 c;                 ///< used by the three-operand kinds only
    uint64 mask;              ///< used by the mask kinds only
    /// What IsNoStackSpellDueToSpell returns when this row matches.
    bool noStack;
    char const* note;
};

/**
 * @brief The recorded verdicts, grouped by the branch they came from.
 *
 * Order within a branch does not matter wherever every row of that branch
 * carries the same verdict, which is so for every branch migrated so far: all
 * of them answer "these may stack". That is what made the move safe without
 * proving, rule by rule, that no earlier rule could have preempted a later one.
 *
 * The branches still living in IsNoStackSpellDueToSpell are not uniform --
 * warrior stances, Seed of Corruption and the paladin seals all answer "no
 * stack" from among rules that answer the opposite. When those arrive their
 * relative order becomes load-bearing, because the derivation returns on the
 * first match exactly as the switch did.
 */
static const SpellStackOverride kSpellStackOverrides[] =
{
    // --- both SPELLFAMILY_GENERIC: every rule here says "may stack" ---------
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 21992, 27648, 0, 0, false, "Thunderfury" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 28093, 42084, 0, 0, false, "Lightning Speed (Mongoose) and Fury of the Crashing Waves (Tsunami Talisman)" },
    { SOC_GENERIC_GENERIC, SOM_ICON_BOTH_VISUAL_PAIR, 92, 99, 0, 0, false, "Soulstone Resurrection and Twisting Nether (resurrector)" },
    { SOC_GENERIC_GENERIC, SOM_ICON_BOTH, 240, 0, 0, 0, false, "Heart of the Wild, Agility and various Idol Triggers" },
    { SOC_GENERIC_GENERIC, SOM_ICON_BOTH, 2606, 0, 0, 0, false, "Personalized Weather (thunder effect should overwrite rainy aura)" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 23170, 23171, 0, 0, false, "Brood Affliction: Bronze" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 32756, 38080, 0, 0, false, "Male Shadowy Disguise" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 32756, 38081, 0, 0, false, "Female Shadowy Disguise" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 8326, 20584, 0, 0, false, "Regular and Night Elf Ghost" },
    { SOC_GENERIC_GENERIC, SOM_ICON_BOTH, 1662, 0, 0, 0, false, "Blood Fury and Rage of the Unraveller" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 23014, 19832, 0, 0, false, "Possess visual and Possess" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 40401, 40447, 0, 0, false, "Shade Soul Channel and Akama Soul Channel" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 39908, 40017, 0, 0, false, "Eye Blast visual and Eye Blast" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 45665, 45661, 0, 0, false, "Encapsulate and Encapsulate (channeled)" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 45068, 45582, 0, 0, false, "Felblaze Visual and Fog of Corruption" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 39993, 40041, 0, 0, false, "Simon Game START timer, (DND) and Simon Game Pre-game timer" },
    { SOC_GENERIC_GENERIC, SOM_ID_PAIR, 39400, 32261, 0, 0, false, "Karazhan - Chess: Is Square OCCUPIED aura and Karazhan - Chess: Create Move Marker" },

    /*
     * The multi-family halves below are DIRECTIONAL. Each has a mirror twin
     * living under the other family's branch, and the pair of them is what made
     * the original symmetric. Reversing one here would make its own branch fire
     * for a pairing the mirror already covers, and in the wrong direction.
     */

    // --- spell 1 GENERIC, spell 2 MAGE -------------------------------------
    { SOC_GENERIC_MAGE, SOM_ID1_ICON2, 18820, 125, 0, 0, false, "Arcane Intellect and Insight" },

    // --- spell 1 GENERIC, spell 2 WARRIOR ----------------------------------
    { SOC_GENERIC_WARRIOR, SOM_ICON1_VISUAL1_ID2, 276, 196, 71, 0, false, "Scroll of Protection and Defensive Stance (multi-family check)" },
    { SOC_GENERIC_WARRIOR, SOM_ID1_MASK2, 23694, 0, 0, UI64LIT(0x2), false, "Improved Hamstring -> Hamstring (multi-family check)" },

    // --- spell 1 GENERIC, spell 2 DRUID ------------------------------------
    { SOC_GENERIC_DRUID, SOM_ICON1_VISUAL1_ID2, 312, 216, 24932, 0, false, "Scroll of Stamina and Leader of the Pack (multi-family check)" },
    { SOC_GENERIC_DRUID, SOM_ID1_ID2, 40216, 42016, 0, 0, false, "Dragonmaw Illusion (multi-family check)" },

    // --- spell 1 GENERIC, spell 2 ROGUE ------------------------------------
    { SOC_GENERIC_ROGUE, SOM_ICON_BOTH_VISUAL1, 498, 0, 0, 0, false, "Garrote-Silence -> Garrote (multi-family check)" },

    // --- spell 1 GENERIC, spell 2 HUNTER -----------------------------------
    { SOC_GENERIC_HUNTER, SOM_ID1_ID2, 19410, 5116, 0, 0, false, "Concussive Shot and Imp. Concussive Shot (multi-family check)" },
    { SOC_GENERIC_HUNTER, SOM_ID1_MASK2, 19229, 0, 0, UI64LIT(0x40), false, "Improved Wing Clip -> Wing Clip (multi-family check)" },

    // --- spell 1 GENERIC, spell 2 PALADIN ----------------------------------
    { SOC_GENERIC_PALADIN, SOM_ICON_BOTH_VISUAL1, 502, 969, 0, 0, false, "Unstable Currents and other -> *Sanctity Aura (multi-family check)" },
    { SOC_GENERIC_PALADIN, SOM_ID1_ICON2_VISUAL2, 35081, 561, 7992, 0, false, "*Band of Eternal Champion and Seal of Command (multi-family check)" },
    { SOC_GENERIC_PALADIN, SOM_ICON_BOTH_VISUAL2, 25, 7986, 0, 0, false, "Seal of Righteousness and Head Crack" },

    // --- spell 1 GENERIC, whatever spell 2 is ------------------------------
    // Reached after the inner switch, so it applies to every second family.
    { SOC_GENERIC_ANY, SOM_ICON_BOTH, 1691, 0, 0, 0, false, "Dragonmaw Illusion, Blood Elf Illusion, Human Illusion, Illidari Agent Illusion, Scarlet Crusade Disguise" },
};

/// Number of rows in kSpellStackOverrides.
static const size_t kSpellStackOverrideCount =
    sizeof(kSpellStackOverrides) / sizeof(kSpellStackOverrides[0]);

/**
 * @brief Tests one row against a pair of spells.
 *
 * The pair is unordered for every match kind defined here, so a caller need not
 * try it both ways round.
 */
inline bool MatchesStackOverride(SpellStackOverride const& row,
                                 StackSpellFacts const& s1,
                                 StackSpellFacts const& s2)
{
    switch (row.match)
    {
        case SOM_ID_PAIR:
            return (s1.id == row.a && s2.id == row.b) ||
                   (s2.id == row.a && s1.id == row.b);

        case SOM_ICON_BOTH:
            return s1.iconId == row.a && s2.iconId == row.a;

        case SOM_ICON_BOTH_VISUAL_PAIR:
            return s1.iconId == row.a && s2.iconId == row.a &&
                   ((s1.visualId == row.b && s2.visualId == row.c) ||
                    (s2.visualId == row.b && s1.visualId == row.c));

        case SOM_ID1_ID2:
            return s1.id == row.a && s2.id == row.b;

        case SOM_ICON_BOTH_VISUAL1:
            return s1.iconId == row.a && s2.iconId == row.a &&
                   s1.visualId == row.b;

        case SOM_ICON_BOTH_VISUAL2:
            return s1.iconId == row.a && s2.iconId == row.a &&
                   s2.visualId == row.b;

        case SOM_ID1_ICON2:
            return s1.id == row.a && s2.iconId == row.b;

        case SOM_ICON1_VISUAL1_ID2:
            return s1.iconId == row.a && s1.visualId == row.b && s2.id == row.c;

        case SOM_ID1_ICON2_VISUAL2:
            return s1.id == row.a && s2.iconId == row.b && s2.visualId == row.c;

        case SOM_ID1_MASK2:
            return s1.id == row.a && (s2.familyMask & row.mask) != 0;
    }

    return false;
}

/**
 * @brief Looks for a recorded verdict for this pair, within one branch.
 *
 * @param context  The family branch the derivation is currently in.
 * @param s1       Facts for the first spell.
 * @param s2       Facts for the second spell.
 * @param noStack  Set to the row's verdict when one matches; untouched if not.
 * @return true when a row matched, meaning the caller must return @p noStack.
 */
inline bool FindStackOverride(StackOverrideContext context,
                              StackSpellFacts const& s1,
                              StackSpellFacts const& s2,
                              bool& noStack)
{
    for (size_t i = 0; i < kSpellStackOverrideCount; ++i)
    {
        SpellStackOverride const& row = kSpellStackOverrides[i];
        if (row.context != context)
        {
            continue;
        }

        if (MatchesStackOverride(row, s1, s2))
        {
            noStack = row.noStack;
            return true;
        }
    }

    return false;
}

#endif
