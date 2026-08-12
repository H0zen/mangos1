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
 * @file SpellCatalog.h
 * @brief Immutable, boot-time catalog of derived per-spell facts.
 *
 * Spell.dbc is a raw client table: it says what a spell *is*, never what the
 * server needs to *know* about it. Everything in the second category -- is this
 * effect positive, what exclusion group does it belong to, which proc flags
 * finally apply once spell_proc_event has had its say -- used to be recomputed
 * from the DBC row on every single query, from more than a hundred call sites.
 *
 * SpellCatalog computes each of those once, after the DBC stores and the SQL
 * override tables are loaded, and then never changes. Build() is the only
 * mutating call; after it returns the catalog is read-only and therefore safe
 * to share across map threads with no synchronisation at all.
 *
 * This header and SpellCatalog.cpp depend on nothing but the DBC row layout and
 * the enum headers -- no Unit, no Player, no ObjectMgr, no database. That is a
 * deliberate constraint, not an accident: it is what lets src/tests compile the
 * derivation directly (game.lib would drag in the whole server), and it is why
 * the SQL-sourced inputs arrive through SpellCatalogSources instead of being
 * read here.
 *
 * @see SpellMgr for the loaders that feed Build()
 */

#ifndef MANGOS_H_SPELLCATALOG
#define MANGOS_H_SPELLCATALOG

#include "Platform/Define.h"
#include "SharedDefines.h"
#include "SpellAuraDefines.h"
#include "DBCStructure.h"

#include <vector>

/// Number of 64-bit words needed for one bit per AuraType.
#define SPELL_AURA_TYPE_WORDS ((TOTAL_AURAS + 63) / 64)

/**
 * Spell clasification (Taken from comments)
 * \todo Properly document this
 */
enum SpellSpecific
{
    SPELL_NORMAL            = 0,
    SPELL_SEAL              = 1,
    SPELL_BLESSING          = 2,
    SPELL_AURA              = 3,
    SPELL_STING             = 4,
    SPELL_CURSE             = 5,
    SPELL_ASPECT            = 6,
    SPELL_TRACKER           = 7,
    SPELL_WARLOCK_ARMOR     = 8,
    SPELL_MAGE_ARMOR        = 9,
    SPELL_ELEMENTAL_SHIELD  = 10,
    SPELL_MAGE_POLYMORPH    = 11,
    SPELL_POSITIVE_SHOUT    = 12,
    SPELL_JUDGEMENT         = 13,
    SPELL_BATTLE_ELIXIR     = 14,
    SPELL_GUARDIAN_ELIXIR   = 15,
    SPELL_FLASK_ELIXIR      = 16,
    // SPELL_PRESENCE          = 17,                        // used in 3.x
    // SPELL_HAND              = 18,                        // used in 3.x
    SPELL_WELL_FED          = 19,
    SPELL_FOOD              = 20,
    SPELL_DRINK             = 21,
    SPELL_FOOD_AND_DRINK    = 22,
};

#define ELIXIR_BATTLE_MASK    0x01
#define ELIXIR_GUARDIAN_MASK  0x02
#define ELIXIR_FLASK_MASK     (ELIXIR_BATTLE_MASK|ELIXIR_GUARDIAN_MASK)
#define ELIXIR_UNSTABLE_MASK  0x04
#define ELIXIR_SHATTRATH_MASK 0x08
#define ELIXIR_WELL_FED       0x10                          // Some foods have SPELLFAMILY_POTION

/*
 * Pure predicates over a DBC row, moved here from SpellMgr.h so that the
 * derivation in SpellCatalog.cpp can use them without pulling the manager in.
 * SpellMgr.h includes this header, so every previous caller is unaffected.
 */

inline bool IsSpellHaveAura(SpellEntry const* spellInfo, AuraType aura, uint32 effectMask = (1 << EFFECT_INDEX_0) | (1 << EFFECT_INDEX_1) | (1 << EFFECT_INDEX_2))
{
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (effectMask & (1 << i))
            if (AuraType(spellInfo->EffectAura[i]) == aura)
            {
                return true;
            }
    }
    return false;
}

