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
 * @file AuraTypeIndex.h
 * @brief Per-aura-type lookup with no per-element allocation.
 *
 * Replaces `AuraList m_modAuras[TOTAL_AURAS]` -- 262 std::list objects on every
 * Unit in the world. That array costs three things, and the largest of them is
 * the least obvious:
 *
 *  - std::list allocates a node per element, so every aura joining a modifier
 *    list costs a heap allocation and gives up locality when it is walked;
 *  - 262 empty list heads is 6288 bytes on libstdc++, carried by every creature
 *    whether or not it ever holds an aura;
 *  - and 262 constructors and destructors run on every spawn and despawn.
 *
 * The links live in the elements instead, so joining a list is two stores and
 * costs no allocation at all. Removal is O(1) rather than a search, because the
 * chain is doubly linked. A bitset of occupied types answers "is there one of
 * these" without touching the chains.
 *
 * Templated on the element so the tests can drive it without an Aura, which
 * cannot be built outside a running server. The element must expose the four
 * link accessors named in Traits below; Aura satisfies them with two pointer
 * members.
 */

#ifndef MANGOS_H_AURATYPEINDEX
#define MANGOS_H_AURATYPEINDEX

#include "Platform/Define.h"
#include "SpellAuraDefines.h"

#include <cstddef>

/// Number of 64-bit words needed for one bit per AuraType.
#define AURA_TYPE_INDEX_WORDS ((TOTAL_AURAS + 63) / 64)

/**
 * @brief Forward iterator over one type's chain.
 *
 * Reads like a list iterator so the call sites that walk a modifier list keep
 * their shape. Advancing past the end is the caller's error, exactly as it was
 * with std::list.
 */
template <typename T>
class AuraChainIterator
{
    public:
        AuraChainIterator() : m_current(nullptr) {}
        explicit AuraChainIterator(T* current) : m_current(current) {}

        T* operator*() const { return m_current; }

        AuraChainIterator& operator++()
        {
            m_current = m_current->GetNextOfAuraType();
            return *this;
        }

        AuraChainIterator operator++(int)
        {
            AuraChainIterator copy(*this);
            ++(*this);
            return copy;
        }

        bool operator==(AuraChainIterator const& other) const
        {
            return m_current == other.m_current;
        }

        bool operator!=(AuraChainIterator const& other) const
        {
            return m_current != other.m_current;
        }

    private:
        T* m_current;
};

/**
 * @brief A begin/end pair over one type's chain.
 *
 * Returned by value. The old accessor handed back `AuraList const&`, a
 * reference into the owner; this is two words and copying it is free, but it
 * does mean a call site must bind it by value rather than by const reference.
 */
template <typename T>
class AuraChainRange
{
    public:
        typedef AuraChainIterator<T> const_iterator;
        typedef AuraChainIterator<T> iterator;

        AuraChainRange() : m_head(nullptr) {}
        explicit AuraChainRange(T* head) : m_head(head) {}

        const_iterator begin() const { return const_iterator(m_head); }
        const_iterator end() const { return const_iterator(nullptr); }

        bool empty() const { return m_head == nullptr; }

        T* front() const { return m_head; }

        /// @brief Walks the chain. O(n) -- prefer empty() where that will do.
        size_t size() const
        {
            size_t n = 0;
            for (T* it = m_head; it; it = it->GetNextOfAuraType())
            {
                ++n;
            }
            return n;
        }

    private:
        T* m_head;
};

/**
 * @brief Chains elements by aura type, with the links stored in the elements.
 *
 * An element may be indexed under at most one type at a time, which is what
 * Aura does -- its modifier carries exactly one AuraType for its whole life.
 * Add() on an element that is already indexed, or Remove() on one that is not,
 * is a caller error and is ignored rather than corrupting the chain.
 */
template <typename T>
class AuraTypeIndex
{
    public:
        AuraTypeIndex()
        {
            for (uint32 i = 0; i < TOTAL_AURAS; ++i)
            {
                m_head[i] = nullptr;
            }
            for (uint32 w = 0; w < AURA_TYPE_INDEX_WORDS; ++w)
            {
                m_present[w] = 0;
            }
        }

