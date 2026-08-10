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

// GENERATED FROM events.manifest -- DO NOT EDIT.
// Regenerate with: python src/game/Scripting/tools/gen_events.py

#ifndef MANGOS_SCRIPT_EVENTS_GEN_H
#define MANGOS_SCRIPT_EVENTS_GEN_H

#include "ScriptTypes.h"

namespace scripting
{
    /// EventId = (category << 8) | local id. The local ids are the
    /// engines' own numbers, so a script that registers by number
    /// keeps working. Holes are retired events; never fill one in.
    enum class EventId : uint16
    {
        None = 0,

        // packet
        PacketReceive                                = 0x0105,
        PacketReceiveUnk                             = 0x0106,
        PacketSend                                   = 0x0107,

        // server
        ServerNetworkStart                           = 0x0201,
        ServerNetworkStop                            = 0x0202,
        ServerSocketOpen                             = 0x0203,
        ServerSocketClose                            = 0x0204,
        ServerPacketReceive                          = 0x0205,
        ServerPacketReceiveUnk                       = 0x0206,
        ServerPacketSend                             = 0x0207,
        ServerOpenStateChange                        = 0x0208,
        ServerConfigLoad                             = 0x0209,
        ServerShutdownInit                           = 0x020B,
        ServerShutdownCancel                         = 0x020C,
        ServerWorldUpdate                            = 0x020D,
        ServerWorldStartup                           = 0x020E,
        ServerWorldShutdown                          = 0x020F,
        ServerLuaStateClose                          = 0x0210,
        ServerMapCreate                              = 0x0211,
        ServerMapDestroy                             = 0x0212,
        ServerMapGridLoad                            = 0x0213,
        ServerMapGridUnload                          = 0x0214,
        ServerMapPlayerEnter                         = 0x0215,
        ServerMapPlayerLeave                         = 0x0216,
        ServerMapUpdate                              = 0x0217,
        ServerEventTrigger                           = 0x0218,
        ServerWeatherChange                          = 0x0219,
        ServerAuctionAdd                             = 0x021A,
        ServerAuctionRemove                          = 0x021B,
        ServerAuctionSuccessful                      = 0x021C,
        ServerAuctionExpire                          = 0x021D,
        ServerAddonMessage                           = 0x021E,
        ServerWorldDeleteCreature                    = 0x021F,
        ServerWorldDeleteGameobject                  = 0x0220,
        ServerLuaStateOpen                           = 0x0221,
        ServerGameStart                              = 0x0222,
        ServerGameStop                               = 0x0223,
        ServerEventRaised                            = 0x0224,

        // player
        PlayerCharacterCreate                        = 0x0301,
        PlayerCharacterDelete                        = 0x0302,
        PlayerLogin                                  = 0x0303,
        PlayerLogout                                 = 0x0304,
        PlayerSpellCast                              = 0x0305,
        PlayerKillPlayer                             = 0x0306,
        PlayerKillCreature                           = 0x0307,
        PlayerKilledByCreature                       = 0x0308,
        PlayerDuelRequest                            = 0x0309,
        PlayerDuelStart                              = 0x030A,
        PlayerDuelEnd                                = 0x030B,
        PlayerGiveXp                                 = 0x030C,
        PlayerLevelChange                            = 0x030D,
        PlayerMoneyChange                            = 0x030E,
        PlayerReputationChange                       = 0x030F,
        PlayerTalentsChange                          = 0x0310,
        PlayerTalentsReset                           = 0x0311,
        PlayerChat                                   = 0x0312,
        PlayerWhisper                                = 0x0313,
        PlayerGroupChat                              = 0x0314,
        PlayerGuildChat                              = 0x0315,
        PlayerChannelChat                            = 0x0316,
        PlayerEmote                                  = 0x0317,
        PlayerTextEmote                              = 0x0318,
        PlayerSave                                   = 0x0319,
        PlayerBindToInstance                         = 0x031A,
        PlayerUpdateZone                             = 0x031B,
        PlayerMapChange                              = 0x031C,
        PlayerEquip                                  = 0x031D,
        PlayerFirstLogin                             = 0x031E,
        PlayerCanUseItem                             = 0x031F,
        PlayerLootItem                               = 0x0320,
        PlayerEnterCombat                            = 0x0321,
        PlayerLeaveCombat                            = 0x0322,
        PlayerRepop                                  = 0x0323,
        PlayerResurrect                              = 0x0324,
        PlayerLootMoney                              = 0x0325,
        PlayerQuestAbandon                           = 0x0326,
        PlayerLearnTalents                           = 0x0327,
        PlayerEnvironmentalDeath                     = 0x0328,
        PlayerTradeAccept                            = 0x0329,
        PlayerCommand                                = 0x032A,
        PlayerSkillChange                            = 0x032B,
        PlayerLearnSpell                             = 0x032C,
        PlayerAchievementComplete                    = 0x032D,
        PlayerDiscoverArea                           = 0x032E,
        PlayerUpdateArea                             = 0x032F,
        PlayerTradeInit                              = 0x0330,
        PlayerSendMail                               = 0x0331,
        PlayerQuestStatusChanged                     = 0x0336,
        PlayerQuestStart                             = 0x0337,
        PlayerQuestEnd                               = 0x0338,

        // guild
        GuildAddMember                               = 0x0401,
        GuildRemoveMember                            = 0x0402,
        GuildMotdChange                              = 0x0403,
        GuildInfoChange                              = 0x0404,
        GuildCreate                                  = 0x0405,
        GuildDisband                                 = 0x0406,
        GuildMoneyWithdraw                           = 0x0407,
        GuildMoneyDeposit                            = 0x0408,
        GuildItemMove                                = 0x0409,
        GuildEvent                                   = 0x040A,
        GuildBankEvent                               = 0x040B,

        // group
        GroupAddMember                               = 0x0501,
        GroupInviteMember                            = 0x0502,
        GroupRemoveMember                            = 0x0503,
        GroupLeaderChange                            = 0x0504,
        GroupDisband                                 = 0x0505,
        GroupCreate                                  = 0x0506,
        GroupMemberAccept                            = 0x0507,

        // vehicle
        VehicleInstall                               = 0x0601,
        VehicleUninstall                             = 0x0602,
        VehicleInstallAccessory                      = 0x0604,
        VehicleAddPassenger                          = 0x0605,
        VehicleRemovePassenger                       = 0x0606,

        // creature
        CreatureEnterCombat                          = 0x0701,
        CreatureLeaveCombat                          = 0x0702,
        CreatureTargetDied                           = 0x0703,
        CreatureDied                                 = 0x0704,
        CreatureSpawn                                = 0x0705,
        CreatureReachWp                              = 0x0706,
        CreatureAiUpdate                             = 0x0707,
        CreatureReceiveEmote                         = 0x0708,
        CreatureDamageTaken                          = 0x0709,
        CreaturePreCombat                            = 0x070A,
        CreatureOwnerAttacked                        = 0x070C,
        CreatureOwnerAttackedAt                      = 0x070D,
        CreatureHitBySpell                           = 0x070E,
        CreatureSpellHitTarget                       = 0x070F,
        CreatureJustSummonedCreature                 = 0x0713,
        CreatureSummonedCreatureDespawn              = 0x0714,
        CreatureSummonedCreatureDied                 = 0x0715,
        CreatureSummoned                             = 0x0716,
        CreatureReset                                = 0x0717,
        CreatureReachHome                            = 0x0718,
        CreatureCorpseRemoved                        = 0x071A,
        CreatureMoveInLos                            = 0x071B,
        CreatureDummyEffect                          = 0x071E,
        CreatureQuestAccept                          = 0x071F,
        CreatureQuestReward                          = 0x0722,
        CreatureDialogStatus                         = 0x0723,
        CreatureAdd                                  = 0x0724,
        CreatureRemove                               = 0x0725,

        // gameobject
        GameobjectAiUpdate                           = 0x0801,
        GameobjectSpawn                              = 0x0802,
        GameobjectDummyEffect                        = 0x0803,
        GameobjectQuestAccept                        = 0x0804,
        GameobjectQuestReward                        = 0x0805,
        GameobjectDialogStatus                       = 0x0806,
        GameobjectDestroyed                          = 0x0807,
        GameobjectDamaged                            = 0x0808,
        GameobjectLootStateChange                    = 0x0809,
        GameobjectGoStateChanged                     = 0x080A,
        GameobjectAdd                                = 0x080C,
        GameobjectRemove                             = 0x080D,
        GameobjectUse                                = 0x080E,
        GameobjectActivate                           = 0x080F,
        GameobjectTrapSprung                         = 0x0810,

        // spell
        SpellCast                                    = 0x0901,
        SpellAuraApplication                         = 0x0902,
        SpellDispel                                  = 0x0903,
        SpellPeriodicTick                            = 0x0904,
        SpellPeriodicUpdate                          = 0x0905,
        SpellAuraCalcAmount                          = 0x0906,
        SpellCalcPeriodic                            = 0x0907,
        SpellCheckProc                               = 0x0908,
        SpellProc                                    = 0x0909,
        SpellCheckCast                               = 0x090A,
        SpellBeforeCast                              = 0x090B,
        SpellAfterCast                               = 0x090C,
        SpellObjectAreaTarget                        = 0x090D,
        SpellObjectTarget                            = 0x090E,
        SpellDestTarget                              = 0x090F,
        SpellEffectLaunch                            = 0x0910,
        SpellEffectLaunchTarget                      = 0x0911,
        SpellEffectCalcAbsorb                        = 0x0912,
        SpellEffectHit                               = 0x0913,
        SpellBeforeHit                               = 0x0914,
        SpellEffectHitTarget                         = 0x0915,
        SpellHit                                     = 0x0916,
        SpellAfterHit                                = 0x0917,

        // item
        ItemDummyEffect                              = 0x0A01,
        ItemUse                                      = 0x0A02,
        ItemQuestAccept                              = 0x0A03,
        ItemExpire                                   = 0x0A04,
        ItemRemove                                   = 0x0A05,
        ItemAdd                                      = 0x0A06,
        ItemEquip                                    = 0x0A07,
        ItemUnequip                                  = 0x0A08,

        // bg
        BgStart                                      = 0x0C01,
        BgEnd                                        = 0x0C02,
        BgCreate                                     = 0x0C03,
        BgPreDestroy                                 = 0x0C04,

        // instance
        InstanceInitialize                           = 0x0D01,
        InstanceLoad                                 = 0x0D02,
        InstanceUpdate                               = 0x0D03,
        InstancePlayerEnter                          = 0x0D04,
        InstanceCreatureCreate                       = 0x0D05,
        InstanceGameobjectCreate                     = 0x0D06,
        InstanceCheckEncounterInProgress             = 0x0D07,

        // gossip
        GossipCreatureHello                          = 0x0B01,
        GossipCreatureSelect                         = 0x0B02,
        GossipGameobjectHello                        = 0x0B03,
        GossipGameobjectSelect                       = 0x0B04,
        GossipItemHello                              = 0x0B05,
        GossipItemSelect                             = 0x0B06,
        GossipPlayerMenuSelect                       = 0x0B07,
        GossipActionChosen                           = 0x0B08,
        GossipMenuShown                              = 0x0B09,

        // core
        CoreNpcSpellClick                            = 0x0F01,
        CoreEffectScriptEffect                       = 0x0F02,
        CoreAuraDummy                                = 0x0F03,
        CoreEffectDummy                              = 0x0F04,
    };

    /// packet/on_receive: cancel
    struct PacketReceive
    {
        static constexpr EventId Id = EventId::PacketReceive;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        Borrow         packet;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromLent(packet);
        }