inline bool IsSealSpell(SpellEntry const* spellInfo)
{
    // Collection of all the seal family flags. No other paladin spell has any of those.
    return spellInfo->IsFitToFamily(SPELLFAMILY_PALADIN, UI64LIT(0x000004000A000200)) &&
           // avoid counting target triggered effect as seal for avoid remove it or seal by it.
           spellInfo->ImplicitTargetA[0] == TARGET_SELF;
}

inline bool IsElementalShield(SpellEntry const* spellInfo)
{
    // family flags 10 (Lightning), 42 (Earth), 37 (Water), proc shield from T2 8 pieces bonus
    return (spellInfo->SpellClassMask & UI64LIT(0x42000000400)) || spellInfo->ID == 23552;
}

inline bool IsAreaAuraEffect(uint32 effect)
{
    if (effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY    ||
            effect == SPELL_EFFECT_APPLY_AREA_AURA_FRIEND   ||
            effect == SPELL_EFFECT_APPLY_AREA_AURA_ENEMY    ||
            effect == SPELL_EFFECT_APPLY_AREA_AURA_PET      ||
            effect == SPELL_EFFECT_APPLY_AREA_AURA_OWNER)
            {
                return true;
            }
    return false;
}

/**
 * @brief One row of spell_proc_event: the SQL override for a spell's proc rules.
 *
 * Lives here rather than in SpellMgr.h because SpellInfo caches a pointer to it
 * and the catalog tests must be able to build one without linking the server.
 */
struct SpellProcEventEntry
{
    /// if nonzero - bit mask for matching proc condition based on spell
    /// candidate's school: Fire=2, Mask=1<<(2-1)=2
    uint32      schoolMask;
    /// if nonzero - for matching proc condition based on candidate spell's
    /// SpellFamilyNamer value
    uint32      spellFamilyName;
    /// if nonzero - for matching proc condition based on candidate spell's
    /// SpellFamilyFlags (like auras 107 and 108 do)
    ClassFamilyMask spellFamilyMask[MAX_EFFECT_INDEX];
    /// bitmask for matching proc event
    uint32      procFlags;
    /// proc Extend info (see ProcFlagsEx)
    uint32      procEx;
    /// for melee (ranged?) damage spells - proc rate per minute. if zero,
    /// falls back to flat chance from Spell.dbc
    float       ppmRate;
    /// Owerride chance (in most cases for debug only)
    float       customChance;
    /// hidden cooldown used for some spell proc events, applied to
    /// _triggered_spell_
    uint32      cooldown;
};

/**
 * @brief One row of spell_bonus_data: SQL-supplied spell power coefficients.
 */
struct SpellBonusEntry
{
    float  direct_damage;                                   ///< Direct Damage Spell Bonus Coeff
    float  dot_damage;                                      ///< Dot Damage Spell Bonus Coeff
    float  ap_bonus;                                        ///< ??
    float  ap_dot_bonus;
};

/**
 * @brief Everything the server derives about one spell, computed once at boot.
 *
 * A SpellInfo never owns its DBC row; it points at the store's copy and adds the
 * facts that used to be recomputed per query. Fields are grouped by where they
 * come from, because that is what decides when they can be filled: the DBC-only
 * group needs nothing but Spell.dbc, the recursive group needs every other row
 * to already be registered, and the SQL group needs the override tables loaded.
 */
struct SpellInfo
{
    /// The client row this entry describes. Never NULL in a built catalog.
    SpellEntry const* dbc = nullptr;

    uint32 id = 0;

    /// Bit i set when Effect[i] is not SPELL_EFFECT_NONE.
    uint8 effectMask = 0;
    /// Bit i set when effect i applies an aura (plain or area).
    uint8 auraEffectMask = 0;
    /// Bit i set when IsPositiveEffect() holds for effect i.
    uint8 positiveMask = 0;

