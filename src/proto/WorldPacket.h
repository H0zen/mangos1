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

#ifndef MANGOSSERVER_WORLDPACKET_H
#define MANGOSSERVER_WORLDPACKET_H

#include "Platform/Define.h"
#include "ByteBuffer.h"
#include "Opcodes.h"

// Note: m_opcode and size stored in platfom dependent format
// ignore endianess until send, and converted at receive
/**
 * @brief
 *
 */
class WorldPacket : public ByteBuffer
{
    public:
        /**
         * @brief just container for later use
         *
         */
        WorldPacket() : ByteBuffer(0), m_opcode(MSG_NULL_ACTION), m_receivedAt(0)
        {
        }
        /**
         * @brief
         *
         * @param opcode
         * @param res
         */
        explicit WorldPacket(uint16 opcode, size_t res = 200)
            : ByteBuffer(res), m_opcode(opcode), m_receivedAt(0) { }
        /**
         * @brief copy constructor
         *
         * @param packet
         */
        WorldPacket(const WorldPacket& packet)
            : ByteBuffer(packet), m_opcode(packet.m_opcode),
              m_receivedAt(packet.m_receivedAt)
        {
        }

        /**
         * @brief
         *
         * @param opcode
         * @param newres
         */
        void Initialize(uint16 opcode, size_t newres = 200)
        {
            clear();
            _storage.reserve(newres);
            m_opcode = opcode;
        }

        /**
         * @brief
         *
         * @return uint16
         */
        uint16 GetOpcode() const { return m_opcode; }

        /**
         * @brief WHEN THIS PACKET ACTUALLY ARRIVED, in server milliseconds.
         *
         * Stamped once, on the network thread, at the moment the packet is handed to a
         * session's mailbox -- NOT when the world thread gets round to it. The two are
         * not the same instant and the difference is not small: for movement it is the
         * whole wait in the queue plus a share of the world tick.
         *
         * That difference used to be invisible and was being paid out of something. A
         * relayed movement stamp is built as `clientTime + offset + playout`, where the
         * offset is calibrated from CMSG_TIME_SYNC_RESP -- which is PROCESS_INPLACE and
         * therefore timed on the network thread. The relay itself happens on the world
         * thread. So the cushion the stamp carries is spent, before anyone sees it, on
         * however long the packet waited; measured over 1,600 relayed packets, a
         * nominal 500 ms of playout arrived as a median of 74.
         *
         * Zero means unstamped: anything the server builds for itself has no arrival.
         */
        uint32 GetReceivedAt() const { return m_receivedAt; }
        void SetReceivedAt(uint32 ms) { m_receivedAt = ms; }
        /**
         * @brief
         *
         * @param opcode
         */
        void SetOpcode(uint16 opcode) { m_opcode = opcode; }
        // Deliberately no GetOpcodeName() here. The opcode-name table belongs to
        // the protocol/game layer, and having this convenience accessor in a
        // shared header made shared depend on game just to format a log line.
        // Callers use LookupOpcodeName(pkt.GetOpcode()) instead.

    protected:
        uint16 m_opcode; /**< TODO */
        uint32 m_receivedAt; ///< server ms at network-thread arrival, 0 if not received
};
#endif
