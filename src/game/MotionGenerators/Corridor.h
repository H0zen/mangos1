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

#ifndef MANGOS_CORRIDOR_H
#define MANGOS_CORRIDOR_H

#include "DetourNavMesh.h"
#include "Platform/Define.h"

#include <cstring>

namespace Path
{
    /**
     * @brief The run of polygons one mover is following, and what happens to it between
     *        legs.
     *
     * This is not new behaviour. The router already kept the previous leg's polygons and
     * already tried to reuse them -- find where the mover has got to, cut off what it has
     * walked past, keep the front and re-search only the tail. What it did not have was a
     * name: the logic sat as forty lines of index arithmetic in the middle of the routing
     * function, sharing two loop variables and a raw array with everything around it.
     *
     * It is worth naming NOW in particular. While a PathFinder was built on the stack for
     * one call, none of that reuse could ever fire -- there was never a previous leg to
     * reuse. MotionDriver keeps a router alive across legs, so the machinery finally runs,
     * and code that runs deserves a boundary and a test.
     *
     * Index arithmetic and storage only: no navmesh, no query. REPAIRING a corridor needs
     * a search and therefore belongs to the router; deciding which part survives does not.
     */
    class Corridor
    {
        public:
            /// No such index. Returned by the searches below.
            static constexpr uint32 NPOS = 0xFFFFFFFFu;

            // 74 * 4.0f = 296 yards of point path, which is far past any evade range.
            // The bound is the corridor's own, not the world's: it is how many polygons one
            // route may DESCRIBE, and Detour reports DT_BUFFER_TOO_SMALL on reaching it.
            static constexpr uint32 CAPACITY = 74;

            Corridor() : m_length(0) {}

            uint32 Length() const { return m_length; }
            bool Empty() const { return m_length == 0; }
            static uint32 Capacity() { return CAPACITY; }

            dtPolyRef const* Polys() const { return m_polys; }

            /// Writable storage for Detour to fill. Pair every write with SetLength.
            dtPolyRef* Buffer() { return m_polys; }

            dtPolyRef At(uint32 i) const { return m_polys[i]; }

            /// The polygon the corridor ends on. Only call it when it is not empty.
            dtPolyRef Last() const { return m_polys[m_length - 1]; }

            void Clear() { m_length = 0; }

            /// Declare how much of the buffer Detour filled, clamped to the capacity.
            void SetLength(uint32 length)
            {
                m_length = (length > CAPACITY) ? CAPACITY : length;
            }

            void Assign(dtPolyRef const* polys, uint32 count)
            {
                SetLength(count);
                if (m_length)
                {
                    std::memcpy(m_polys, polys, size_t(m_length) * sizeof(dtPolyRef));
                }
            }

            /// The FIRST index at or after @p from holding @p poly, or NPOS.
            uint32 Find(dtPolyRef poly, uint32 from = 0) const
            {
                for (uint32 i = from; i < m_length; ++i)
                {
                    if (m_polys[i] == poly)
                    {
                        return i;
                    }
                }
                return NPOS;
            }

            /**
             * @brief The LAST index strictly after @p after holding @p poly, or NPOS.
             *
             * Last and not first, and that is load-bearing. A corridor may re-enter the same
             * polygon -- a path that doubles back round an obstacle does it routinely -- and
             * taking the first occurrence would cut the route short at the point where it
             * merely passed through, discarding the part that actually goes somewhere.
             */
            uint32 FindLastAfter(dtPolyRef poly, uint32 after) const
            {
                // Guarded before the arithmetic: `after` is unsigned, so NPOS + 1 wraps to
                // zero and the loop below would sweep the whole corridor for a caller that
                // meant "I found nothing to search after".
                if (after >= m_length)
                {
                    return NPOS;
                }

                for (uint32 i = m_length; i > after + 1; --i)
                {
                    if (m_polys[i - 1] == poly)
                    {
                        return i - 1;
                    }
                }
                return NPOS;
            }

            /**
             * @brief The mover has walked as far as @p index: drop everything before it.
             *
             * @param index Index that becomes the new front. Out of range empties it.
             */
            void Advance(uint32 index)
            {
                if (index == 0)
                {
                    return;
                }
                if (index >= m_length)
                {
                    m_length = 0;
                    return;
                }

                m_length -= index;
                std::memmove(m_polys, m_polys + index, size_t(m_length) * sizeof(dtPolyRef));
            }

            /// Keep only the first @p count polygons. Growing is not truncating: a count
            /// past the end leaves the corridor as it is rather than inventing polygons.
            void Truncate(uint32 count)
            {
                if (count < m_length)
                {
                    m_length = count;
                }
            }

            /// Whether any slot holds the null reference, which nothing downstream tolerates.
            bool HasInvalid() const
            {
                for (uint32 i = 0; i < m_length; ++i)
                {
                    if (m_polys[i] == 0)
                    {
                        return true;
                    }
                }
                return false;
            }

        private:
            dtPolyRef m_polys[CAPACITY];
            uint32    m_length;
    };
}

#endif // MANGOS_CORRIDOR_H