    /// True when every non-empty effect is positive -- the old IsPositiveSpell.
    bool positive = false;
    /// True when the spell is passive -- the old IsPassiveSpell.
    bool passive = false;

    /// Exclusion class used by the aura stacking rules.
    SpellSpecific specific = SPELL_NORMAL;

    /// One bit per AuraType present on any effect of this spell.
    uint64 auraTypes[SPELL_AURA_TYPE_WORDS] = {};

    /**
     * Proc flags that actually apply, after spell_proc_event has overridden the
     * DBC. This is an override and not a union: IsTriggeredAtSpellProcEvent
     * takes spell_proc_event.procFlags whenever it is nonzero and falls back to
     * ProcTypeMask otherwise, so the merge here reproduces exactly that.
     */
    uint32 procFlags = 0;

    /// spell_proc_event row for this spell, or NULL.
    SpellProcEventEntry const* procEvent = nullptr;
    /// spell_bonus_data row for this spell, or NULL.
    SpellBonusEntry const* bonus = nullptr;

    /// @brief Tests whether any effect of this spell applies the given aura type.
    bool HasAuraType(AuraType type) const
    {
        const uint32 t = uint32(type);
        if (t >= TOTAL_AURAS)
        {
            return false;
        }
        return (auraTypes[t >> 6] & (uint64(1) << (t & 63))) != 0;
    }

    /// @brief Tests whether the given effect index is positive.
    bool IsEffectPositive(SpellEffectIndex effIndex) const
    {
        return (positiveMask & (1 << effIndex)) != 0;
    }
};

/**
 * @brief SQL-sourced inputs the catalog cannot read for itself.
 *
 * Plain function pointers plus an opaque context, so that the catalog stays
 * free of any dependency on SpellMgr's containers and a test can supply fakes
 * with capture-less lambdas.
 */
struct SpellCatalogSources
{
    /// spell_elixir mask for a spell id; 0 when the spell is not an elixir.
    uint32 (*GetElixirMask)(void const* ctx, uint32 spellId) = nullptr;
    /// spell_proc_event row for a spell id, or NULL.
    SpellProcEventEntry const* (*GetProcEvent)(void const* ctx, uint32 spellId) = nullptr;
    /// spell_bonus_data row for a spell id, or NULL.
    SpellBonusEntry const* (*GetBonus)(void const* ctx, uint32 spellId) = nullptr;
    /// Passed back to each callback; the catalog never dereferences it.
    void const* ctx = nullptr;
};

/**
 * @brief Dense, immutable store of SpellInfo, indexed by spell id.
 *
 * Storage is a flat vector sized to the highest spell id plus an id-to-slot
 * table, so Get() is a bounds check and two loads. Empty ids resolve to a
 * shared, zeroed entry rather than NULL, which removes the null test from every
 * call site that used to guard a LookupEntry.
 */
class SpellCatalog
{
    public:
        static SpellCatalog& Instance();

        SpellCatalog() = default;

        SpellCatalog(SpellCatalog const&) = delete;
        SpellCatalog& operator=(SpellCatalog const&) = delete;

        /**
         * @brief Builds the catalog from the DBC rows and the SQL overrides.
         *
         * Must run after the spell DBC store and every SQL table named in
         * @p sources are loaded. Replaces any previously built contents, so a
         * .reload of an override table can rebuild in place.
         *
         * @param entries All spell rows to register; NULL elements are skipped.
         * @param sources Callbacks supplying the SQL-derived inputs.
         */
        void Build(std::vector<SpellEntry const*> const& entries,
                   SpellCatalogSources const& sources);

        /// @brief True once Build() has run.
        bool IsBuilt() const { return m_built; }

        /// @brief Number of registered spells.
        uint32 GetSpellCount() const { return m_count; }