        void Unpack(Arg const* args)
        {
            packet = (args[1].AsLent());
        }
    };

    /// packet/on_receive_unk: cancel
    struct PacketReceiveUnk
    {
        static constexpr EventId Id = EventId::PacketReceiveUnk;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        Borrow         packet;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromLent(packet);
        }

        void Unpack(Arg const* args)
        {
            packet = (args[1].AsLent());
        }
    };

    /// packet/on_send: cancel
    struct PacketSend
    {
        static constexpr EventId Id = EventId::PacketSend;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        Borrow         packet;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromLent(packet);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_network_start: broadcast
    struct ServerNetworkStart
    {
        static constexpr EventId Id = EventId::ServerNetworkStart;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_network_stop: broadcast
    struct ServerNetworkStop
    {
        static constexpr EventId Id = EventId::ServerNetworkStop;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_socket_open: broadcast
    struct ServerSocketOpen
    {
        static constexpr EventId Id = EventId::ServerSocketOpen;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_socket_close: broadcast
    struct ServerSocketClose
    {
        static constexpr EventId Id = EventId::ServerSocketClose;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_packet_receive: cancel
    struct ServerPacketReceive
    {
        static constexpr EventId Id = EventId::ServerPacketReceive;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Borrow         session;
        Borrow         packet;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(session);
            args[1] = Arg::FromLent(packet);
        }

        void Unpack(Arg const* args)
        {
            packet = (args[1].AsLent());
        }
    };

    /// server/on_packet_receive_unk: cancel
    struct ServerPacketReceiveUnk
    {
        static constexpr EventId Id = EventId::ServerPacketReceiveUnk;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        Borrow         packet;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromLent(packet);
        }

        void Unpack(Arg const* args)
        {
            packet = (args[1].AsLent());
        }
    };

    /// server/on_packet_send: cancel
    struct ServerPacketSend
    {
        static constexpr EventId Id = EventId::ServerPacketSend;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Borrow         session;
        Borrow         packet;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(session);
            args[1] = Arg::FromLent(packet);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_open_state_change: broadcast
    struct ServerOpenStateChange
    {
        static constexpr EventId Id = EventId::ServerOpenStateChange;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        bool           open;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromFlag(open);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_config_load: broadcast
    struct ServerConfigLoad
    {
        static constexpr EventId Id = EventId::ServerConfigLoad;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        bool           reload;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromFlag(reload);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_shutdown_init: broadcast
    struct ServerShutdownInit
    {
        static constexpr EventId Id = EventId::ServerShutdownInit;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        uint32         code;
        uint32         mask;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNumber(code);
            args[1] = Arg::FromNumber(mask);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_shutdown_cancel: broadcast
    struct ServerShutdownCancel
    {
        static constexpr EventId Id = EventId::ServerShutdownCancel;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_world_update: broadcast
    struct ServerWorldUpdate
    {
        static constexpr EventId Id = EventId::ServerWorldUpdate;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        uint32         diff;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNumber(diff);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_world_startup: broadcast
    struct ServerWorldStartup
    {
        static constexpr EventId Id = EventId::ServerWorldStartup;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_world_shutdown: broadcast
    struct ServerWorldShutdown
    {
        static constexpr EventId Id = EventId::ServerWorldShutdown;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_lua_state_close: broadcast
    struct ServerLuaStateClose
    {
        static constexpr EventId Id = EventId::ServerLuaStateClose;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_map_create: broadcast
    struct ServerMapCreate
    {
        static constexpr EventId Id = EventId::ServerMapCreate;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         map;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(map);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_map_destroy: broadcast
    struct ServerMapDestroy
    {
        static constexpr EventId Id = EventId::ServerMapDestroy;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         map;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(map);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_map_grid_load: broadcast
    struct ServerMapGridLoad
    {
        static constexpr EventId Id = EventId::ServerMapGridLoad;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_map_grid_unload: broadcast
    struct ServerMapGridUnload
    {
        static constexpr EventId Id = EventId::ServerMapGridUnload;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_map_player_enter: broadcast
    struct ServerMapPlayerEnter
    {
        static constexpr EventId Id = EventId::ServerMapPlayerEnter;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         map;
        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(map);
            args[1] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_map_player_leave: broadcast
    struct ServerMapPlayerLeave
    {
        static constexpr EventId Id = EventId::ServerMapPlayerLeave;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         map;
        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(map);
            args[1] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_map_update: broadcast
    struct ServerMapUpdate
    {
        static constexpr EventId Id = EventId::ServerMapUpdate;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         map;
        uint32         diff;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(map);
            args[1] = Arg::FromNumber(diff);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_event_trigger: claim
    struct ServerEventTrigger
    {
        static constexpr EventId Id = EventId::ServerEventTrigger;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Handle         trigger;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNamed(trigger);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_weather_change: broadcast
    struct ServerWeatherChange
    {
        static constexpr EventId Id = EventId::ServerWeatherChange;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         weather;
        uint32         zone;
        uint32         state;
        double         grade;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(weather);
            args[1] = Arg::FromNumber(zone);
            args[2] = Arg::FromNumber(state);
            args[3] = Arg::FromReal(grade);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_auction_add: broadcast
    struct ServerAuctionAdd
    {
        static constexpr EventId Id = EventId::ServerAuctionAdd;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         house;
        Handle         entry;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(house);
            args[1] = Arg::FromNamed(entry);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_auction_remove: broadcast
    struct ServerAuctionRemove
    {
        static constexpr EventId Id = EventId::ServerAuctionRemove;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         house;
        Handle         entry;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(house);
            args[1] = Arg::FromNamed(entry);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_auction_successful: broadcast
    struct ServerAuctionSuccessful
    {
        static constexpr EventId Id = EventId::ServerAuctionSuccessful;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         house;
        Handle         entry;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(house);
            args[1] = Arg::FromNamed(entry);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_auction_expire: broadcast
    struct ServerAuctionExpire
    {
        static constexpr EventId Id = EventId::ServerAuctionExpire;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         house;
        Handle         entry;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(house);
            args[1] = Arg::FromNamed(entry);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_addon_message: cancel
    struct ServerAddonMessage
    {
        static constexpr EventId Id = EventId::ServerAddonMessage;
        static constexpr std::size_t Arity = 7;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            sender;
        uint32         type;
        std::string&   msg;    ///< in/out
        Ref            receiver;
        Handle         guild;
        Handle         group;
        Borrow         channel;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(sender);
            args[1] = Arg::FromNumber(type);
            args[2] = Arg::FromText(msg);
            args[3] = Arg::FromEntity(receiver);
            args[4] = Arg::FromNamed(guild);
            args[5] = Arg::FromNamed(group);
            args[6] = Arg::FromLent(channel);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_world_delete_creature: broadcast
    struct ServerWorldDeleteCreature
    {
        static constexpr EventId Id = EventId::ServerWorldDeleteCreature;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_world_delete_gameobject: broadcast
    struct ServerWorldDeleteGameobject
    {
        static constexpr EventId Id = EventId::ServerWorldDeleteGameobject;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            gameobject;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(gameobject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_lua_state_open: broadcast
    struct ServerLuaStateOpen
    {
        static constexpr EventId Id = EventId::ServerLuaStateOpen;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            subject;    ///< placeholder; this event has no payload

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(subject);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_game_start: broadcast
    struct ServerGameStart
    {
        static constexpr EventId Id = EventId::ServerGameStart;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        uint32         eventId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNumber(eventId);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_game_stop: broadcast
    struct ServerGameStop
    {
        static constexpr EventId Id = EventId::ServerGameStop;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        uint32         eventId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNumber(eventId);
        }

        void Unpack(Arg const*) {}
    };

    /// server/on_event_raised: claim
    struct ServerEventRaised
    {
        static constexpr EventId Id = EventId::ServerEventRaised;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            source;
        Ref            target;
        uint32         eventId;
        bool           isStart;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(source);
            args[1] = Arg::FromEntity(target);
            args[2] = Arg::FromNumber(eventId);
            args[3] = Arg::FromFlag(isStart);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_character_create: broadcast
    struct PlayerCharacterCreate
    {
        static constexpr EventId Id = EventId::PlayerCharacterCreate;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_character_delete: broadcast
    struct PlayerCharacterDelete
    {
        static constexpr EventId Id = EventId::PlayerCharacterDelete;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        uint32         guid;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNumber(guid);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_login: broadcast
    struct PlayerLogin
    {
        static constexpr EventId Id = EventId::PlayerLogin;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_logout: broadcast
    struct PlayerLogout
    {
        static constexpr EventId Id = EventId::PlayerLogout;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_spell_cast: broadcast
    struct PlayerSpellCast
    {
        static constexpr EventId Id = EventId::PlayerSpellCast;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Borrow         spell;
        bool           skipCheck;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromLent(spell);
            args[2] = Arg::FromFlag(skipCheck);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_kill_player: broadcast
    struct PlayerKillPlayer
    {
        static constexpr EventId Id = EventId::PlayerKillPlayer;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            killer;
        Ref            killed;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(killer);
            args[1] = Arg::FromEntity(killed);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_kill_creature: broadcast
    struct PlayerKillCreature
    {
        static constexpr EventId Id = EventId::PlayerKillCreature;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            killer;
        Ref            killed;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(killer);
            args[1] = Arg::FromEntity(killed);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_killed_by_creature: broadcast
    struct PlayerKilledByCreature
    {
        static constexpr EventId Id = EventId::PlayerKilledByCreature;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            killer;
        Ref            killed;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(killer);
            args[1] = Arg::FromEntity(killed);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_duel_request: broadcast
    struct PlayerDuelRequest
    {
        static constexpr EventId Id = EventId::PlayerDuelRequest;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            target;
        Ref            challenger;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(target);
            args[1] = Arg::FromEntity(challenger);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_duel_start: broadcast
    struct PlayerDuelStart
    {
        static constexpr EventId Id = EventId::PlayerDuelStart;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            starter;
        Ref            challenger;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(starter);
            args[1] = Arg::FromEntity(challenger);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_duel_end: broadcast
    struct PlayerDuelEnd
    {
        static constexpr EventId Id = EventId::PlayerDuelEnd;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            winner;
        Ref            loser;
        uint32         type;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(winner);
            args[1] = Arg::FromEntity(loser);
            args[2] = Arg::FromNumber(type);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_give_xp: broadcast
    struct PlayerGiveXp
    {
        static constexpr EventId Id = EventId::PlayerGiveXp;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         amount;    ///< in/out
        Ref            victim;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(amount);
            args[2] = Arg::FromEntity(victim);
        }

        void Unpack(Arg const* args)
        {
            amount = static_cast<uint32>(args[1].AsNumber());
        }
    };

    /// player/on_level_change: broadcast
    struct PlayerLevelChange
    {
        static constexpr EventId Id = EventId::PlayerLevelChange;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         oldLevel;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(oldLevel);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_money_change: broadcast
    struct PlayerMoneyChange
    {
        static constexpr EventId Id = EventId::PlayerMoneyChange;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        int32          amount;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromSigned(amount);
        }

        void Unpack(Arg const* args)
        {
            amount = static_cast<int32>(args[1].AsSigned());
        }
    };

    /// player/on_reputation_change: broadcast
    struct PlayerReputationChange
    {
        static constexpr EventId Id = EventId::PlayerReputationChange;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         factionID;
        int32          standing;    ///< in/out
        bool           incremental;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(factionID);
            args[2] = Arg::FromSigned(standing);
            args[3] = Arg::FromFlag(incremental);
        }

        void Unpack(Arg const* args)
        {
            standing = static_cast<int32>(args[2].AsSigned());
        }
    };

    /// player/on_talents_change: broadcast
    struct PlayerTalentsChange
    {
        static constexpr EventId Id = EventId::PlayerTalentsChange;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         newPoints;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(newPoints);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_talents_reset: broadcast
    struct PlayerTalentsReset
    {
        static constexpr EventId Id = EventId::PlayerTalentsReset;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        bool           noCost;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromFlag(noCost);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_chat: cancel
    struct PlayerChat
    {
        static constexpr EventId Id = EventId::PlayerChat;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         type;
        uint32         lang;
        std::string&   msg;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(type);
            args[2] = Arg::FromNumber(lang);
            args[3] = Arg::FromText(msg);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_whisper: cancel
    struct PlayerWhisper
    {
        static constexpr EventId Id = EventId::PlayerWhisper;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         type;
        uint32         lang;
        std::string&   msg;    ///< in/out
        Ref            receiver;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(type);
            args[2] = Arg::FromNumber(lang);
            args[3] = Arg::FromText(msg);
            args[4] = Arg::FromEntity(receiver);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_group_chat: cancel
    struct PlayerGroupChat
    {
        static constexpr EventId Id = EventId::PlayerGroupChat;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         type;
        uint32         lang;
        std::string&   msg;    ///< in/out
        Handle         group;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(type);
            args[2] = Arg::FromNumber(lang);
            args[3] = Arg::FromText(msg);
            args[4] = Arg::FromNamed(group);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_guild_chat: cancel
    struct PlayerGuildChat
    {
        static constexpr EventId Id = EventId::PlayerGuildChat;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         type;
        uint32         lang;
        std::string&   msg;    ///< in/out
        Handle         guild;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(type);
            args[2] = Arg::FromNumber(lang);
            args[3] = Arg::FromText(msg);
            args[4] = Arg::FromNamed(guild);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_channel_chat: cancel
    struct PlayerChannelChat
    {
        static constexpr EventId Id = EventId::PlayerChannelChat;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         type;
        uint32         lang;
        std::string&   msg;    ///< in/out
        Borrow         channel;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(type);
            args[2] = Arg::FromNumber(lang);
            args[3] = Arg::FromText(msg);
            args[4] = Arg::FromLent(channel);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_emote: broadcast
    struct PlayerEmote
    {
        static constexpr EventId Id = EventId::PlayerEmote;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         emote;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(emote);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_text_emote: broadcast
    struct PlayerTextEmote
    {
        static constexpr EventId Id = EventId::PlayerTextEmote;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         textEmote;
        uint32         emoteNum;
        Ref            guid;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(textEmote);
            args[2] = Arg::FromNumber(emoteNum);
            args[3] = Arg::FromEntity(guid);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_save: broadcast
    struct PlayerSave
    {
        static constexpr EventId Id = EventId::PlayerSave;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_bind_to_instance: broadcast
    struct PlayerBindToInstance
    {
        static constexpr EventId Id = EventId::PlayerBindToInstance;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         difficulty;
        uint32         mapid;
        bool           permanent;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(difficulty);
            args[2] = Arg::FromNumber(mapid);
            args[3] = Arg::FromFlag(permanent);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_update_zone: broadcast
    struct PlayerUpdateZone
    {
        static constexpr EventId Id = EventId::PlayerUpdateZone;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         newZone;
        uint32         newArea;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(newZone);
            args[2] = Arg::FromNumber(newArea);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_map_change: broadcast
    struct PlayerMapChange
    {
        static constexpr EventId Id = EventId::PlayerMapChange;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_equip: broadcast
    struct PlayerEquip
    {
        static constexpr EventId Id = EventId::PlayerEquip;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            item;
        uint32         bag;
        uint32         slot;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(item);
            args[2] = Arg::FromNumber(bag);
            args[3] = Arg::FromNumber(slot);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_first_login: broadcast
    struct PlayerFirstLogin
    {
        static constexpr EventId Id = EventId::PlayerFirstLogin;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_can_use_item: broadcast
    struct PlayerCanUseItem
    {
        static constexpr EventId Id = EventId::PlayerCanUseItem;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         itemEntry;
        uint32         result;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(itemEntry);
            args[2] = Arg::FromNumber(result);
        }

        void Unpack(Arg const* args)
        {
            result = static_cast<uint32>(args[2].AsNumber());
        }
    };

    /// player/on_loot_item: broadcast
    struct PlayerLootItem
    {
        static constexpr EventId Id = EventId::PlayerLootItem;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            item;
        uint32         count;
        Ref            guid;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(item);
            args[2] = Arg::FromNumber(count);
            args[3] = Arg::FromEntity(guid);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_enter_combat: broadcast
    struct PlayerEnterCombat
    {
        static constexpr EventId Id = EventId::PlayerEnterCombat;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            enemy;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(enemy);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_leave_combat: broadcast
    struct PlayerLeaveCombat
    {
        static constexpr EventId Id = EventId::PlayerLeaveCombat;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_repop: broadcast
    struct PlayerRepop
    {
        static constexpr EventId Id = EventId::PlayerRepop;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_resurrect: broadcast
    struct PlayerResurrect
    {
        static constexpr EventId Id = EventId::PlayerResurrect;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_loot_money: broadcast
    struct PlayerLootMoney
    {
        static constexpr EventId Id = EventId::PlayerLootMoney;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         amount;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(amount);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_quest_abandon: broadcast
    struct PlayerQuestAbandon
    {
        static constexpr EventId Id = EventId::PlayerQuestAbandon;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         questId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(questId);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_learn_talents: broadcast
    struct PlayerLearnTalents
    {
        static constexpr EventId Id = EventId::PlayerLearnTalents;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         talentId;
        uint32         talentRank;
        uint32         spellid;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(talentId);
            args[2] = Arg::FromNumber(talentRank);
            args[3] = Arg::FromNumber(spellid);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_environmental_death: broadcast
    struct PlayerEnvironmentalDeath
    {
        static constexpr EventId Id = EventId::PlayerEnvironmentalDeath;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            killed;
        uint32         damageType;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(killed);
            args[1] = Arg::FromNumber(damageType);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_trade_accept: cancel
    struct PlayerTradeAccept
    {
        static constexpr EventId Id = EventId::PlayerTradeAccept;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            trader;
        Ref            tradee;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(trader);
            args[1] = Arg::FromEntity(tradee);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_command: cancel
    struct PlayerCommand
    {
        static constexpr EventId Id = EventId::PlayerCommand;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        std::string&   text;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromText(text);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_skill_change: broadcast
    struct PlayerSkillChange
    {
        static constexpr EventId Id = EventId::PlayerSkillChange;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         skillId;
        uint32         skillValue;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(skillId);
            args[2] = Arg::FromNumber(skillValue);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_learn_spell: broadcast
    struct PlayerLearnSpell
    {
        static constexpr EventId Id = EventId::PlayerLearnSpell;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         spellid;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(spellid);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_achievement_complete: broadcast
    struct PlayerAchievementComplete
    {
        static constexpr EventId Id = EventId::PlayerAchievementComplete;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         achievementId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(achievementId);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_discover_area: broadcast
    struct PlayerDiscoverArea
    {
        static constexpr EventId Id = EventId::PlayerDiscoverArea;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         area;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(area);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_update_area: broadcast
    struct PlayerUpdateArea
    {
        static constexpr EventId Id = EventId::PlayerUpdateArea;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         oldArea;
        uint32         newArea;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(oldArea);
            args[2] = Arg::FromNumber(newArea);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_trade_init: cancel
    struct PlayerTradeInit
    {
        static constexpr EventId Id = EventId::PlayerTradeInit;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            trader;
        Ref            tradee;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(trader);
            args[1] = Arg::FromEntity(tradee);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_send_mail: cancel
    struct PlayerSendMail
    {
        static constexpr EventId Id = EventId::PlayerSendMail;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            sender;
        Ref            recipientGuid;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(sender);
            args[1] = Arg::FromEntity(recipientGuid);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_quest_status_changed: broadcast
    struct PlayerQuestStatusChanged
    {
        static constexpr EventId Id = EventId::PlayerQuestStatusChanged;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        uint32         questId;
        uint32         status;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(questId);
            args[2] = Arg::FromNumber(status);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_quest_start: broadcast
    struct PlayerQuestStart
    {
        static constexpr EventId Id = EventId::PlayerQuestStart;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            questGiver;
        Handle         quest;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(questGiver);
            args[2] = Arg::FromNamed(quest);
        }

        void Unpack(Arg const*) {}
    };

    /// player/on_quest_end: broadcast
    struct PlayerQuestEnd
    {
        static constexpr EventId Id = EventId::PlayerQuestEnd;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            questGiver;
        Handle         quest;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(questGiver);
            args[2] = Arg::FromNamed(quest);
        }

        void Unpack(Arg const*) {}
    };

    /// guild/on_add_member: broadcast
    struct GuildAddMember
    {
        static constexpr EventId Id = EventId::GuildAddMember;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;
        Ref            player;
        uint32         plRank;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
            args[1] = Arg::FromEntity(player);
            args[2] = Arg::FromNumber(plRank);
        }

        void Unpack(Arg const*) {}
    };

    /// guild/on_remove_member: broadcast
    struct GuildRemoveMember
    {
        static constexpr EventId Id = EventId::GuildRemoveMember;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;
        Ref            player;
        bool           isDisbanding;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
            args[1] = Arg::FromEntity(player);
            args[2] = Arg::FromFlag(isDisbanding);
        }

        void Unpack(Arg const*) {}
    };

    /// guild/on_motd_change: broadcast
    struct GuildMotdChange
    {
        static constexpr EventId Id = EventId::GuildMotdChange;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;
        std::string&   motd;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
            args[1] = Arg::FromText(motd);
        }

        void Unpack(Arg const*) {}
    };

    /// guild/on_info_change: broadcast
    struct GuildInfoChange
    {
        static constexpr EventId Id = EventId::GuildInfoChange;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;
        std::string&   info;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
            args[1] = Arg::FromText(info);
        }

        void Unpack(Arg const*) {}
    };

    /// guild/on_create: broadcast
    struct GuildCreate
    {
        static constexpr EventId Id = EventId::GuildCreate;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;
        Ref            leader;
        std::string&   name;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
            args[1] = Arg::FromEntity(leader);
            args[2] = Arg::FromText(name);
        }

        void Unpack(Arg const*) {}
    };

    /// guild/on_disband: broadcast
    struct GuildDisband
    {
        static constexpr EventId Id = EventId::GuildDisband;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
        }

        void Unpack(Arg const*) {}
    };

    /// guild/on_money_withdraw: broadcast
    struct GuildMoneyWithdraw
    {
        static constexpr EventId Id = EventId::GuildMoneyWithdraw;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;
        Ref            player;
        uint32         amount;    ///< in/out
        bool           isRepair;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
            args[1] = Arg::FromEntity(player);
            args[2] = Arg::FromNumber(amount);
            args[3] = Arg::FromFlag(isRepair);
        }

        void Unpack(Arg const* args)
        {
            amount = static_cast<uint32>(args[2].AsNumber());
        }
    };

    /// guild/on_money_deposit: broadcast
    struct GuildMoneyDeposit
    {
        static constexpr EventId Id = EventId::GuildMoneyDeposit;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;
        Ref            player;
        uint32         amount;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
            args[1] = Arg::FromEntity(player);
            args[2] = Arg::FromNumber(amount);
        }

        void Unpack(Arg const* args)
        {
            amount = static_cast<uint32>(args[2].AsNumber());
        }
    };

    /// guild/on_item_move: broadcast
    struct GuildItemMove
    {
        static constexpr EventId Id = EventId::GuildItemMove;
        static constexpr std::size_t Arity = 9;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;
        Ref            player;
        Ref            item;
        bool           isSrcBank;
        uint32         srcContainer;
        uint32         srcSlotId;
        bool           isDestBank;
        uint32         destContainer;
        uint32         destSlotId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
            args[1] = Arg::FromEntity(player);
            args[2] = Arg::FromEntity(item);
            args[3] = Arg::FromFlag(isSrcBank);
            args[4] = Arg::FromNumber(srcContainer);
            args[5] = Arg::FromNumber(srcSlotId);
            args[6] = Arg::FromFlag(isDestBank);
            args[7] = Arg::FromNumber(destContainer);
            args[8] = Arg::FromNumber(destSlotId);
        }

        void Unpack(Arg const*) {}
    };

    /// guild/on_event: broadcast
    struct GuildEvent
    {
        static constexpr EventId Id = EventId::GuildEvent;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;
        uint32         eventType;
        uint32         playerGuid1;
        uint32         playerGuid2;
        uint32         newRank;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
            args[1] = Arg::FromNumber(eventType);
            args[2] = Arg::FromNumber(playerGuid1);
            args[3] = Arg::FromNumber(playerGuid2);
            args[4] = Arg::FromNumber(newRank);
        }

        void Unpack(Arg const*) {}
    };

    /// guild/on_bank_event: broadcast
    struct GuildBankEvent
    {
        static constexpr EventId Id = EventId::GuildBankEvent;
        static constexpr std::size_t Arity = 7;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         guild;
        uint32         eventType;
        uint32         tabId;
        uint32         playerGuid;
        uint32         itemOrMoney;
        uint32         itemStackCount;
        uint32         destTabId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(guild);
            args[1] = Arg::FromNumber(eventType);
            args[2] = Arg::FromNumber(tabId);
            args[3] = Arg::FromNumber(playerGuid);
            args[4] = Arg::FromNumber(itemOrMoney);
            args[5] = Arg::FromNumber(itemStackCount);
            args[6] = Arg::FromNumber(destTabId);
        }

        void Unpack(Arg const*) {}
    };

    /// group/on_add_member: broadcast
    struct GroupAddMember
    {
        static constexpr EventId Id = EventId::GroupAddMember;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         group;
        Ref            guid;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(group);
            args[1] = Arg::FromEntity(guid);
        }

        void Unpack(Arg const*) {}
    };

    /// group/on_invite_member: broadcast
    struct GroupInviteMember
    {
        static constexpr EventId Id = EventId::GroupInviteMember;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         group;
        Ref            guid;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(group);
            args[1] = Arg::FromEntity(guid);
        }

        void Unpack(Arg const*) {}
    };

    /// group/on_remove_member: broadcast
    struct GroupRemoveMember
    {
        static constexpr EventId Id = EventId::GroupRemoveMember;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         group;
        Ref            guid;
        uint32         method;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(group);
            args[1] = Arg::FromEntity(guid);
            args[2] = Arg::FromNumber(method);
        }

        void Unpack(Arg const*) {}
    };

    /// group/on_leader_change: broadcast
    struct GroupLeaderChange
    {
        static constexpr EventId Id = EventId::GroupLeaderChange;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         group;
        Ref            newLeader;
        Ref            oldLeader;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(group);
            args[1] = Arg::FromEntity(newLeader);
            args[2] = Arg::FromEntity(oldLeader);
        }

        void Unpack(Arg const*) {}
    };

    /// group/on_disband: broadcast
    struct GroupDisband
    {
        static constexpr EventId Id = EventId::GroupDisband;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         group;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(group);
        }

        void Unpack(Arg const*) {}
    };

    /// group/on_create: broadcast
    struct GroupCreate
    {
        static constexpr EventId Id = EventId::GroupCreate;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         group;
        Ref            leaderGuid;
        uint32         groupType;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(group);
            args[1] = Arg::FromEntity(leaderGuid);
            args[2] = Arg::FromNumber(groupType);
        }

        void Unpack(Arg const*) {}
    };

    /// group/on_member_accept: cancel
    struct GroupMemberAccept
    {
        static constexpr EventId Id = EventId::GroupMemberAccept;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Handle         group;
        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(group);
            args[1] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// vehicle/on_install: broadcast
    struct VehicleInstall
    {
        static constexpr EventId Id = EventId::VehicleInstall;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        uint32         vehicle;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNumber(vehicle);
        }

        void Unpack(Arg const*) {}
    };

    /// vehicle/on_uninstall: broadcast
    struct VehicleUninstall
    {
        static constexpr EventId Id = EventId::VehicleUninstall;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        uint32         vehicle;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNumber(vehicle);
        }

        void Unpack(Arg const*) {}
    };

    /// vehicle/on_install_accessory: broadcast
    struct VehicleInstallAccessory
    {
        static constexpr EventId Id = EventId::VehicleInstallAccessory;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        uint32         vehicle;
        Ref            accessory;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNumber(vehicle);
            args[1] = Arg::FromEntity(accessory);
        }

        void Unpack(Arg const*) {}
    };

    /// vehicle/on_add_passenger: broadcast
    struct VehicleAddPassenger
    {
        static constexpr EventId Id = EventId::VehicleAddPassenger;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        uint32         vehicle;
        Ref            passenger;
        int32          seatId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNumber(vehicle);
            args[1] = Arg::FromEntity(passenger);
            args[2] = Arg::FromSigned(seatId);
        }

        void Unpack(Arg const*) {}
    };

    /// vehicle/on_remove_passenger: broadcast
    struct VehicleRemovePassenger
    {
        static constexpr EventId Id = EventId::VehicleRemovePassenger;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        uint32         vehicle;
        Ref            passenger;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNumber(vehicle);
            args[1] = Arg::FromEntity(passenger);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_enter_combat: broadcast
    struct CreatureEnterCombat
    {
        static constexpr EventId Id = EventId::CreatureEnterCombat;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            enemy;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(enemy);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_leave_combat: broadcast
    struct CreatureLeaveCombat
    {
        static constexpr EventId Id = EventId::CreatureLeaveCombat;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_target_died: broadcast
    struct CreatureTargetDied
    {
        static constexpr EventId Id = EventId::CreatureTargetDied;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            victim;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(victim);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_died: broadcast
    struct CreatureDied
    {
        static constexpr EventId Id = EventId::CreatureDied;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            killer;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(killer);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_spawn: broadcast
    struct CreatureSpawn
    {
        static constexpr EventId Id = EventId::CreatureSpawn;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            gameobject;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(gameobject);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_reach_wp: broadcast
    struct CreatureReachWp
    {
        static constexpr EventId Id = EventId::CreatureReachWp;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        int32          pathId;
        uint32         pathOrigin;
        uint32         nodeIndex;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromSigned(pathId);
            args[2] = Arg::FromNumber(pathOrigin);
            args[3] = Arg::FromNumber(nodeIndex);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_receive_emote: broadcast
    struct CreatureReceiveEmote
    {
        static constexpr EventId Id = EventId::CreatureReceiveEmote;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            player;
        uint32         emoteId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(player);
            args[2] = Arg::FromNumber(emoteId);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_damage_taken: broadcast
    struct CreatureDamageTaken
    {
        static constexpr EventId Id = EventId::CreatureDamageTaken;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            attacker;
        uint32         damage;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(attacker);
            args[2] = Arg::FromNumber(damage);
        }

        void Unpack(Arg const* args)
        {
            damage = static_cast<uint32>(args[2].AsNumber());
        }
    };

    /// creature/on_pre_combat: broadcast
    struct CreaturePreCombat
    {
        static constexpr EventId Id = EventId::CreaturePreCombat;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            target;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(target);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_owner_attacked: broadcast
    struct CreatureOwnerAttacked
    {
        static constexpr EventId Id = EventId::CreatureOwnerAttacked;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            target;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(target);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_owner_attacked_at: broadcast
    struct CreatureOwnerAttackedAt
    {
        static constexpr EventId Id = EventId::CreatureOwnerAttackedAt;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            attacker;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(attacker);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_hit_by_spell: broadcast
    struct CreatureHitBySpell
    {
        static constexpr EventId Id = EventId::CreatureHitBySpell;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            caster;
        Handle         spell;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(caster);
            args[2] = Arg::FromNamed(spell);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_spell_hit_target: broadcast
    struct CreatureSpellHitTarget
    {
        static constexpr EventId Id = EventId::CreatureSpellHitTarget;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            target;
        Handle         spell;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(target);
            args[2] = Arg::FromNamed(spell);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_just_summoned_creature: broadcast
    struct CreatureJustSummonedCreature
    {
        static constexpr EventId Id = EventId::CreatureJustSummonedCreature;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            summon;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(summon);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_summoned_creature_despawn: broadcast
    struct CreatureSummonedCreatureDespawn
    {
        static constexpr EventId Id = EventId::CreatureSummonedCreatureDespawn;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            summon;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(summon);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_summoned_creature_died: broadcast
    struct CreatureSummonedCreatureDied
    {
        static constexpr EventId Id = EventId::CreatureSummonedCreatureDied;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            summon;
        Ref            killer;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(summon);
            args[2] = Arg::FromEntity(killer);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_summoned: cancel
    struct CreatureSummoned
    {
        static constexpr EventId Id = EventId::CreatureSummoned;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            summoner;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(summoner);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_reset: broadcast
    struct CreatureReset
    {
        static constexpr EventId Id = EventId::CreatureReset;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_reach_home: broadcast
    struct CreatureReachHome
    {
        static constexpr EventId Id = EventId::CreatureReachHome;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_corpse_removed: broadcast
    struct CreatureCorpseRemoved
    {
        static constexpr EventId Id = EventId::CreatureCorpseRemoved;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        uint32         respawnDelay;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromNumber(respawnDelay);
        }

        void Unpack(Arg const* args)
        {
            respawnDelay = static_cast<uint32>(args[1].AsNumber());
        }
    };

    /// creature/on_move_in_los: broadcast
    struct CreatureMoveInLos
    {
        static constexpr EventId Id = EventId::CreatureMoveInLos;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;
        Ref            who;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
            args[1] = Arg::FromEntity(who);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_dummy_effect: claim
    struct CreatureDummyEffect
    {
        static constexpr EventId Id = EventId::CreatureDummyEffect;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            caster;
        uint32         spellId;
        uint32         effIndex;
        Ref            target;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(caster);
            args[1] = Arg::FromNumber(spellId);
            args[2] = Arg::FromNumber(effIndex);
            args[3] = Arg::FromEntity(target);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_quest_accept: claim
    struct CreatureQuestAccept
    {
        static constexpr EventId Id = EventId::CreatureQuestAccept;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            creature;
        Handle         quest;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(creature);
            args[2] = Arg::FromNamed(quest);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_quest_reward: claim
    struct CreatureQuestReward
    {
        static constexpr EventId Id = EventId::CreatureQuestReward;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            creature;
        Handle         quest;
        uint32         opt;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(creature);
            args[2] = Arg::FromNamed(quest);
            args[3] = Arg::FromNumber(opt);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_dialog_status: claim
    struct CreatureDialogStatus
    {
        static constexpr EventId Id = EventId::CreatureDialogStatus;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            creature;
        uint32         status;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(creature);
            args[2] = Arg::FromNumber(status);
        }

        void Unpack(Arg const* args)
        {
            status = static_cast<uint32>(args[2].AsNumber());
        }
    };

    /// creature/on_add: broadcast
    struct CreatureAdd
    {
        static constexpr EventId Id = EventId::CreatureAdd;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            creature;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
        }

        void Unpack(Arg const*) {}
    };

    /// creature/on_remove: cancel
    struct CreatureRemove
    {
        static constexpr EventId Id = EventId::CreatureRemove;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            creature;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(creature);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_spawn: broadcast
    struct GameobjectSpawn
    {
        static constexpr EventId Id = EventId::GameobjectSpawn;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            gameobject;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(gameobject);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_dummy_effect: claim
    struct GameobjectDummyEffect
    {
        static constexpr EventId Id = EventId::GameobjectDummyEffect;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            caster;
        uint32         spellId;
        uint32         effIndex;
        Ref            target;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(caster);
            args[1] = Arg::FromNumber(spellId);
            args[2] = Arg::FromNumber(effIndex);
            args[3] = Arg::FromEntity(target);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_quest_accept: claim
    struct GameobjectQuestAccept
    {
        static constexpr EventId Id = EventId::GameobjectQuestAccept;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            gameobject;
        Handle         quest;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(gameobject);
            args[2] = Arg::FromNamed(quest);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_quest_reward: claim
    struct GameobjectQuestReward
    {
        static constexpr EventId Id = EventId::GameobjectQuestReward;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            gameobject;
        Handle         quest;
        uint32         opt;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(gameobject);
            args[2] = Arg::FromNamed(quest);
            args[3] = Arg::FromNumber(opt);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_dialog_status: claim
    struct GameobjectDialogStatus
    {
        static constexpr EventId Id = EventId::GameobjectDialogStatus;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            gameobject;
        uint32         status;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(gameobject);
            args[2] = Arg::FromNumber(status);
        }

        void Unpack(Arg const* args)
        {
            status = static_cast<uint32>(args[2].AsNumber());
        }
    };

    /// gameobject/on_destroyed: broadcast
    struct GameobjectDestroyed
    {
        static constexpr EventId Id = EventId::GameobjectDestroyed;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            gameObject;
        Ref            attacker;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(gameObject);
            args[1] = Arg::FromEntity(attacker);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_damaged: broadcast
    struct GameobjectDamaged
    {
        static constexpr EventId Id = EventId::GameobjectDamaged;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            gameObject;
        Ref            attacker;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(gameObject);
            args[1] = Arg::FromEntity(attacker);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_loot_state_change: broadcast
    struct GameobjectLootStateChange
    {
        static constexpr EventId Id = EventId::GameobjectLootStateChange;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            gameObject;
        uint32         state;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(gameObject);
            args[1] = Arg::FromNumber(state);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_go_state_changed: broadcast
    struct GameobjectGoStateChanged
    {
        static constexpr EventId Id = EventId::GameobjectGoStateChanged;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            gameObject;
        uint32         state;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(gameObject);
            args[1] = Arg::FromNumber(state);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_add: broadcast
    struct GameobjectAdd
    {
        static constexpr EventId Id = EventId::GameobjectAdd;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            gameobject;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(gameobject);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_remove: cancel
    struct GameobjectRemove
    {
        static constexpr EventId Id = EventId::GameobjectRemove;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            gameobject;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(gameobject);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_use: claim
    struct GameobjectUse
    {
        static constexpr EventId Id = EventId::GameobjectUse;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            user;
        Ref            gameobject;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(user);
            args[1] = Arg::FromEntity(gameobject);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_activate: broadcast
    struct GameobjectActivate
    {
        static constexpr EventId Id = EventId::GameobjectActivate;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            user;
        Ref            gameobject;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(user);
            args[1] = Arg::FromEntity(gameobject);
        }

        void Unpack(Arg const*) {}
    };

    /// gameobject/on_trap_sprung: broadcast
    struct GameobjectTrapSprung
    {
        static constexpr EventId Id = EventId::GameobjectTrapSprung;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            unit;
        Ref            gameobject;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(unit);
            args[1] = Arg::FromEntity(gameobject);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_cast: broadcast
    struct SpellCast
    {
        static constexpr EventId Id = EventId::SpellCast;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;
        bool           skipCheck;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
            args[1] = Arg::FromFlag(skipCheck);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_aura_application: cancel
    struct SpellAuraApplication
    {
        static constexpr EventId Id = EventId::SpellAuraApplication;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Borrow         aura;
        Borrow         auraEff;
        Ref            target;
        uint32         mode;
        bool           apply;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(aura);
            args[1] = Arg::FromLent(auraEff);
            args[2] = Arg::FromEntity(target);
            args[3] = Arg::FromNumber(mode);
            args[4] = Arg::FromFlag(apply);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_dispel: broadcast
    struct SpellDispel
    {
        static constexpr EventId Id = EventId::SpellDispel;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         aura;
        Borrow         dispel;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(aura);
            args[1] = Arg::FromLent(dispel);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_periodic_tick: cancel
    struct SpellPeriodicTick
    {
        static constexpr EventId Id = EventId::SpellPeriodicTick;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Borrow         aura;
        Borrow         auraEff;
        Ref            target;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(aura);
            args[1] = Arg::FromLent(auraEff);
            args[2] = Arg::FromEntity(target);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_periodic_update: broadcast
    struct SpellPeriodicUpdate
    {
        static constexpr EventId Id = EventId::SpellPeriodicUpdate;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         aura;
        Borrow         auraEff;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(aura);
            args[1] = Arg::FromLent(auraEff);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_aura_calc_amount: broadcast
    struct SpellAuraCalcAmount
    {
        static constexpr EventId Id = EventId::SpellAuraCalcAmount;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         aura;
        Borrow         auraEff;
        int32          amount;    ///< in/out
        bool           canBeRecalculated;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(aura);
            args[1] = Arg::FromLent(auraEff);
            args[2] = Arg::FromSigned(amount);
            args[3] = Arg::FromFlag(canBeRecalculated);
        }

        void Unpack(Arg const* args)
        {
            amount = static_cast<int32>(args[2].AsSigned());
            canBeRecalculated = static_cast<bool>(args[3].AsFlag());
        }
    };

    /// spell/on_calc_periodic: broadcast
    struct SpellCalcPeriodic
    {
        static constexpr EventId Id = EventId::SpellCalcPeriodic;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         aura;
        Borrow         auraEff;
        bool           isPeriodic;    ///< in/out
        int32          amplitude;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(aura);
            args[1] = Arg::FromLent(auraEff);
            args[2] = Arg::FromFlag(isPeriodic);
            args[3] = Arg::FromSigned(amplitude);
        }

        void Unpack(Arg const* args)
        {
            isPeriodic = static_cast<bool>(args[2].AsFlag());
            amplitude = static_cast<int32>(args[3].AsSigned());
        }
    };

    /// spell/on_check_proc: cancel
    struct SpellCheckProc
    {
        static constexpr EventId Id = EventId::SpellCheckProc;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Borrow         aura;
        Borrow         proc;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(aura);
            args[1] = Arg::FromLent(proc);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_proc: cancel
    struct SpellProc
    {
        static constexpr EventId Id = EventId::SpellProc;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Borrow         aura;
        Borrow         proc;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(aura);
            args[1] = Arg::FromLent(proc);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_check_cast: broadcast
    struct SpellCheckCast
    {
        static constexpr EventId Id = EventId::SpellCheckCast;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_before_cast: broadcast
    struct SpellBeforeCast
    {
        static constexpr EventId Id = EventId::SpellBeforeCast;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_after_cast: broadcast
    struct SpellAfterCast
    {
        static constexpr EventId Id = EventId::SpellAfterCast;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_object_area_target: broadcast
    struct SpellObjectAreaTarget
    {
        static constexpr EventId Id = EventId::SpellObjectAreaTarget;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;
        uint32         effIndex;
        Borrow         targets;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
            args[1] = Arg::FromNumber(effIndex);
            args[2] = Arg::FromLent(targets);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_object_target: broadcast
    struct SpellObjectTarget
    {
        static constexpr EventId Id = EventId::SpellObjectTarget;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;
        uint32         effIndex;
        Borrow         target;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
            args[1] = Arg::FromNumber(effIndex);
            args[2] = Arg::FromLent(target);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_dest_target: broadcast
    struct SpellDestTarget
    {
        static constexpr EventId Id = EventId::SpellDestTarget;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;
        uint32         effIndex;
        Borrow         dest;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
            args[1] = Arg::FromNumber(effIndex);
            args[2] = Arg::FromLent(dest);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_effect_launch: cancel
    struct SpellEffectLaunch
    {
        static constexpr EventId Id = EventId::SpellEffectLaunch;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Borrow         spell;
        uint32         effIndex;
        uint32         mode;
        bool           preventDefault;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
            args[1] = Arg::FromNumber(effIndex);
            args[2] = Arg::FromNumber(mode);
            args[3] = Arg::FromFlag(preventDefault);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_effect_launch_target: cancel
    struct SpellEffectLaunchTarget
    {
        static constexpr EventId Id = EventId::SpellEffectLaunchTarget;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Borrow         spell;
        uint32         effIndex;
        uint32         mode;
        bool           preventDefault;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
            args[1] = Arg::FromNumber(effIndex);
            args[2] = Arg::FromNumber(mode);
            args[3] = Arg::FromFlag(preventDefault);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_effect_calc_absorb: broadcast
    struct SpellEffectCalcAbsorb
    {
        static constexpr EventId Id = EventId::SpellEffectCalcAbsorb;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;
        Borrow         damageInfo;
        uint32         resistAmount;    ///< in/out
        int32          absorbAmount;    ///< in/out

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
            args[1] = Arg::FromLent(damageInfo);
            args[2] = Arg::FromNumber(resistAmount);
            args[3] = Arg::FromSigned(absorbAmount);
        }

        void Unpack(Arg const* args)
        {
            resistAmount = static_cast<uint32>(args[2].AsNumber());
            absorbAmount = static_cast<int32>(args[3].AsSigned());
        }
    };

    /// spell/on_effect_hit: claim
    struct SpellEffectHit
    {
        static constexpr EventId Id = EventId::SpellEffectHit;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            caster;
        Ref            target;
        uint32         spellId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(caster);
            args[1] = Arg::FromEntity(target);
            args[2] = Arg::FromNumber(spellId);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_before_hit: broadcast
    struct SpellBeforeHit
    {
        static constexpr EventId Id = EventId::SpellBeforeHit;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;
        uint32         missInfo;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
            args[1] = Arg::FromNumber(missInfo);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_effect_hit_target: cancel
    struct SpellEffectHitTarget
    {
        static constexpr EventId Id = EventId::SpellEffectHitTarget;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Borrow         spell;
        uint32         effIndex;
        uint32         mode;
        bool           preventDefault;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
            args[1] = Arg::FromNumber(effIndex);
            args[2] = Arg::FromNumber(mode);
            args[3] = Arg::FromFlag(preventDefault);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_hit: broadcast
    struct SpellHit
    {
        static constexpr EventId Id = EventId::SpellHit;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
        }

        void Unpack(Arg const*) {}
    };

    /// spell/on_after_hit: broadcast
    struct SpellAfterHit
    {
        static constexpr EventId Id = EventId::SpellAfterHit;
        static constexpr std::size_t Arity = 1;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Borrow         spell;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(spell);
        }

        void Unpack(Arg const*) {}
    };

    /// item/on_dummy_effect: claim
    struct ItemDummyEffect
    {
        static constexpr EventId Id = EventId::ItemDummyEffect;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            caster;
        uint32         spellId;
        uint32         effIndex;
        Ref            target;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(caster);
            args[1] = Arg::FromNumber(spellId);
            args[2] = Arg::FromNumber(effIndex);
            args[3] = Arg::FromEntity(target);
        }

        void Unpack(Arg const*) {}
    };

    /// item/on_use: cancel
    struct ItemUse
    {
        static constexpr EventId Id = EventId::ItemUse;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            item;
        Borrow         targets;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(item);
            args[2] = Arg::FromLent(targets);
        }

        void Unpack(Arg const*) {}
    };

    /// item/on_quest_accept: claim
    struct ItemQuestAccept
    {
        static constexpr EventId Id = EventId::ItemQuestAccept;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            item;
        Handle         quest;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(item);
            args[2] = Arg::FromNamed(quest);
        }

        void Unpack(Arg const*) {}
    };

    /// item/on_expire: cancel
    struct ItemExpire
    {
        static constexpr EventId Id = EventId::ItemExpire;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        Handle         proto;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNamed(proto);
        }

        void Unpack(Arg const*) {}
    };

    /// item/on_remove: cancel
    struct ItemRemove
    {
        static constexpr EventId Id = EventId::ItemRemove;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = true;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            item;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(item);
        }

        void Unpack(Arg const*) {}
    };

    /// item/on_add: broadcast
    struct ItemAdd
    {
        static constexpr EventId Id = EventId::ItemAdd;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            item;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(item);
        }

        void Unpack(Arg const*) {}
    };

    /// item/on_equip: broadcast
    struct ItemEquip
    {
        static constexpr EventId Id = EventId::ItemEquip;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            item;
        uint32         bag;
        uint32         slot;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(item);
            args[2] = Arg::FromNumber(bag);
            args[3] = Arg::FromNumber(slot);
        }

        void Unpack(Arg const*) {}
    };

    /// item/on_unequip: broadcast
    struct ItemUnequip
    {
        static constexpr EventId Id = EventId::ItemUnequip;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            item;
        uint32         slot;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(item);
            args[2] = Arg::FromNumber(slot);
        }

        void Unpack(Arg const*) {}
    };

    /// bg/on_start: broadcast
    struct BgStart
    {
        static constexpr EventId Id = EventId::BgStart;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         bg;
        uint32         bgId;
        uint32         instanceId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(bg);
            args[1] = Arg::FromNumber(bgId);
            args[2] = Arg::FromNumber(instanceId);
        }

        void Unpack(Arg const*) {}
    };

    /// bg/on_end: broadcast
    struct BgEnd
    {
        static constexpr EventId Id = EventId::BgEnd;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         bg;
        uint32         bgId;
        uint32         instanceId;
        uint32         winner;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(bg);
            args[1] = Arg::FromNumber(bgId);
            args[2] = Arg::FromNumber(instanceId);
            args[3] = Arg::FromNumber(winner);
        }

        void Unpack(Arg const*) {}
    };

    /// bg/on_create: broadcast
    struct BgCreate
    {
        static constexpr EventId Id = EventId::BgCreate;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         bg;
        uint32         bgId;
        uint32         instanceId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(bg);
            args[1] = Arg::FromNumber(bgId);
            args[2] = Arg::FromNumber(instanceId);
        }

        void Unpack(Arg const*) {}
    };

    /// bg/on_pre_destroy: broadcast
    struct BgPreDestroy
    {
        static constexpr EventId Id = EventId::BgPreDestroy;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Handle         bg;
        uint32         bgId;
        uint32         instanceId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromNamed(bg);
            args[1] = Arg::FromNumber(bgId);
            args[2] = Arg::FromNumber(instanceId);
        }

        void Unpack(Arg const*) {}
    };

    /// gossip/creature_hello: claim
    struct GossipCreatureHello
    {
        static constexpr EventId Id = EventId::GossipCreatureHello;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            creature;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(creature);
        }

        void Unpack(Arg const*) {}
    };

    /// gossip/creature_select: claim
    struct GossipCreatureSelect
    {
        static constexpr EventId Id = EventId::GossipCreatureSelect;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            creature;
        uint32         sender;
        uint32         action;
        std::string&   code;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(creature);
            args[2] = Arg::FromNumber(sender);
            args[3] = Arg::FromNumber(action);
            args[4] = Arg::FromText(code);
        }

        void Unpack(Arg const*) {}
    };

    /// gossip/gameobject_hello: claim
    struct GossipGameobjectHello
    {
        static constexpr EventId Id = EventId::GossipGameobjectHello;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            gameobject;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(gameobject);
        }

        void Unpack(Arg const*) {}
    };

    /// gossip/gameobject_select: claim
    struct GossipGameobjectSelect
    {
        static constexpr EventId Id = EventId::GossipGameobjectSelect;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            gameobject;
        uint32         sender;
        uint32         action;
        std::string&   code;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(gameobject);
            args[2] = Arg::FromNumber(sender);
            args[3] = Arg::FromNumber(action);
            args[4] = Arg::FromText(code);
        }

        void Unpack(Arg const*) {}
    };

    /// gossip/item_hello: claim
    struct GossipItemHello
    {
        static constexpr EventId Id = EventId::GossipItemHello;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            item;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(item);
        }

        void Unpack(Arg const*) {}
    };

    /// gossip/item_select: claim
    struct GossipItemSelect
    {
        static constexpr EventId Id = EventId::GossipItemSelect;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            item;
        uint32         sender;
        uint32         action;
        std::string&   code;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(item);
            args[2] = Arg::FromNumber(sender);
            args[3] = Arg::FromNumber(action);
            args[4] = Arg::FromText(code);
        }

        void Unpack(Arg const*) {}
    };

    /// gossip/player_menu_select: claim
    struct GossipPlayerMenuSelect
    {
        static constexpr EventId Id = EventId::GossipPlayerMenuSelect;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        uint32         menuId;
        uint32         sender;
        uint32         action;
        std::string&   code;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromNumber(menuId);
            args[2] = Arg::FromNumber(sender);
            args[3] = Arg::FromNumber(action);
            args[4] = Arg::FromText(code);
        }

        void Unpack(Arg const*) {}
    };

    /// gossip/action_chosen: broadcast
    struct GossipActionChosen
    {
        static constexpr EventId Id = EventId::GossipActionChosen;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            source;
        uint32         menuId;
        uint32         gossipListId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(source);
            args[2] = Arg::FromNumber(menuId);
            args[3] = Arg::FromNumber(gossipListId);
        }

        void Unpack(Arg const*) {}
    };

    /// gossip/menu_shown: broadcast
    struct GossipMenuShown
    {
        static constexpr EventId Id = EventId::GossipMenuShown;
        static constexpr std::size_t Arity = 4;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = false;

        Ref            player;
        Ref            source;
        uint32         menuId;
        uint32         textId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(source);
            args[2] = Arg::FromNumber(menuId);
            args[3] = Arg::FromNumber(textId);
        }

        void Unpack(Arg const*) {}
    };

    /// core/on_npc_spell_click: claim
    struct CoreNpcSpellClick
    {
        static constexpr EventId Id = EventId::CoreNpcSpellClick;
        static constexpr std::size_t Arity = 3;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            player;
        Ref            creature;
        uint32         spellId;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(player);
            args[1] = Arg::FromEntity(creature);
            args[2] = Arg::FromNumber(spellId);
        }

        void Unpack(Arg const*) {}
    };

    /// core/on_effect_script_effect: claim
    struct CoreEffectScriptEffect
    {
        static constexpr EventId Id = EventId::CoreEffectScriptEffect;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            caster;
        uint32         spellId;
        uint32         effIndex;
        Ref            target;
        Ref            originalCaster;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(caster);
            args[1] = Arg::FromNumber(spellId);
            args[2] = Arg::FromNumber(effIndex);
            args[3] = Arg::FromEntity(target);
            args[4] = Arg::FromEntity(originalCaster);
        }

        void Unpack(Arg const*) {}
    };

    /// core/on_aura_dummy: claim
    struct CoreAuraDummy
    {
        static constexpr EventId Id = EventId::CoreAuraDummy;
        static constexpr std::size_t Arity = 2;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Borrow         aura;
        bool           apply;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromLent(aura);
            args[1] = Arg::FromFlag(apply);
        }

        void Unpack(Arg const*) {}
    };

    /// core/on_effect_dummy: claim
    struct CoreEffectDummy
    {
        static constexpr EventId Id = EventId::CoreEffectDummy;
        static constexpr std::size_t Arity = 5;
        static constexpr bool Cancellable = false;
        static constexpr bool Claimable = true;

        Ref            caster;
        uint32         spellId;
        uint32         effIndex;
        Ref            target;
        Ref            originalCaster;

        void Pack(Arg* args) const
        {
            args[0] = Arg::FromEntity(caster);
            args[1] = Arg::FromNumber(spellId);
            args[2] = Arg::FromNumber(effIndex);
            args[3] = Arg::FromEntity(target);
            args[4] = Arg::FromEntity(originalCaster);
        }

        void Unpack(Arg const*) {}
    };

    // -- the same events as run-time data; see describe() in
    //    tools/gen_events.py for why both forms exist.

    struct ArgSpec
    {
        char const* name;
        Arg::Kind   kind;
        Domain      domain;   ///< None unless Named or Lent
        bool        inout;
    };

    struct EventSpec
    {
        EventId        id;
        char const*    name;        ///< "player.on_login"
        ArgSpec const* args;
        std::size_t    arity;
        bool           cancellable;
        bool           claimable;
    };

    inline constexpr ArgSpec g_argsPacketReceive[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "packet", Arg::Kind::Lent, Domain::Packet, true },
    };

    inline constexpr ArgSpec g_argsPacketReceiveUnk[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "packet", Arg::Kind::Lent, Domain::Packet, true },
    };

    inline constexpr ArgSpec g_argsPacketSend[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "packet", Arg::Kind::Lent, Domain::Packet, false },
    };

    inline constexpr ArgSpec g_argsServerNetworkStart[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerNetworkStop[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerSocketOpen[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerSocketClose[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerPacketReceive[] =
    {
        { "session", Arg::Kind::Lent, Domain::Session, false },
        { "packet", Arg::Kind::Lent, Domain::Packet, true },
    };

    inline constexpr ArgSpec g_argsServerPacketReceiveUnk[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "packet", Arg::Kind::Lent, Domain::Packet, true },
    };

    inline constexpr ArgSpec g_argsServerPacketSend[] =
    {
        { "session", Arg::Kind::Lent, Domain::Session, false },
        { "packet", Arg::Kind::Lent, Domain::Packet, false },
    };

    inline constexpr ArgSpec g_argsServerOpenStateChange[] =
    {
        { "open", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerConfigLoad[] =
    {
        { "reload", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerShutdownInit[] =
    {
        { "code", Arg::Kind::Number, Domain::None, false },
        { "mask", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerShutdownCancel[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerWorldUpdate[] =
    {
        { "diff", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerWorldStartup[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerWorldShutdown[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerLuaStateClose[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerMapCreate[] =
    {
        { "map", Arg::Kind::Named, Domain::Map, false },
    };

    inline constexpr ArgSpec g_argsServerMapDestroy[] =
    {
        { "map", Arg::Kind::Named, Domain::Map, false },
    };

    inline constexpr ArgSpec g_argsServerMapGridLoad[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerMapGridUnload[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerMapPlayerEnter[] =
    {
        { "map", Arg::Kind::Named, Domain::Map, false },
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerMapPlayerLeave[] =
    {
        { "map", Arg::Kind::Named, Domain::Map, false },
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerMapUpdate[] =
    {
        { "map", Arg::Kind::Named, Domain::Map, false },
        { "diff", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerEventTrigger[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "trigger", Arg::Kind::Named, Domain::AreaTrigger, false },
    };

    inline constexpr ArgSpec g_argsServerWeatherChange[] =
    {
        { "weather", Arg::Kind::Named, Domain::Weather, false },
        { "zone", Arg::Kind::Number, Domain::None, false },
        { "state", Arg::Kind::Number, Domain::None, false },
        { "grade", Arg::Kind::Real, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerAuctionAdd[] =
    {
        { "house", Arg::Kind::Lent, Domain::AuctionHouse, false },
        { "entry", Arg::Kind::Named, Domain::Auction, false },
    };

    inline constexpr ArgSpec g_argsServerAuctionRemove[] =
    {
        { "house", Arg::Kind::Lent, Domain::AuctionHouse, false },
        { "entry", Arg::Kind::Named, Domain::Auction, false },
    };

    inline constexpr ArgSpec g_argsServerAuctionSuccessful[] =
    {
        { "house", Arg::Kind::Lent, Domain::AuctionHouse, false },
        { "entry", Arg::Kind::Named, Domain::Auction, false },
    };

    inline constexpr ArgSpec g_argsServerAuctionExpire[] =
    {
        { "house", Arg::Kind::Lent, Domain::AuctionHouse, false },
        { "entry", Arg::Kind::Named, Domain::Auction, false },
    };

    inline constexpr ArgSpec g_argsServerAddonMessage[] =
    {
        { "sender", Arg::Kind::Entity, Domain::None, false },
        { "type", Arg::Kind::Number, Domain::None, false },
        { "msg", Arg::Kind::Text, Domain::None, true },
        { "receiver", Arg::Kind::Entity, Domain::None, false },
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "group", Arg::Kind::Named, Domain::Group, false },
        { "channel", Arg::Kind::Lent, Domain::Channel, false },
    };

    inline constexpr ArgSpec g_argsServerWorldDeleteCreature[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerWorldDeleteGameobject[] =
    {
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerLuaStateOpen[] =
    {
        { "subject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerGameStart[] =
    {
        { "eventId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerGameStop[] =
    {
        { "eventId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsServerEventRaised[] =
    {
        { "source", Arg::Kind::Entity, Domain::None, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
        { "eventId", Arg::Kind::Number, Domain::None, false },
        { "isStart", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerCharacterCreate[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerCharacterDelete[] =
    {
        { "guid", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerLogin[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerLogout[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerSpellCast[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
        { "skipCheck", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerKillPlayer[] =
    {
        { "killer", Arg::Kind::Entity, Domain::None, false },
        { "killed", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerKillCreature[] =
    {
        { "killer", Arg::Kind::Entity, Domain::None, false },
        { "killed", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerKilledByCreature[] =
    {
        { "killer", Arg::Kind::Entity, Domain::None, false },
        { "killed", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerDuelRequest[] =
    {
        { "target", Arg::Kind::Entity, Domain::None, false },
        { "challenger", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerDuelStart[] =
    {
        { "starter", Arg::Kind::Entity, Domain::None, false },
        { "challenger", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerDuelEnd[] =
    {
        { "winner", Arg::Kind::Entity, Domain::None, false },
        { "loser", Arg::Kind::Entity, Domain::None, false },
        { "type", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerGiveXp[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "amount", Arg::Kind::Number, Domain::None, true },
        { "victim", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerLevelChange[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "oldLevel", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerMoneyChange[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "amount", Arg::Kind::Signed, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsPlayerReputationChange[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "factionID", Arg::Kind::Number, Domain::None, false },
        { "standing", Arg::Kind::Signed, Domain::None, true },
        { "incremental", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerTalentsChange[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "newPoints", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerTalentsReset[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "noCost", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerChat[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "type", Arg::Kind::Number, Domain::None, false },
        { "lang", Arg::Kind::Number, Domain::None, false },
        { "msg", Arg::Kind::Text, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsPlayerWhisper[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "type", Arg::Kind::Number, Domain::None, false },
        { "lang", Arg::Kind::Number, Domain::None, false },
        { "msg", Arg::Kind::Text, Domain::None, true },
        { "receiver", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerGroupChat[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "type", Arg::Kind::Number, Domain::None, false },
        { "lang", Arg::Kind::Number, Domain::None, false },
        { "msg", Arg::Kind::Text, Domain::None, true },
        { "group", Arg::Kind::Named, Domain::Group, false },
    };

    inline constexpr ArgSpec g_argsPlayerGuildChat[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "type", Arg::Kind::Number, Domain::None, false },
        { "lang", Arg::Kind::Number, Domain::None, false },
        { "msg", Arg::Kind::Text, Domain::None, true },
        { "guild", Arg::Kind::Named, Domain::Guild, false },
    };

    inline constexpr ArgSpec g_argsPlayerChannelChat[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "type", Arg::Kind::Number, Domain::None, false },
        { "lang", Arg::Kind::Number, Domain::None, false },
        { "msg", Arg::Kind::Text, Domain::None, true },
        { "channel", Arg::Kind::Lent, Domain::Channel, false },
    };

    inline constexpr ArgSpec g_argsPlayerEmote[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "emote", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerTextEmote[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "textEmote", Arg::Kind::Number, Domain::None, false },
        { "emoteNum", Arg::Kind::Number, Domain::None, false },
        { "guid", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerSave[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerBindToInstance[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "difficulty", Arg::Kind::Number, Domain::None, false },
        { "mapid", Arg::Kind::Number, Domain::None, false },
        { "permanent", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerUpdateZone[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "newZone", Arg::Kind::Number, Domain::None, false },
        { "newArea", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerMapChange[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerEquip[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
        { "bag", Arg::Kind::Number, Domain::None, false },
        { "slot", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerFirstLogin[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerCanUseItem[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "itemEntry", Arg::Kind::Number, Domain::None, false },
        { "result", Arg::Kind::Number, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsPlayerLootItem[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
        { "count", Arg::Kind::Number, Domain::None, false },
        { "guid", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerEnterCombat[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "enemy", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerLeaveCombat[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerRepop[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerResurrect[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerLootMoney[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "amount", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerQuestAbandon[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "questId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerLearnTalents[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "talentId", Arg::Kind::Number, Domain::None, false },
        { "talentRank", Arg::Kind::Number, Domain::None, false },
        { "spellid", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerEnvironmentalDeath[] =
    {
        { "killed", Arg::Kind::Entity, Domain::None, false },
        { "damageType", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerTradeAccept[] =
    {
        { "trader", Arg::Kind::Entity, Domain::None, false },
        { "tradee", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerCommand[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "text", Arg::Kind::Text, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerSkillChange[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "skillId", Arg::Kind::Number, Domain::None, false },
        { "skillValue", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerLearnSpell[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "spellid", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerAchievementComplete[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "achievementId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerDiscoverArea[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "area", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerUpdateArea[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "oldArea", Arg::Kind::Number, Domain::None, false },
        { "newArea", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerTradeInit[] =
    {
        { "trader", Arg::Kind::Entity, Domain::None, false },
        { "tradee", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerSendMail[] =
    {
        { "sender", Arg::Kind::Entity, Domain::None, false },
        { "recipientGuid", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerQuestStatusChanged[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "questId", Arg::Kind::Number, Domain::None, false },
        { "status", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsPlayerQuestStart[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "questGiver", Arg::Kind::Entity, Domain::None, false },
        { "quest", Arg::Kind::Named, Domain::Quest, false },
    };

    inline constexpr ArgSpec g_argsPlayerQuestEnd[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "questGiver", Arg::Kind::Entity, Domain::None, false },
        { "quest", Arg::Kind::Named, Domain::Quest, false },
    };

    inline constexpr ArgSpec g_argsGuildAddMember[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "plRank", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGuildRemoveMember[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "isDisbanding", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGuildMotdChange[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "motd", Arg::Kind::Text, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGuildInfoChange[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "info", Arg::Kind::Text, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGuildCreate[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "leader", Arg::Kind::Entity, Domain::None, false },
        { "name", Arg::Kind::Text, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGuildDisband[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
    };

    inline constexpr ArgSpec g_argsGuildMoneyWithdraw[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "amount", Arg::Kind::Number, Domain::None, true },
        { "isRepair", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGuildMoneyDeposit[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "amount", Arg::Kind::Number, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsGuildItemMove[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
        { "isSrcBank", Arg::Kind::Flag, Domain::None, false },
        { "srcContainer", Arg::Kind::Number, Domain::None, false },
        { "srcSlotId", Arg::Kind::Number, Domain::None, false },
        { "isDestBank", Arg::Kind::Flag, Domain::None, false },
        { "destContainer", Arg::Kind::Number, Domain::None, false },
        { "destSlotId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGuildEvent[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "eventType", Arg::Kind::Number, Domain::None, false },
        { "playerGuid1", Arg::Kind::Number, Domain::None, false },
        { "playerGuid2", Arg::Kind::Number, Domain::None, false },
        { "newRank", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGuildBankEvent[] =
    {
        { "guild", Arg::Kind::Named, Domain::Guild, false },
        { "eventType", Arg::Kind::Number, Domain::None, false },
        { "tabId", Arg::Kind::Number, Domain::None, false },
        { "playerGuid", Arg::Kind::Number, Domain::None, false },
        { "itemOrMoney", Arg::Kind::Number, Domain::None, false },
        { "itemStackCount", Arg::Kind::Number, Domain::None, false },
        { "destTabId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGroupAddMember[] =
    {
        { "group", Arg::Kind::Named, Domain::Group, false },
        { "guid", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGroupInviteMember[] =
    {
        { "group", Arg::Kind::Named, Domain::Group, false },
        { "guid", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGroupRemoveMember[] =
    {
        { "group", Arg::Kind::Named, Domain::Group, false },
        { "guid", Arg::Kind::Entity, Domain::None, false },
        { "method", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGroupLeaderChange[] =
    {
        { "group", Arg::Kind::Named, Domain::Group, false },
        { "newLeader", Arg::Kind::Entity, Domain::None, false },
        { "oldLeader", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGroupDisband[] =
    {
        { "group", Arg::Kind::Named, Domain::Group, false },
    };

    inline constexpr ArgSpec g_argsGroupCreate[] =
    {
        { "group", Arg::Kind::Named, Domain::Group, false },
        { "leaderGuid", Arg::Kind::Entity, Domain::None, false },
        { "groupType", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGroupMemberAccept[] =
    {
        { "group", Arg::Kind::Named, Domain::Group, false },
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsVehicleInstall[] =
    {
        { "vehicle", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsVehicleUninstall[] =
    {
        { "vehicle", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsVehicleInstallAccessory[] =
    {
        { "vehicle", Arg::Kind::Number, Domain::None, false },
        { "accessory", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsVehicleAddPassenger[] =
    {
        { "vehicle", Arg::Kind::Number, Domain::None, false },
        { "passenger", Arg::Kind::Entity, Domain::None, false },
        { "seatId", Arg::Kind::Signed, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsVehicleRemovePassenger[] =
    {
        { "vehicle", Arg::Kind::Number, Domain::None, false },
        { "passenger", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureEnterCombat[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "enemy", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureLeaveCombat[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureTargetDied[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "victim", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureDied[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "killer", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureSpawn[] =
    {
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureReachWp[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "pathId", Arg::Kind::Signed, Domain::None, false },
        { "pathOrigin", Arg::Kind::Number, Domain::None, false },
        { "nodeIndex", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureReceiveEmote[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "emoteId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureDamageTaken[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "attacker", Arg::Kind::Entity, Domain::None, false },
        { "damage", Arg::Kind::Number, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsCreaturePreCombat[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureOwnerAttacked[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureOwnerAttackedAt[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "attacker", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureHitBySpell[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "caster", Arg::Kind::Entity, Domain::None, false },
        { "spell", Arg::Kind::Named, Domain::SpellInfo, false },
    };

    inline constexpr ArgSpec g_argsCreatureSpellHitTarget[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
        { "spell", Arg::Kind::Named, Domain::SpellInfo, false },
    };

    inline constexpr ArgSpec g_argsCreatureJustSummonedCreature[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "summon", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureSummonedCreatureDespawn[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "summon", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureSummonedCreatureDied[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "summon", Arg::Kind::Entity, Domain::None, false },
        { "killer", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureSummoned[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "summoner", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureReset[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureReachHome[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureCorpseRemoved[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "respawnDelay", Arg::Kind::Number, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsCreatureMoveInLos[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "who", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureDummyEffect[] =
    {
        { "caster", Arg::Kind::Entity, Domain::None, false },
        { "spellId", Arg::Kind::Number, Domain::None, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureQuestAccept[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "quest", Arg::Kind::Named, Domain::Quest, false },
    };

    inline constexpr ArgSpec g_argsCreatureQuestReward[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "quest", Arg::Kind::Named, Domain::Quest, false },
        { "opt", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureDialogStatus[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "status", Arg::Kind::Number, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsCreatureAdd[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCreatureRemove[] =
    {
        { "creature", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectSpawn[] =
    {
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectDummyEffect[] =
    {
        { "caster", Arg::Kind::Entity, Domain::None, false },
        { "spellId", Arg::Kind::Number, Domain::None, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectQuestAccept[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
        { "quest", Arg::Kind::Named, Domain::Quest, false },
    };

    inline constexpr ArgSpec g_argsGameobjectQuestReward[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
        { "quest", Arg::Kind::Named, Domain::Quest, false },
        { "opt", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectDialogStatus[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
        { "status", Arg::Kind::Number, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsGameobjectDestroyed[] =
    {
        { "gameObject", Arg::Kind::Entity, Domain::None, false },
        { "attacker", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectDamaged[] =
    {
        { "gameObject", Arg::Kind::Entity, Domain::None, false },
        { "attacker", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectLootStateChange[] =
    {
        { "gameObject", Arg::Kind::Entity, Domain::None, false },
        { "state", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectGoStateChanged[] =
    {
        { "gameObject", Arg::Kind::Entity, Domain::None, false },
        { "state", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectAdd[] =
    {
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectRemove[] =
    {
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectUse[] =
    {
        { "user", Arg::Kind::Entity, Domain::None, false },
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectActivate[] =
    {
        { "user", Arg::Kind::Entity, Domain::None, false },
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGameobjectTrapSprung[] =
    {
        { "unit", Arg::Kind::Entity, Domain::None, false },
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsSpellCast[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
        { "skipCheck", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsSpellAuraApplication[] =
    {
        { "aura", Arg::Kind::Lent, Domain::Aura, false },
        { "auraEff", Arg::Kind::Lent, Domain::AuraEffect, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
        { "mode", Arg::Kind::Number, Domain::None, false },
        { "apply", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsSpellDispel[] =
    {
        { "aura", Arg::Kind::Lent, Domain::Aura, false },
        { "dispel", Arg::Kind::Lent, Domain::Dispel, false },
    };

    inline constexpr ArgSpec g_argsSpellPeriodicTick[] =
    {
        { "aura", Arg::Kind::Lent, Domain::Aura, false },
        { "auraEff", Arg::Kind::Lent, Domain::AuraEffect, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsSpellPeriodicUpdate[] =
    {
        { "aura", Arg::Kind::Lent, Domain::Aura, false },
        { "auraEff", Arg::Kind::Lent, Domain::AuraEffect, false },
    };

    inline constexpr ArgSpec g_argsSpellAuraCalcAmount[] =
    {
        { "aura", Arg::Kind::Lent, Domain::Aura, false },
        { "auraEff", Arg::Kind::Lent, Domain::AuraEffect, false },
        { "amount", Arg::Kind::Signed, Domain::None, true },
        { "canBeRecalculated", Arg::Kind::Flag, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsSpellCalcPeriodic[] =
    {
        { "aura", Arg::Kind::Lent, Domain::Aura, false },
        { "auraEff", Arg::Kind::Lent, Domain::AuraEffect, false },
        { "isPeriodic", Arg::Kind::Flag, Domain::None, true },
        { "amplitude", Arg::Kind::Signed, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsSpellCheckProc[] =
    {
        { "aura", Arg::Kind::Lent, Domain::Aura, false },
        { "proc", Arg::Kind::Lent, Domain::Proc, false },
    };

    inline constexpr ArgSpec g_argsSpellProc[] =
    {
        { "aura", Arg::Kind::Lent, Domain::Aura, false },
        { "proc", Arg::Kind::Lent, Domain::Proc, false },
    };

    inline constexpr ArgSpec g_argsSpellCheckCast[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
    };

    inline constexpr ArgSpec g_argsSpellBeforeCast[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
    };

    inline constexpr ArgSpec g_argsSpellAfterCast[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
    };

    inline constexpr ArgSpec g_argsSpellObjectAreaTarget[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "targets", Arg::Kind::Lent, Domain::ObjectList, false },
    };

    inline constexpr ArgSpec g_argsSpellObjectTarget[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "target", Arg::Kind::Lent, Domain::ObjectSlot, false },
    };

    inline constexpr ArgSpec g_argsSpellDestTarget[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "dest", Arg::Kind::Lent, Domain::SpellDestination, false },
    };

    inline constexpr ArgSpec g_argsSpellEffectLaunch[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "mode", Arg::Kind::Number, Domain::None, false },
        { "preventDefault", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsSpellEffectLaunchTarget[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "mode", Arg::Kind::Number, Domain::None, false },
        { "preventDefault", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsSpellEffectCalcAbsorb[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
        { "damageInfo", Arg::Kind::Lent, Domain::Damage, false },
        { "resistAmount", Arg::Kind::Number, Domain::None, true },
        { "absorbAmount", Arg::Kind::Signed, Domain::None, true },
    };

    inline constexpr ArgSpec g_argsSpellEffectHit[] =
    {
        { "caster", Arg::Kind::Entity, Domain::None, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
        { "spellId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsSpellBeforeHit[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
        { "missInfo", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsSpellEffectHitTarget[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "mode", Arg::Kind::Number, Domain::None, false },
        { "preventDefault", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsSpellHit[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
    };

    inline constexpr ArgSpec g_argsSpellAfterHit[] =
    {
        { "spell", Arg::Kind::Lent, Domain::Spell, false },
    };

    inline constexpr ArgSpec g_argsItemDummyEffect[] =
    {
        { "caster", Arg::Kind::Entity, Domain::None, false },
        { "spellId", Arg::Kind::Number, Domain::None, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsItemUse[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
        { "targets", Arg::Kind::Lent, Domain::CastTargets, false },
    };

    inline constexpr ArgSpec g_argsItemQuestAccept[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
        { "quest", Arg::Kind::Named, Domain::Quest, false },
    };

    inline constexpr ArgSpec g_argsItemExpire[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "proto", Arg::Kind::Named, Domain::ItemTemplate, false },
    };

    inline constexpr ArgSpec g_argsItemRemove[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsItemAdd[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsItemEquip[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
        { "bag", Arg::Kind::Number, Domain::None, false },
        { "slot", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsItemUnequip[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
        { "slot", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsBgStart[] =
    {
        { "bg", Arg::Kind::Named, Domain::BattleGround, false },
        { "bgId", Arg::Kind::Number, Domain::None, false },
        { "instanceId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsBgEnd[] =
    {
        { "bg", Arg::Kind::Named, Domain::BattleGround, false },
        { "bgId", Arg::Kind::Number, Domain::None, false },
        { "instanceId", Arg::Kind::Number, Domain::None, false },
        { "winner", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsBgCreate[] =
    {
        { "bg", Arg::Kind::Named, Domain::BattleGround, false },
        { "bgId", Arg::Kind::Number, Domain::None, false },
        { "instanceId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsBgPreDestroy[] =
    {
        { "bg", Arg::Kind::Named, Domain::BattleGround, false },
        { "bgId", Arg::Kind::Number, Domain::None, false },
        { "instanceId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGossipCreatureHello[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "creature", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGossipCreatureSelect[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "sender", Arg::Kind::Number, Domain::None, false },
        { "action", Arg::Kind::Number, Domain::None, false },
        { "code", Arg::Kind::Text, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGossipGameobjectHello[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGossipGameobjectSelect[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "gameobject", Arg::Kind::Entity, Domain::None, false },
        { "sender", Arg::Kind::Number, Domain::None, false },
        { "action", Arg::Kind::Number, Domain::None, false },
        { "code", Arg::Kind::Text, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGossipItemHello[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGossipItemSelect[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "item", Arg::Kind::Entity, Domain::None, false },
        { "sender", Arg::Kind::Number, Domain::None, false },
        { "action", Arg::Kind::Number, Domain::None, false },
        { "code", Arg::Kind::Text, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGossipPlayerMenuSelect[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "menuId", Arg::Kind::Number, Domain::None, false },
        { "sender", Arg::Kind::Number, Domain::None, false },
        { "action", Arg::Kind::Number, Domain::None, false },
        { "code", Arg::Kind::Text, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGossipActionChosen[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "source", Arg::Kind::Entity, Domain::None, false },
        { "menuId", Arg::Kind::Number, Domain::None, false },
        { "gossipListId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsGossipMenuShown[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "source", Arg::Kind::Entity, Domain::None, false },
        { "menuId", Arg::Kind::Number, Domain::None, false },
        { "textId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCoreNpcSpellClick[] =
    {
        { "player", Arg::Kind::Entity, Domain::None, false },
        { "creature", Arg::Kind::Entity, Domain::None, false },
        { "spellId", Arg::Kind::Number, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCoreEffectScriptEffect[] =
    {
        { "caster", Arg::Kind::Entity, Domain::None, false },
        { "spellId", Arg::Kind::Number, Domain::None, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
        { "originalCaster", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCoreAuraDummy[] =
    {
        { "aura", Arg::Kind::Lent, Domain::Aura, false },
        { "apply", Arg::Kind::Flag, Domain::None, false },
    };

    inline constexpr ArgSpec g_argsCoreEffectDummy[] =
    {
        { "caster", Arg::Kind::Entity, Domain::None, false },
        { "spellId", Arg::Kind::Number, Domain::None, false },
        { "effIndex", Arg::Kind::Number, Domain::None, false },
        { "target", Arg::Kind::Entity, Domain::None, false },
        { "originalCaster", Arg::Kind::Entity, Domain::None, false },
    };

    inline constexpr EventSpec g_eventSpecs[] =
    {
        { EventId::PacketReceive, "packet.on_receive", g_argsPacketReceive, 2, true, false },
        { EventId::PacketReceiveUnk, "packet.on_receive_unk", g_argsPacketReceiveUnk, 2, true, false },
        { EventId::PacketSend, "packet.on_send", g_argsPacketSend, 2, true, false },
        { EventId::ServerNetworkStart, "server.on_network_start", g_argsServerNetworkStart, 1, false, false },
        { EventId::ServerNetworkStop, "server.on_network_stop", g_argsServerNetworkStop, 1, false, false },
        { EventId::ServerSocketOpen, "server.on_socket_open", g_argsServerSocketOpen, 1, false, false },
        { EventId::ServerSocketClose, "server.on_socket_close", g_argsServerSocketClose, 1, false, false },
        { EventId::ServerPacketReceive, "server.on_packet_receive", g_argsServerPacketReceive, 2, true, false },
        { EventId::ServerPacketReceiveUnk, "server.on_packet_receive_unk", g_argsServerPacketReceiveUnk, 2, true, false },
        { EventId::ServerPacketSend, "server.on_packet_send", g_argsServerPacketSend, 2, true, false },
        { EventId::ServerOpenStateChange, "server.on_open_state_change", g_argsServerOpenStateChange, 1, false, false },
        { EventId::ServerConfigLoad, "server.on_config_load", g_argsServerConfigLoad, 1, false, false },
        { EventId::ServerShutdownInit, "server.on_shutdown_init", g_argsServerShutdownInit, 2, false, false },
        { EventId::ServerShutdownCancel, "server.on_shutdown_cancel", g_argsServerShutdownCancel, 1, false, false },
        { EventId::ServerWorldUpdate, "server.on_world_update", g_argsServerWorldUpdate, 1, false, false },
        { EventId::ServerWorldStartup, "server.on_world_startup", g_argsServerWorldStartup, 1, false, false },
        { EventId::ServerWorldShutdown, "server.on_world_shutdown", g_argsServerWorldShutdown, 1, false, false },
        { EventId::ServerLuaStateClose, "server.on_lua_state_close", g_argsServerLuaStateClose, 1, false, false },
        { EventId::ServerMapCreate, "server.on_map_create", g_argsServerMapCreate, 1, false, false },
        { EventId::ServerMapDestroy, "server.on_map_destroy", g_argsServerMapDestroy, 1, false, false },
        { EventId::ServerMapGridLoad, "server.on_map_grid_load", g_argsServerMapGridLoad, 1, false, false },
        { EventId::ServerMapGridUnload, "server.on_map_grid_unload", g_argsServerMapGridUnload, 1, false, false },
        { EventId::ServerMapPlayerEnter, "server.on_map_player_enter", g_argsServerMapPlayerEnter, 2, false, false },
        { EventId::ServerMapPlayerLeave, "server.on_map_player_leave", g_argsServerMapPlayerLeave, 2, false, false },
        { EventId::ServerMapUpdate, "server.on_map_update", g_argsServerMapUpdate, 2, false, false },
        { EventId::ServerEventTrigger, "server.on_event_trigger", g_argsServerEventTrigger, 2, false, true },
        { EventId::ServerWeatherChange, "server.on_weather_change", g_argsServerWeatherChange, 4, false, false },
        { EventId::ServerAuctionAdd, "server.on_auction_add", g_argsServerAuctionAdd, 2, false, false },
        { EventId::ServerAuctionRemove, "server.on_auction_remove", g_argsServerAuctionRemove, 2, false, false },
        { EventId::ServerAuctionSuccessful, "server.on_auction_successful", g_argsServerAuctionSuccessful, 2, false, false },
        { EventId::ServerAuctionExpire, "server.on_auction_expire", g_argsServerAuctionExpire, 2, false, false },
        { EventId::ServerAddonMessage, "server.on_addon_message", g_argsServerAddonMessage, 7, true, false },
        { EventId::ServerWorldDeleteCreature, "server.on_world_delete_creature", g_argsServerWorldDeleteCreature, 1, false, false },
        { EventId::ServerWorldDeleteGameobject, "server.on_world_delete_gameobject", g_argsServerWorldDeleteGameobject, 1, false, false },
        { EventId::ServerLuaStateOpen, "server.on_lua_state_open", g_argsServerLuaStateOpen, 1, false, false },
        { EventId::ServerGameStart, "server.on_game_start", g_argsServerGameStart, 1, false, false },
        { EventId::ServerGameStop, "server.on_game_stop", g_argsServerGameStop, 1, false, false },
        { EventId::ServerEventRaised, "server.on_event_raised", g_argsServerEventRaised, 4, false, true },
        { EventId::PlayerCharacterCreate, "player.on_character_create", g_argsPlayerCharacterCreate, 1, false, false },
        { EventId::PlayerCharacterDelete, "player.on_character_delete", g_argsPlayerCharacterDelete, 1, false, false },
        { EventId::PlayerLogin, "player.on_login", g_argsPlayerLogin, 1, false, false },
        { EventId::PlayerLogout, "player.on_logout", g_argsPlayerLogout, 1, false, false },
        { EventId::PlayerSpellCast, "player.on_spell_cast", g_argsPlayerSpellCast, 3, false, false },
        { EventId::PlayerKillPlayer, "player.on_kill_player", g_argsPlayerKillPlayer, 2, false, false },
        { EventId::PlayerKillCreature, "player.on_kill_creature", g_argsPlayerKillCreature, 2, false, false },
        { EventId::PlayerKilledByCreature, "player.on_killed_by_creature", g_argsPlayerKilledByCreature, 2, false, false },
        { EventId::PlayerDuelRequest, "player.on_duel_request", g_argsPlayerDuelRequest, 2, false, false },
        { EventId::PlayerDuelStart, "player.on_duel_start", g_argsPlayerDuelStart, 2, false, false },
        { EventId::PlayerDuelEnd, "player.on_duel_end", g_argsPlayerDuelEnd, 3, false, false },
        { EventId::PlayerGiveXp, "player.on_give_xp", g_argsPlayerGiveXp, 3, false, false },
        { EventId::PlayerLevelChange, "player.on_level_change", g_argsPlayerLevelChange, 2, false, false },
        { EventId::PlayerMoneyChange, "player.on_money_change", g_argsPlayerMoneyChange, 2, false, false },
        { EventId::PlayerReputationChange, "player.on_reputation_change", g_argsPlayerReputationChange, 4, false, false },
        { EventId::PlayerTalentsChange, "player.on_talents_change", g_argsPlayerTalentsChange, 2, false, false },
        { EventId::PlayerTalentsReset, "player.on_talents_reset", g_argsPlayerTalentsReset, 2, false, false },
        { EventId::PlayerChat, "player.on_chat", g_argsPlayerChat, 4, true, false },
        { EventId::PlayerWhisper, "player.on_whisper", g_argsPlayerWhisper, 5, true, false },
        { EventId::PlayerGroupChat, "player.on_group_chat", g_argsPlayerGroupChat, 5, true, false },
        { EventId::PlayerGuildChat, "player.on_guild_chat", g_argsPlayerGuildChat, 5, true, false },
        { EventId::PlayerChannelChat, "player.on_channel_chat", g_argsPlayerChannelChat, 5, true, false },
        { EventId::PlayerEmote, "player.on_emote", g_argsPlayerEmote, 2, false, false },
        { EventId::PlayerTextEmote, "player.on_text_emote", g_argsPlayerTextEmote, 4, false, false },
        { EventId::PlayerSave, "player.on_save", g_argsPlayerSave, 1, false, false },
        { EventId::PlayerBindToInstance, "player.on_bind_to_instance", g_argsPlayerBindToInstance, 4, false, false },
        { EventId::PlayerUpdateZone, "player.on_update_zone", g_argsPlayerUpdateZone, 3, false, false },
        { EventId::PlayerMapChange, "player.on_map_change", g_argsPlayerMapChange, 1, false, false },
        { EventId::PlayerEquip, "player.on_equip", g_argsPlayerEquip, 4, false, false },
        { EventId::PlayerFirstLogin, "player.on_first_login", g_argsPlayerFirstLogin, 1, false, false },
        { EventId::PlayerCanUseItem, "player.on_can_use_item", g_argsPlayerCanUseItem, 3, false, false },
        { EventId::PlayerLootItem, "player.on_loot_item", g_argsPlayerLootItem, 4, false, false },
        { EventId::PlayerEnterCombat, "player.on_enter_combat", g_argsPlayerEnterCombat, 2, false, false },
        { EventId::PlayerLeaveCombat, "player.on_leave_combat", g_argsPlayerLeaveCombat, 1, false, false },
        { EventId::PlayerRepop, "player.on_repop", g_argsPlayerRepop, 1, false, false },
        { EventId::PlayerResurrect, "player.on_resurrect", g_argsPlayerResurrect, 1, false, false },
        { EventId::PlayerLootMoney, "player.on_loot_money", g_argsPlayerLootMoney, 2, false, false },
        { EventId::PlayerQuestAbandon, "player.on_quest_abandon", g_argsPlayerQuestAbandon, 2, false, false },
        { EventId::PlayerLearnTalents, "player.on_learn_talents", g_argsPlayerLearnTalents, 4, false, false },
        { EventId::PlayerEnvironmentalDeath, "player.on_environmental_death", g_argsPlayerEnvironmentalDeath, 2, false, false },
        { EventId::PlayerTradeAccept, "player.on_trade_accept", g_argsPlayerTradeAccept, 2, true, false },
        { EventId::PlayerCommand, "player.on_command", g_argsPlayerCommand, 2, true, false },
        { EventId::PlayerSkillChange, "player.on_skill_change", g_argsPlayerSkillChange, 3, false, false },
        { EventId::PlayerLearnSpell, "player.on_learn_spell", g_argsPlayerLearnSpell, 2, false, false },
        { EventId::PlayerAchievementComplete, "player.on_achievement_complete", g_argsPlayerAchievementComplete, 2, false, false },
        { EventId::PlayerDiscoverArea, "player.on_discover_area", g_argsPlayerDiscoverArea, 2, false, false },
        { EventId::PlayerUpdateArea, "player.on_update_area", g_argsPlayerUpdateArea, 3, false, false },
        { EventId::PlayerTradeInit, "player.on_trade_init", g_argsPlayerTradeInit, 2, true, false },
        { EventId::PlayerSendMail, "player.on_send_mail", g_argsPlayerSendMail, 2, true, false },
        { EventId::PlayerQuestStatusChanged, "player.on_quest_status_changed", g_argsPlayerQuestStatusChanged, 3, false, false },
        { EventId::PlayerQuestStart, "player.on_quest_start", g_argsPlayerQuestStart, 3, false, false },
        { EventId::PlayerQuestEnd, "player.on_quest_end", g_argsPlayerQuestEnd, 3, false, false },
        { EventId::GuildAddMember, "guild.on_add_member", g_argsGuildAddMember, 3, false, false },
        { EventId::GuildRemoveMember, "guild.on_remove_member", g_argsGuildRemoveMember, 3, false, false },
        { EventId::GuildMotdChange, "guild.on_motd_change", g_argsGuildMotdChange, 2, false, false },
        { EventId::GuildInfoChange, "guild.on_info_change", g_argsGuildInfoChange, 2, false, false },
        { EventId::GuildCreate, "guild.on_create", g_argsGuildCreate, 3, false, false },
        { EventId::GuildDisband, "guild.on_disband", g_argsGuildDisband, 1, false, false },
        { EventId::GuildMoneyWithdraw, "guild.on_money_withdraw", g_argsGuildMoneyWithdraw, 4, false, false },
        { EventId::GuildMoneyDeposit, "guild.on_money_deposit", g_argsGuildMoneyDeposit, 3, false, false },
        { EventId::GuildItemMove, "guild.on_item_move", g_argsGuildItemMove, 9, false, false },
        { EventId::GuildEvent, "guild.on_event", g_argsGuildEvent, 5, false, false },
        { EventId::GuildBankEvent, "guild.on_bank_event", g_argsGuildBankEvent, 7, false, false },
        { EventId::GroupAddMember, "group.on_add_member", g_argsGroupAddMember, 2, false, false },
        { EventId::GroupInviteMember, "group.on_invite_member", g_argsGroupInviteMember, 2, false, false },
        { EventId::GroupRemoveMember, "group.on_remove_member", g_argsGroupRemoveMember, 3, false, false },
        { EventId::GroupLeaderChange, "group.on_leader_change", g_argsGroupLeaderChange, 3, false, false },
        { EventId::GroupDisband, "group.on_disband", g_argsGroupDisband, 1, false, false },
        { EventId::GroupCreate, "group.on_create", g_argsGroupCreate, 3, false, false },
        { EventId::GroupMemberAccept, "group.on_member_accept", g_argsGroupMemberAccept, 2, true, false },
        { EventId::VehicleInstall, "vehicle.on_install", g_argsVehicleInstall, 1, false, false },
        { EventId::VehicleUninstall, "vehicle.on_uninstall", g_argsVehicleUninstall, 1, false, false },
        { EventId::VehicleInstallAccessory, "vehicle.on_install_accessory", g_argsVehicleInstallAccessory, 2, false, false },
        { EventId::VehicleAddPassenger, "vehicle.on_add_passenger", g_argsVehicleAddPassenger, 3, false, false },
        { EventId::VehicleRemovePassenger, "vehicle.on_remove_passenger", g_argsVehicleRemovePassenger, 2, false, false },
        { EventId::CreatureEnterCombat, "creature.on_enter_combat", g_argsCreatureEnterCombat, 2, false, false },
        { EventId::CreatureLeaveCombat, "creature.on_leave_combat", g_argsCreatureLeaveCombat, 1, false, false },
        { EventId::CreatureTargetDied, "creature.on_target_died", g_argsCreatureTargetDied, 2, false, false },
        { EventId::CreatureDied, "creature.on_died", g_argsCreatureDied, 2, false, false },
        { EventId::CreatureSpawn, "creature.on_spawn", g_argsCreatureSpawn, 1, false, false },
        { EventId::CreatureReachWp, "creature.on_reach_wp", g_argsCreatureReachWp, 4, false, false },
        { EventId::CreatureReceiveEmote, "creature.on_receive_emote", g_argsCreatureReceiveEmote, 3, false, false },
        { EventId::CreatureDamageTaken, "creature.on_damage_taken", g_argsCreatureDamageTaken, 3, false, false },
        { EventId::CreaturePreCombat, "creature.on_pre_combat", g_argsCreaturePreCombat, 2, false, false },
        { EventId::CreatureOwnerAttacked, "creature.on_owner_attacked", g_argsCreatureOwnerAttacked, 2, false, false },
        { EventId::CreatureOwnerAttackedAt, "creature.on_owner_attacked_at", g_argsCreatureOwnerAttackedAt, 2, false, false },
        { EventId::CreatureHitBySpell, "creature.on_hit_by_spell", g_argsCreatureHitBySpell, 3, false, false },
        { EventId::CreatureSpellHitTarget, "creature.on_spell_hit_target", g_argsCreatureSpellHitTarget, 3, false, false },
        { EventId::CreatureJustSummonedCreature, "creature.on_just_summoned_creature", g_argsCreatureJustSummonedCreature, 2, false, false },
        { EventId::CreatureSummonedCreatureDespawn, "creature.on_summoned_creature_despawn", g_argsCreatureSummonedCreatureDespawn, 2, false, false },
        { EventId::CreatureSummonedCreatureDied, "creature.on_summoned_creature_died", g_argsCreatureSummonedCreatureDied, 3, false, false },
        { EventId::CreatureSummoned, "creature.on_summoned", g_argsCreatureSummoned, 2, true, false },
        { EventId::CreatureReset, "creature.on_reset", g_argsCreatureReset, 1, false, false },
        { EventId::CreatureReachHome, "creature.on_reach_home", g_argsCreatureReachHome, 1, false, false },
        { EventId::CreatureCorpseRemoved, "creature.on_corpse_removed", g_argsCreatureCorpseRemoved, 2, false, false },
        { EventId::CreatureMoveInLos, "creature.on_move_in_los", g_argsCreatureMoveInLos, 2, false, false },
        { EventId::CreatureDummyEffect, "creature.on_dummy_effect", g_argsCreatureDummyEffect, 4, false, true },
        { EventId::CreatureQuestAccept, "creature.on_quest_accept", g_argsCreatureQuestAccept, 3, false, true },
        { EventId::CreatureQuestReward, "creature.on_quest_reward", g_argsCreatureQuestReward, 4, false, true },
        { EventId::CreatureDialogStatus, "creature.on_dialog_status", g_argsCreatureDialogStatus, 3, false, true },
        { EventId::CreatureAdd, "creature.on_add", g_argsCreatureAdd, 1, false, false },
        { EventId::CreatureRemove, "creature.on_remove", g_argsCreatureRemove, 1, true, false },
        { EventId::GameobjectSpawn, "gameobject.on_spawn", g_argsGameobjectSpawn, 1, false, false },
        { EventId::GameobjectDummyEffect, "gameobject.on_dummy_effect", g_argsGameobjectDummyEffect, 4, false, true },
        { EventId::GameobjectQuestAccept, "gameobject.on_quest_accept", g_argsGameobjectQuestAccept, 3, false, true },
        { EventId::GameobjectQuestReward, "gameobject.on_quest_reward", g_argsGameobjectQuestReward, 4, false, true },
        { EventId::GameobjectDialogStatus, "gameobject.on_dialog_status", g_argsGameobjectDialogStatus, 3, false, true },
        { EventId::GameobjectDestroyed, "gameobject.on_destroyed", g_argsGameobjectDestroyed, 2, false, false },
        { EventId::GameobjectDamaged, "gameobject.on_damaged", g_argsGameobjectDamaged, 2, false, false },
        { EventId::GameobjectLootStateChange, "gameobject.on_loot_state_change", g_argsGameobjectLootStateChange, 2, false, false },
        { EventId::GameobjectGoStateChanged, "gameobject.on_go_state_changed", g_argsGameobjectGoStateChanged, 2, false, false },
        { EventId::GameobjectAdd, "gameobject.on_add", g_argsGameobjectAdd, 1, false, false },
        { EventId::GameobjectRemove, "gameobject.on_remove", g_argsGameobjectRemove, 1, true, false },
        { EventId::GameobjectUse, "gameobject.on_use", g_argsGameobjectUse, 2, false, true },
        { EventId::GameobjectActivate, "gameobject.on_activate", g_argsGameobjectActivate, 2, false, false },
        { EventId::GameobjectTrapSprung, "gameobject.on_trap_sprung", g_argsGameobjectTrapSprung, 2, false, false },
        { EventId::SpellCast, "spell.on_cast", g_argsSpellCast, 2, false, false },
        { EventId::SpellAuraApplication, "spell.on_aura_application", g_argsSpellAuraApplication, 5, true, false },
        { EventId::SpellDispel, "spell.on_dispel", g_argsSpellDispel, 2, false, false },
        { EventId::SpellPeriodicTick, "spell.on_periodic_tick", g_argsSpellPeriodicTick, 3, true, false },
        { EventId::SpellPeriodicUpdate, "spell.on_periodic_update", g_argsSpellPeriodicUpdate, 2, false, false },
        { EventId::SpellAuraCalcAmount, "spell.on_aura_calc_amount", g_argsSpellAuraCalcAmount, 4, false, false },
        { EventId::SpellCalcPeriodic, "spell.on_calc_periodic", g_argsSpellCalcPeriodic, 4, false, false },
        { EventId::SpellCheckProc, "spell.on_check_proc", g_argsSpellCheckProc, 2, true, false },
        { EventId::SpellProc, "spell.on_proc", g_argsSpellProc, 2, true, false },
        { EventId::SpellCheckCast, "spell.on_check_cast", g_argsSpellCheckCast, 1, false, false },
        { EventId::SpellBeforeCast, "spell.on_before_cast", g_argsSpellBeforeCast, 1, false, false },
        { EventId::SpellAfterCast, "spell.on_after_cast", g_argsSpellAfterCast, 1, false, false },
        { EventId::SpellObjectAreaTarget, "spell.on_object_area_target", g_argsSpellObjectAreaTarget, 3, false, false },
        { EventId::SpellObjectTarget, "spell.on_object_target", g_argsSpellObjectTarget, 3, false, false },
        { EventId::SpellDestTarget, "spell.on_dest_target", g_argsSpellDestTarget, 3, false, false },
        { EventId::SpellEffectLaunch, "spell.on_effect_launch", g_argsSpellEffectLaunch, 4, true, false },
        { EventId::SpellEffectLaunchTarget, "spell.on_effect_launch_target", g_argsSpellEffectLaunchTarget, 4, true, false },
        { EventId::SpellEffectCalcAbsorb, "spell.on_effect_calc_absorb", g_argsSpellEffectCalcAbsorb, 4, false, false },
        { EventId::SpellEffectHit, "spell.on_effect_hit", g_argsSpellEffectHit, 3, false, true },
        { EventId::SpellBeforeHit, "spell.on_before_hit", g_argsSpellBeforeHit, 2, false, false },
        { EventId::SpellEffectHitTarget, "spell.on_effect_hit_target", g_argsSpellEffectHitTarget, 4, true, false },
        { EventId::SpellHit, "spell.on_hit", g_argsSpellHit, 1, false, false },
        { EventId::SpellAfterHit, "spell.on_after_hit", g_argsSpellAfterHit, 1, false, false },
        { EventId::ItemDummyEffect, "item.on_dummy_effect", g_argsItemDummyEffect, 4, false, true },
        { EventId::ItemUse, "item.on_use", g_argsItemUse, 3, true, false },
        { EventId::ItemQuestAccept, "item.on_quest_accept", g_argsItemQuestAccept, 3, false, true },
        { EventId::ItemExpire, "item.on_expire", g_argsItemExpire, 2, true, false },
        { EventId::ItemRemove, "item.on_remove", g_argsItemRemove, 2, true, false },
        { EventId::ItemAdd, "item.on_add", g_argsItemAdd, 2, false, false },
        { EventId::ItemEquip, "item.on_equip", g_argsItemEquip, 4, false, false },
        { EventId::ItemUnequip, "item.on_unequip", g_argsItemUnequip, 3, false, false },
        { EventId::BgStart, "bg.on_start", g_argsBgStart, 3, false, false },
        { EventId::BgEnd, "bg.on_end", g_argsBgEnd, 4, false, false },
        { EventId::BgCreate, "bg.on_create", g_argsBgCreate, 3, false, false },
        { EventId::BgPreDestroy, "bg.on_pre_destroy", g_argsBgPreDestroy, 3, false, false },
        { EventId::GossipCreatureHello, "gossip.creature_hello", g_argsGossipCreatureHello, 2, false, true },
        { EventId::GossipCreatureSelect, "gossip.creature_select", g_argsGossipCreatureSelect, 5, false, true },
        { EventId::GossipGameobjectHello, "gossip.gameobject_hello", g_argsGossipGameobjectHello, 2, false, true },
        { EventId::GossipGameobjectSelect, "gossip.gameobject_select", g_argsGossipGameobjectSelect, 5, false, true },
        { EventId::GossipItemHello, "gossip.item_hello", g_argsGossipItemHello, 2, false, true },
        { EventId::GossipItemSelect, "gossip.item_select", g_argsGossipItemSelect, 5, false, true },
        { EventId::GossipPlayerMenuSelect, "gossip.player_menu_select", g_argsGossipPlayerMenuSelect, 5, false, true },
        { EventId::GossipActionChosen, "gossip.action_chosen", g_argsGossipActionChosen, 4, false, false },
        { EventId::GossipMenuShown, "gossip.menu_shown", g_argsGossipMenuShown, 4, false, false },
        { EventId::CoreNpcSpellClick, "core.on_npc_spell_click", g_argsCoreNpcSpellClick, 3, false, true },
        { EventId::CoreEffectScriptEffect, "core.on_effect_script_effect", g_argsCoreEffectScriptEffect, 5, false, true },
        { EventId::CoreAuraDummy, "core.on_aura_dummy", g_argsCoreAuraDummy, 2, false, true },
        { EventId::CoreEffectDummy, "core.on_effect_dummy", g_argsCoreEffectDummy, 5, false, true },
    };

    /// The shape of @a id, or nullptr when nothing carries it.
    inline EventSpec const* SpecOf(EventId id)
    {
        for (EventSpec const& spec : g_eventSpecs)
        {
            if (spec.id == id)
            {
                return &spec;
            }
        }

        return nullptr;
    }

}

#endif //MANGOS_SCRIPT_EVENTS_GEN_H
