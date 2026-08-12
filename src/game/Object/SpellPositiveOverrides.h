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
 * @file SpellPositiveOverrides.h
 * @brief The spells whose buff/debuff verdict cannot be read off their effects.
 *
 * Some spells cannot be classified from their DBC row. A DUMMY effect says
 * nothing about intent; a TRANSFORM can be a gift or a curse; a MOD_SCALE can
 * be either. There is no DBC bit to consult -- every AttributesEx bit is a real
 * client flag, so there is no spare one to borrow -- so the verdict has to be
 * recorded somewhere, and until now it lived as thirty spell ids hardcoded into
 * switch statements inside the derivation itself.
 *
 * They are data here instead. That makes the list visible, countable and
 * editable without reading the derivation, and it makes adding one a one-line
 * change rather than finding the right nested case label.
 *
 * CONTEXT IS PART OF THE KEY, not decoration. Each id was previously checked
 * only inside one branch -- 28441 only when the effect is DUMMY, 38449 only for
 * MOD_SCALE and MOD_MELEE_HASTE. Keying on the id alone would apply a verdict to
 * effects it never used to touch, which is why every row names its branch.
 *
 * @see DeriveIsPositiveEffect in SpellCatalog.cpp, the only consumer
 */

#ifndef MANGOS_H_SPELLPOSITIVEOVERRIDES
#define MANGOS_H_SPELLPOSITIVEOVERRIDES

#include "Platform/Define.h"

/// The branch of DeriveIsPositiveEffect a row applies to.
enum PositiveOverrideContext
{
    POC_EFFECT_DUMMY,          ///< SPELL_EFFECT_DUMMY
    POC_EFFECT_SCRIPT,         ///< SPELL_EFFECT_SCRIPT_EFFECT
    POC_AURA_DUMMY,            ///< SPELL_AURA_DUMMY
    POC_AURA_MOD_STUN,         ///< SPELL_AURA_MOD_STUN
    POC_AURA_PACIFY_SILENCE,   ///< SPELL_AURA_MOD_PACIFY_SILENCE
    POC_AURA_TRANSFORM,        ///< SPELL_AURA_TRANSFORM
    POC_AURA_MOD_SCALE,        ///< SPELL_AURA_MOD_SCALE
    POC_AURA_MELEE_HASTE,      ///< SPELL_AURA_MOD_MELEE_HASTE
    POC_AURA_FORCE_REACTION,   ///< SPELL_AURA_FORCE_REACTION
};

/// One recorded verdict.
struct SpellPositiveOverride
{
    uint32 spellId;
    PositiveOverrideContext context;
    bool positive;
    char const* note;
};

/**
 * @brief The recorded verdicts, in the order they appeared in the switches.
 *
 * Not sorted and not required to be: it is read once per spell at boot and has
 * thirty rows. Sorting it would only invite someone to assume a binary search.
 */