        /**
         * @brief Returns the entry for a spell id.
         *
         * Unknown ids yield a zeroed entry whose dbc member is NULL, so callers
         * that only read derived fields need no null check.
         */
        SpellInfo const& Get(uint32 spellId) const
        {
            if (spellId < m_slotById.size())
            {
                const uint32 slot = m_slotById[spellId];
                if (slot != INVALID_SLOT)
                {
                    return m_entries[slot];
                }
            }
            return m_empty;
        }

        /// @brief Returns the entry for a spell id, or NULL when unknown.
        SpellInfo const* Find(uint32 spellId) const
        {
            SpellInfo const& info = Get(spellId);
            return info.dbc ? &info : nullptr;
        }

    private:
        // constexpr, not const: a static const member is not implicitly inline
        // in C++17, so odr-using it from the inline Get() below needs an
        // out-of-class definition. constexpr members are inline and need none.
        static constexpr uint32 INVALID_SLOT = 0xFFFFFFFF;

        /// Fills the fields that need only this spell's own DBC row.
        void DeriveLocal(SpellInfo& info) const;
        /// Fills the fields whose derivation may consult other spells.
        void DeriveCrossReferenced(SpellInfo& info,
                                   SpellCatalogSources const& sources) const;

        std::vector<SpellInfo> m_entries;
        std::vector<uint32> m_slotById;
        SpellInfo m_empty;
        uint32 m_count = 0;
        bool m_built = false;
};

#define sSpellCatalog SpellCatalog::Instance()

/**
 * @brief Derives whether one effect of a spell is positive.
 *
 * Pure over the DBC rows. @p resolve is used for the one recursive case
 * (SPELL_AURA_PERIODIC_TRIGGER_SPELL, which judges the triggered spell); it may
 * be NULL, in which case that case is skipped exactly as an unknown spell id
 * would have been.
 */
bool DeriveIsPositiveEffect(SpellEntry const* spellproto,
                            SpellEffectIndex effIndex,
                            SpellEntry const* (*resolve)(void const* ctx, uint32 spellId),
                            void const* ctx, uint32 depth = 0);

/**
 * @brief Diagnostics for the one derivation that can be cut short.
 *
 * DeriveIsPositiveEffect follows a chain of triggered spells and stops at
 * MAX_POSITIVE_TRIGGER_DEPTH. That cap exists because only a direct
 * self-trigger is excluded, so a cycle would otherwise recurse without end --
 * but a chain that is merely long, not cyclic, gets truncated too, and there
 * its verdict may differ from the uncapped derivation this replaced.
 *
 * SpellCatalog.Verify cannot see it: the verifier runs the same cap and so
 * truncates identically and agrees. These counters are how the truncation
 * becomes visible at all.
 *
 * Written from Build() only, which is single-threaded at start-up.
 */
struct SpellTriggerTruncation
{
    /// Spell whose trigger chain was cut short.
    uint32 spellId;
    /// Effect index that carried the periodic trigger.
    uint32 effIndex;
    /// Spell the chain was about to step into.
    uint32 triggeredId;
};

/// @brief Number of trigger chains cut short since the last reset.
uint32 GetPositiveTriggerTruncationCount();

/// @brief The recorded truncations, capped at a handful for reporting.
std::vector<SpellTriggerTruncation> const& GetPositiveTriggerTruncations();

/// @brief Clears both of the above. Called at the top of SpellCatalog::Build().
void ResetPositiveTriggerTruncations();

/**
 * @brief Derives the exclusion class for a spell.
 *
 * @param spellInfo  The DBC row.
 * @param elixirMask The spell_elixir mask for this spell, 0 when not an elixir.
 */
SpellSpecific DeriveSpellSpecific(SpellEntry const* spellInfo, uint32 elixirMask);

/**
 * @brief Derives whether a spell is passive. Pure over the DBC row.
 */
bool DeriveIsPassiveSpell(SpellEntry const* spellInfo);

/**
 * @brief Derives whether a target pair reads as positive. Pure over the enums.
 */
bool DeriveIsPositiveTarget(uint32 targetA, uint32 targetB);

#endif