        AuraTypeIndex(AuraTypeIndex const&) = delete;
        AuraTypeIndex& operator=(AuraTypeIndex const&) = delete;

        /// @brief True when at least one element is indexed under that type.
        bool Has(AuraType type) const
        {
            const uint32 t = uint32(type);
            if (t >= TOTAL_AURAS)
            {
                return false;
            }
            return (m_present[t >> 6] & (uint64(1) << (t & 63))) != 0;
        }

        /// @brief The chain for a type; empty for an unoccupied or invalid type.
        AuraChainRange<T> Get(AuraType type) const
        {
            const uint32 t = uint32(type);
            if (t >= TOTAL_AURAS)
            {
                return AuraChainRange<T>();
            }
            return AuraChainRange<T>(m_head[t]);
        }

        /**
         * @brief Indexes an element under a type, at the front of the chain.
         *
         * Front insertion, where the old code pushed to the back. Order is
         * observable: several call sites take front(), and the accumulators sum
         * in chain order. Owners that care must preserve it -- see AddToBack.
         */
        void Add(T* element, AuraType type)
        {
            const uint32 t = uint32(type);
            if (!element || t >= TOTAL_AURAS || element->GetAuraTypeIndexed())
            {
                return;
            }

            element->SetPrevOfAuraType(nullptr);
            element->SetNextOfAuraType(m_head[t]);
            if (m_head[t])
            {
                m_head[t]->SetPrevOfAuraType(element);
            }
            m_head[t] = element;
            element->SetAuraTypeIndexed(true);

            m_present[t >> 6] |= uint64(1) << (t & 63);
        }

        /**
         * @brief Indexes an element at the end of the chain.
         *
         * This is what replaces the old push_back, and it is the one the owner
         * should use: several handlers depend on the order auras were applied
         * in, and reversing it would change which aura front() names.
         */
        void AddToBack(T* element, AuraType type)
        {
            const uint32 t = uint32(type);
            if (!element || t >= TOTAL_AURAS || element->GetAuraTypeIndexed())
            {
                return;
            }

            element->SetNextOfAuraType(nullptr);

            if (!m_head[t])
            {
                element->SetPrevOfAuraType(nullptr);
                m_head[t] = element;
            }
            else
            {
                T* tail = m_head[t];
                while (tail->GetNextOfAuraType())
                {
                    tail = tail->GetNextOfAuraType();
                }
                tail->SetNextOfAuraType(element);
                element->SetPrevOfAuraType(tail);
            }

            element->SetAuraTypeIndexed(true);
            m_present[t >> 6] |= uint64(1) << (t & 63);
        }

        /// @brief Unlinks an element. O(1); ignores an element that is not indexed.
        void Remove(T* element, AuraType type)
        {
            const uint32 t = uint32(type);
            if (!element || t >= TOTAL_AURAS || !element->GetAuraTypeIndexed())
            {
                return;
            }

            T* prev = element->GetPrevOfAuraType();
            T* next = element->GetNextOfAuraType();

            if (prev)
            {
                prev->SetNextOfAuraType(next);
            }
            else
            {
                m_head[t] = next;
            }

            if (next)
            {
                next->SetPrevOfAuraType(prev);
            }

            element->SetPrevOfAuraType(nullptr);
            element->SetNextOfAuraType(nullptr);
            element->SetAuraTypeIndexed(false);

            if (!m_head[t])
            {
                m_present[t >> 6] &= ~(uint64(1) << (t & 63));
            }
        }

        /// @brief Empties every chain, clearing the links it owns as it goes.
        void Clear()
        {
            for (uint32 t = 0; t < TOTAL_AURAS; ++t)
            {
                T* it = m_head[t];
                while (it)
                {
                    T* next = it->GetNextOfAuraType();
                    it->SetPrevOfAuraType(nullptr);
                    it->SetNextOfAuraType(nullptr);
                    it->SetAuraTypeIndexed(false);
                    it = next;
                }
                m_head[t] = nullptr;
            }

            for (uint32 w = 0; w < AURA_TYPE_INDEX_WORDS; ++w)
            {
                m_present[w] = 0;
            }
        }

    private:
        T* m_head[TOTAL_AURAS];
        uint64 m_present[AURA_TYPE_INDEX_WORDS];
};

#endif
