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

#include "ReactionQueue.h"

#include <algorithm>
#include <utility>

namespace Combat
{
    bool ReactionQueue::Push(Reaction reaction)
    {
        if (reaction.depth > MAX_DEPTH)
        {
            ++m_refused;
            return false;
        }

        // The budget counts what a drain runs, so a sink that pushes while
        // the budget is already spent is refused here rather than growing a
        // queue nobody will empty.
        if (m_draining && m_ranThisDrain >= MAX_PER_DRAIN)
        {
            ++m_refused;
            return false;
        }

        m_pending.push_back(std::move(reaction));
        return true;
    }

    void ReactionQueue::Drain(ReactionSink& sink)
    {
        // A sink that drains is a sink that recurses. One drain owns the
        // queue until it is empty.
        if (m_draining)
        {
            return;
        }

        m_draining     = true;
        m_ranThisDrain = 0;

        while (!m_pending.empty() && m_ranThisDrain < MAX_PER_DRAIN)
        {
            // Taken by value and popped before running: the sink may push,
            // and a deque that reallocates under a held reference is the kind
            // of bug this whole queue exists to make unwritable.
            const Reaction reaction = m_pending.front();
            m_pending.pop_front();

            ++m_ranThisDrain;

            // Remembered for the whole of Run, so that anything pushed from
            // code the sink calls -- a proc handler, several frames down --
            // inherits the depth it was raised at instead of starting from
            // zero. Without it, a reaction pushed from inside another one
            // looks like a fresh root and the depth limit never fires.
            m_runningDepth = reaction.depth;
            sink.Run(reaction, *this);
            m_runningDepth = 0;
        }

        if (!m_pending.empty())
        {
            m_refused += static_cast<std::uint32_t>(m_pending.size());
            m_pending.clear();
        }

        m_draining = false;
    }

    void ReactionQueue::DropInvolving(ObjectGuid guid)
    {
        m_pending.erase(
            std::remove_if(m_pending.begin(), m_pending.end(),
                [guid](Reaction const& reaction)
                {
                    return reaction.source == guid || reaction.target == guid;
                }),
            m_pending.end());
    }

    void ReactionQueue::Clear()
    {
        m_pending.clear();
        m_ranThisDrain = 0;
        m_refused      = 0;
    }
}