static const SpellPositiveOverride kSpellPositiveOverrides[] =
{
    // --- SPELL_EFFECT_DUMMY: a dummy effect carries no intent of its own -----
    { 28441, POC_EFFECT_DUMMY, false, "AB Effect 000" },
    { 10258, POC_EFFECT_DUMMY, true,  "Awaken Vault Warder" },
    { 18153, POC_EFFECT_DUMMY, true,  "Kodo Kombobulator" },
    { 32312, POC_EFFECT_DUMMY, true,  "Move 1" },
    { 37388, POC_EFFECT_DUMMY, true,  "Move 2" },

    // --- SPELL_EFFECT_SCRIPT_EFFECT ----------------------------------------
    { 46650, POC_EFFECT_SCRIPT, true,  "Open Brutallus Back Door" },
    { 5249,  POC_EFFECT_SCRIPT, false, "Ice Block - fixed trap dire maul" },

    // --- SPELL_AURA_DUMMY: positive or negative depending on the caster -----
    { 13139, POC_AURA_DUMMY, false, "net-o-matic special effect" },
    { 18172, POC_AURA_DUMMY, false, "Quest Kodo Roundup player debuff" },
    { 23445, POC_AURA_DUMMY, false, "evil twin" },
    { 35679, POC_AURA_DUMMY, false, "Protectorate Demolitionist" },
    { 37695, POC_AURA_DUMMY, false, "Stanky" },
    { 38637, POC_AURA_DUMMY, false, "Nether Exhaustion (red)" },
    { 38638, POC_AURA_DUMMY, false, "Nether Exhaustion (green)" },
    { 38639, POC_AURA_DUMMY, false, "Nether Exhaustion (blue)" },
    { 44689, POC_AURA_DUMMY, false, "Relay Race Accept Hidden Debuff - DND" },
    // unclear target modes for selection, so just make the effect positive
    { 27184, POC_AURA_DUMMY, true, "unclear target mode" },
    { 27190, POC_AURA_DUMMY, true, "unclear target mode" },
    { 27191, POC_AURA_DUMMY, true, "unclear target mode" },
    { 27201, POC_AURA_DUMMY, true, "unclear target mode" },
    { 27202, POC_AURA_DUMMY, true, "unclear target mode" },
    { 27203, POC_AURA_DUMMY, true, "unclear target mode" },

    // --- SPELL_AURA_MOD_STUN ------------------------------------------------
    { 17624, POC_AURA_MOD_STUN, false, "Petrification" },

    // --- SPELL_AURA_MOD_PACIFY_SILENCE: negative unless listed here ---------
    { 24740, POC_AURA_PACIFY_SILENCE, true, "Wisp Costume" },

    // --- SPELL_AURA_TRANSFORM -----------------------------------------------
    { 36897, POC_AURA_TRANSFORM, false, "Transporter Malfunction (race mutation to horde)" },
    { 36899, POC_AURA_TRANSFORM, false, "Transporter Malfunction (race mutation to alliance)" },

    // --- SPELL_AURA_MOD_SCALE -----------------------------------------------
    { 802,   POC_AURA_MOD_SCALE, true,  "Mutate Bug, wrongly negative by target modes" },
    { 38449, POC_AURA_MOD_SCALE, true,  "Blessing of the Tides" },
    { 36900, POC_AURA_MOD_SCALE, false, "Soul Split: Evil!" },
    { 36901, POC_AURA_MOD_SCALE, false, "Soul Split: Good" },
    { 36893, POC_AURA_MOD_SCALE, false, "Transporter Malfunction (decrease size case)" },
    { 36895, POC_AURA_MOD_SCALE, false, "Transporter Malfunction (increase size case)" },

    // --- SPELL_AURA_MOD_MELEE_HASTE -----------------------------------------
    { 38449, POC_AURA_MELEE_HASTE, true, "Blessing of the Tides" },

    // --- SPELL_AURA_FORCE_REACTION ------------------------------------------
    { 42792, POC_AURA_FORCE_REACTION, false, "Recently Dropped Flag (prevent cancel)" },
};

/// Answer of a lookup that found no row.
enum PositiveOverrideResult
{
    POR_NONE = 0,   ///< nothing recorded; carry on with the ordinary derivation
    POR_POSITIVE,
    POR_NEGATIVE,
};

/**
 * @brief Looks up a recorded verdict for one spell in one branch.
 *
 * @param spellId The spell to look up.
 * @param context The branch asking.
 * @return POR_NONE when nothing is recorded for that pair.
 */
inline PositiveOverrideResult LookupSpellPositiveOverride(uint32 spellId,
                                                          PositiveOverrideContext context)
{
    for (SpellPositiveOverride const& row : kSpellPositiveOverrides)
    {
        if (row.spellId == spellId && row.context == context)
        {
            return row.positive ? POR_POSITIVE : POR_NEGATIVE;
        }
    }
    return POR_NONE;
}

#endif
