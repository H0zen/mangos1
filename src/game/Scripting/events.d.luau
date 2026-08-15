--!strict
-- GENERATED FROM events.manifest -- DO NOT EDIT.
-- Regenerate with: python src/game/Scripting/tools/gen_events.py

-- A guid is 64 bits and a Lua number holds 53, so one crosses as
-- an opaque box: comparable and printable, never rounded.
export type Guid = userdata
export type Handle = userdata

-- Valid only while the call that lent it is on the stack. Ask.
export type Borrow = { IsLive: (Borrow) -> boolean }

-- packet.on_receive (cancel)
export type PacketReceive = { player: Guid, packet: Borrow }
-- packet.on_receive_unk (cancel)
export type PacketReceiveUnk = { player: Guid, packet: Borrow }
-- packet.on_send (cancel)
export type PacketSend = { player: Guid, packet: Borrow }
-- server.on_network_start (broadcast)
export type ServerNetworkStart = { subject: Guid }
-- server.on_network_stop (broadcast)
export type ServerNetworkStop = { subject: Guid }
-- server.on_socket_open (broadcast)
export type ServerSocketOpen = { subject: Guid }
-- server.on_socket_close (broadcast)
export type ServerSocketClose = { subject: Guid }
-- server.on_packet_receive (cancel)
export type ServerPacketReceive = { session: Borrow, packet: Borrow }
-- server.on_packet_receive_unk (cancel)
export type ServerPacketReceiveUnk = { player: Guid, packet: Borrow }
-- server.on_packet_send (cancel)
export type ServerPacketSend = { session: Borrow, packet: Borrow }
-- server.on_open_state_change (broadcast)
export type ServerOpenStateChange = { open: boolean }
-- server.on_config_load (broadcast)
export type ServerConfigLoad = { reload: boolean }
-- server.on_shutdown_init (broadcast)
export type ServerShutdownInit = { code: number, mask: number }
-- server.on_shutdown_cancel (broadcast)
export type ServerShutdownCancel = { subject: Guid }
-- server.on_world_update (broadcast)
export type ServerWorldUpdate = { diff: number }
-- server.on_world_startup (broadcast)
export type ServerWorldStartup = { subject: Guid }
-- server.on_world_shutdown (broadcast)
export type ServerWorldShutdown = { subject: Guid }
-- server.on_lua_state_close (broadcast)
export type ServerLuaStateClose = { subject: Guid }
-- server.on_map_create (broadcast)
export type ServerMapCreate = { map: Handle }
-- server.on_map_destroy (broadcast)
export type ServerMapDestroy = { map: Handle }
-- server.on_map_grid_load (broadcast)
export type ServerMapGridLoad = { subject: Guid }
-- server.on_map_grid_unload (broadcast)
export type ServerMapGridUnload = { subject: Guid }
-- server.on_map_player_enter (broadcast)
export type ServerMapPlayerEnter = { map: Handle, player: Guid }
-- server.on_map_player_leave (broadcast)
export type ServerMapPlayerLeave = { map: Handle, player: Guid }
-- server.on_map_update (broadcast)
export type ServerMapUpdate = { map: Handle, diff: number }
-- server.on_event_trigger (claim)
export type ServerEventTrigger = { player: Guid, trigger: Handle }
-- server.on_weather_change (broadcast)
export type ServerWeatherChange = { weather: Handle, zone: number, state: number, grade: number }
-- server.on_auction_add (broadcast)
export type ServerAuctionAdd = { house: Borrow, entry: Handle }
-- server.on_auction_remove (broadcast)
export type ServerAuctionRemove = { house: Borrow, entry: Handle }
-- server.on_auction_successful (broadcast)
export type ServerAuctionSuccessful = { house: Borrow, entry: Handle }
-- server.on_auction_expire (broadcast)
export type ServerAuctionExpire = { house: Borrow, entry: Handle }
-- server.on_addon_message (cancel)
export type ServerAddonMessage = { sender: Guid, type: number, msg: string, receiver: Guid, guild: Handle, group: Handle, channel: Borrow }
-- server.on_world_delete_creature (broadcast)
export type ServerWorldDeleteCreature = { creature: Guid }
-- server.on_world_delete_gameobject (broadcast)
export type ServerWorldDeleteGameobject = { gameobject: Guid }
-- server.on_lua_state_open (broadcast)
export type ServerLuaStateOpen = { subject: Guid }
-- server.on_game_start (broadcast)
export type ServerGameStart = { eventId: number }
-- server.on_game_stop (broadcast)
export type ServerGameStop = { eventId: number }
-- server.on_event_raised (claim)
export type ServerEventRaised = { source: Guid, target: Guid, eventId: number, isStart: boolean }
-- player.on_character_create (broadcast)
export type PlayerCharacterCreate = { player: Guid }
-- player.on_character_delete (broadcast)
export type PlayerCharacterDelete = { guid: number }
-- player.on_login (broadcast)
export type PlayerLogin = { player: Guid }
-- player.on_logout (broadcast)
export type PlayerLogout = { player: Guid }
-- player.on_spell_cast (broadcast)
export type PlayerSpellCast = { player: Guid, spell: Borrow, skipCheck: boolean }
-- player.on_kill_player (broadcast)
export type PlayerKillPlayer = { killer: Guid, killed: Guid }
-- player.on_kill_creature (broadcast)
export type PlayerKillCreature = { killer: Guid, killed: Guid }
-- player.on_killed_by_creature (broadcast)
export type PlayerKilledByCreature = { killer: Guid, killed: Guid }
-- player.on_duel_request (broadcast)
export type PlayerDuelRequest = { target: Guid, challenger: Guid }
-- player.on_duel_start (broadcast)
export type PlayerDuelStart = { starter: Guid, challenger: Guid }
-- player.on_duel_end (broadcast)
export type PlayerDuelEnd = { winner: Guid, loser: Guid, type: number }
-- player.on_give_xp (broadcast)
export type PlayerGiveXp = { player: Guid, amount: number, victim: Guid }
-- player.on_level_change (broadcast)
export type PlayerLevelChange = { player: Guid, oldLevel: number }
-- player.on_money_change (broadcast)
export type PlayerMoneyChange = { player: Guid, amount: number }
-- player.on_reputation_change (broadcast)
export type PlayerReputationChange = { player: Guid, factionID: number, standing: number, incremental: boolean }
-- player.on_talents_change (broadcast)
export type PlayerTalentsChange = { player: Guid, newPoints: number }
-- player.on_talents_reset (broadcast)
export type PlayerTalentsReset = { player: Guid, noCost: boolean }
-- player.on_chat (cancel)
export type PlayerChat = { player: Guid, type: number, lang: number, msg: string }
-- player.on_whisper (cancel)
export type PlayerWhisper = { player: Guid, type: number, lang: number, msg: string, receiver: Guid }
-- player.on_group_chat (cancel)
export type PlayerGroupChat = { player: Guid, type: number, lang: number, msg: string, group: Handle }
-- player.on_guild_chat (cancel)
export type PlayerGuildChat = { player: Guid, type: number, lang: number, msg: string, guild: Handle }
-- player.on_channel_chat (cancel)
export type PlayerChannelChat = { player: Guid, type: number, lang: number, msg: string, channel: Borrow }
-- player.on_emote (broadcast)
export type PlayerEmote = { player: Guid, emote: number }
-- player.on_text_emote (broadcast)
export type PlayerTextEmote = { player: Guid, textEmote: number, emoteNum: number, guid: Guid }
-- player.on_save (broadcast)
export type PlayerSave = { player: Guid }
-- player.on_bind_to_instance (broadcast)
export type PlayerBindToInstance = { player: Guid, difficulty: number, mapid: number, permanent: boolean }
-- player.on_update_zone (broadcast)
export type PlayerUpdateZone = { player: Guid, newZone: number, newArea: number }
-- player.on_map_change (broadcast)
export type PlayerMapChange = { player: Guid }
-- player.on_equip (broadcast)
export type PlayerEquip = { player: Guid, item: Guid, bag: number, slot: number }
-- player.on_first_login (broadcast)
export type PlayerFirstLogin = { player: Guid }
-- player.on_can_use_item (broadcast)
export type PlayerCanUseItem = { player: Guid, itemEntry: number, result: number }
-- player.on_loot_item (broadcast)
export type PlayerLootItem = { player: Guid, item: Guid, count: number, guid: Guid }
-- player.on_enter_combat (broadcast)
export type PlayerEnterCombat = { player: Guid, enemy: Guid }
-- player.on_leave_combat (broadcast)
export type PlayerLeaveCombat = { player: Guid }
-- player.on_repop (broadcast)
export type PlayerRepop = { player: Guid }
-- player.on_resurrect (broadcast)
export type PlayerResurrect = { player: Guid }
-- player.on_loot_money (broadcast)
export type PlayerLootMoney = { player: Guid, amount: number }
-- player.on_quest_abandon (broadcast)
export type PlayerQuestAbandon = { player: Guid, questId: number }
-- player.on_learn_talents (broadcast)
export type PlayerLearnTalents = { player: Guid, talentId: number, talentRank: number, spellid: number }
-- player.on_environmental_death (broadcast)
export type PlayerEnvironmentalDeath = { killed: Guid, damageType: number }
-- player.on_trade_accept (cancel)
export type PlayerTradeAccept = { trader: Guid, tradee: Guid }
-- player.on_command (cancel)
export type PlayerCommand = { player: Guid, text: string }
-- player.on_skill_change (broadcast)
export type PlayerSkillChange = { player: Guid, skillId: number, skillValue: number }
-- player.on_learn_spell (broadcast)
export type PlayerLearnSpell = { player: Guid, spellid: number }
-- player.on_achievement_complete (broadcast)
export type PlayerAchievementComplete = { player: Guid, achievementId: number }
-- player.on_discover_area (broadcast)
export type PlayerDiscoverArea = { player: Guid, area: number }
-- player.on_update_area (broadcast)
export type PlayerUpdateArea = { player: Guid, oldArea: number, newArea: number }
-- player.on_trade_init (cancel)
export type PlayerTradeInit = { trader: Guid, tradee: Guid }
-- player.on_send_mail (cancel)
export type PlayerSendMail = { sender: Guid, recipientGuid: Guid }
-- player.on_quest_status_changed (broadcast)
export type PlayerQuestStatusChanged = { player: Guid, questId: number, status: number }
-- player.on_quest_start (broadcast)
export type PlayerQuestStart = { player: Guid, questGiver: Guid, quest: Handle }
-- player.on_quest_end (broadcast)
export type PlayerQuestEnd = { player: Guid, questGiver: Guid, quest: Handle }
-- guild.on_add_member (broadcast)
export type GuildAddMember = { guild: Handle, player: Guid, plRank: number }
-- guild.on_remove_member (broadcast)
export type GuildRemoveMember = { guild: Handle, player: Guid, isDisbanding: boolean }
-- guild.on_motd_change (broadcast)
export type GuildMotdChange = { guild: Handle, motd: string }
-- guild.on_info_change (broadcast)
export type GuildInfoChange = { guild: Handle, info: string }
-- guild.on_create (broadcast)
export type GuildCreate = { guild: Handle, leader: Guid, name: string }
-- guild.on_disband (broadcast)
export type GuildDisband = { guild: Handle }
-- guild.on_money_withdraw (broadcast)
export type GuildMoneyWithdraw = { guild: Handle, player: Guid, amount: number, isRepair: boolean }
-- guild.on_money_deposit (broadcast)
export type GuildMoneyDeposit = { guild: Handle, player: Guid, amount: number }
-- guild.on_item_move (broadcast)
export type GuildItemMove = { guild: Handle, player: Guid, item: Guid, isSrcBank: boolean, srcContainer: number, srcSlotId: number, isDestBank: boolean, destContainer: number, destSlotId: number }
-- guild.on_event (broadcast)
export type GuildEvent = { guild: Handle, eventType: number, playerGuid1: number, playerGuid2: number, newRank: number }
-- guild.on_bank_event (broadcast)
export type GuildBankEvent = { guild: Handle, eventType: number, tabId: number, playerGuid: number, itemOrMoney: number, itemStackCount: number, destTabId: number }
-- group.on_add_member (broadcast)
export type GroupAddMember = { group: Handle, guid: Guid }
-- group.on_invite_member (broadcast)
export type GroupInviteMember = { group: Handle, guid: Guid }
-- group.on_remove_member (broadcast)
export type GroupRemoveMember = { group: Handle, guid: Guid, method: number }
-- group.on_leader_change (broadcast)
export type GroupLeaderChange = { group: Handle, newLeader: Guid, oldLeader: Guid }
-- group.on_disband (broadcast)
export type GroupDisband = { group: Handle }
-- group.on_create (broadcast)
export type GroupCreate = { group: Handle, leaderGuid: Guid, groupType: number }
-- group.on_member_accept (cancel)
export type GroupMemberAccept = { group: Handle, player: Guid }
-- vehicle.on_install (broadcast)
export type VehicleInstall = { vehicle: number }
-- vehicle.on_uninstall (broadcast)
export type VehicleUninstall = { vehicle: number }
-- vehicle.on_install_accessory (broadcast)
export type VehicleInstallAccessory = { vehicle: number, accessory: Guid }
-- vehicle.on_add_passenger (broadcast)
export type VehicleAddPassenger = { vehicle: number, passenger: Guid, seatId: number }
-- vehicle.on_remove_passenger (broadcast)
export type VehicleRemovePassenger = { vehicle: number, passenger: Guid }
-- creature.on_enter_combat (broadcast)
export type CreatureEnterCombat = { player: Guid, enemy: Guid }
-- creature.on_leave_combat (broadcast)
export type CreatureLeaveCombat = { player: Guid }
-- creature.on_target_died (broadcast)
export type CreatureTargetDied = { creature: Guid, victim: Guid }
-- creature.on_died (broadcast)
export type CreatureDied = { creature: Guid, killer: Guid }
-- creature.on_spawn (broadcast)
export type CreatureSpawn = { gameobject: Guid }
-- creature.on_reach_wp (broadcast)
export type CreatureReachWp = { creature: Guid, pathId: number, pathOrigin: number, nodeIndex: number }
-- creature.on_receive_emote (broadcast)
export type CreatureReceiveEmote = { creature: Guid, player: Guid, emoteId: number }
-- creature.on_damage_taken (broadcast)
export type CreatureDamageTaken = { creature: Guid, attacker: Guid, damage: number }
-- creature.on_pre_combat (broadcast)
export type CreaturePreCombat = { creature: Guid, target: Guid }
-- creature.on_owner_attacked (broadcast)
export type CreatureOwnerAttacked = { creature: Guid, target: Guid }
-- creature.on_owner_attacked_at (broadcast)
export type CreatureOwnerAttackedAt = { creature: Guid, attacker: Guid }
-- creature.on_hit_by_spell (broadcast)
export type CreatureHitBySpell = { creature: Guid, caster: Guid, spell: Handle }
-- creature.on_spell_hit_target (broadcast)
export type CreatureSpellHitTarget = { creature: Guid, target: Guid, spell: Handle }
-- creature.on_just_summoned_creature (broadcast)
export type CreatureJustSummonedCreature = { creature: Guid, summon: Guid }
-- creature.on_summoned_creature_despawn (broadcast)
export type CreatureSummonedCreatureDespawn = { creature: Guid, summon: Guid }
-- creature.on_summoned_creature_died (broadcast)
export type CreatureSummonedCreatureDied = { creature: Guid, summon: Guid, killer: Guid }
-- creature.on_summoned (cancel)
export type CreatureSummoned = { creature: Guid, summoner: Guid }
-- creature.on_reset (broadcast)
export type CreatureReset = { creature: Guid }
-- creature.on_reach_home (broadcast)
export type CreatureReachHome = { creature: Guid }
-- creature.on_corpse_removed (broadcast)
export type CreatureCorpseRemoved = { creature: Guid, respawnDelay: number }
-- creature.on_move_in_los (broadcast)
export type CreatureMoveInLos = { creature: Guid, who: Guid }
-- creature.on_dummy_effect (claim)
export type CreatureDummyEffect = { caster: Guid, spellId: number, effIndex: number, target: Guid }
-- creature.on_quest_accept (claim)
export type CreatureQuestAccept = { player: Guid, creature: Guid, quest: Handle }
-- creature.on_quest_reward (claim)
export type CreatureQuestReward = { player: Guid, creature: Guid, quest: Handle, opt: number }
-- creature.on_dialog_status (claim)
export type CreatureDialogStatus = { player: Guid, creature: Guid, status: number }
-- creature.on_add (broadcast)
export type CreatureAdd = { creature: Guid }
-- creature.on_remove (cancel)
export type CreatureRemove = { creature: Guid }
-- gameobject.on_spawn (broadcast)
export type GameobjectSpawn = { gameobject: Guid }
-- gameobject.on_dummy_effect (claim)
export type GameobjectDummyEffect = { caster: Guid, spellId: number, effIndex: number, target: Guid }
-- gameobject.on_quest_accept (claim)
export type GameobjectQuestAccept = { player: Guid, gameobject: Guid, quest: Handle }
-- gameobject.on_quest_reward (claim)
export type GameobjectQuestReward = { player: Guid, gameobject: Guid, quest: Handle, opt: number }
-- gameobject.on_dialog_status (claim)
export type GameobjectDialogStatus = { player: Guid, gameobject: Guid, status: number }
-- gameobject.on_destroyed (broadcast)
export type GameobjectDestroyed = { gameObject: Guid, attacker: Guid }
-- gameobject.on_damaged (broadcast)
export type GameobjectDamaged = { gameObject: Guid, attacker: Guid }
-- gameobject.on_loot_state_change (broadcast)
export type GameobjectLootStateChange = { gameObject: Guid, state: number }
-- gameobject.on_go_state_changed (broadcast)
export type GameobjectGoStateChanged = { gameObject: Guid, state: number }
-- gameobject.on_add (broadcast)
export type GameobjectAdd = { gameobject: Guid }
-- gameobject.on_remove (cancel)
export type GameobjectRemove = { gameobject: Guid }
-- gameobject.on_use (claim)
export type GameobjectUse = { user: Guid, gameobject: Guid }
-- gameobject.on_activate (broadcast)
export type GameobjectActivate = { user: Guid, gameobject: Guid }
-- gameobject.on_trap_sprung (broadcast)
export type GameobjectTrapSprung = { unit: Guid, gameobject: Guid }
-- spell.on_cast (broadcast)
export type SpellCast = { spell: Borrow, skipCheck: boolean }
-- spell.on_aura_application (cancel)
export type SpellAuraApplication = { aura: Borrow, auraEff: Borrow, target: Guid, mode: number, apply: boolean }
-- spell.on_dispel (broadcast)
export type SpellDispel = { aura: Borrow, dispel: Borrow }
-- spell.on_periodic_tick (cancel)
export type SpellPeriodicTick = { aura: Borrow, auraEff: Borrow, target: Guid }
-- spell.on_periodic_update (broadcast)
export type SpellPeriodicUpdate = { aura: Borrow, auraEff: Borrow }
-- spell.on_aura_calc_amount (broadcast)
export type SpellAuraCalcAmount = { aura: Borrow, auraEff: Borrow, amount: number, canBeRecalculated: boolean }
-- spell.on_calc_periodic (broadcast)
export type SpellCalcPeriodic = { aura: Borrow, auraEff: Borrow, isPeriodic: boolean, amplitude: number }
-- spell.on_check_proc (cancel)
export type SpellCheckProc = { aura: Borrow, proc: Borrow }
-- spell.on_proc (cancel)
export type SpellProc = { aura: Borrow, proc: Borrow }
-- spell.on_check_cast (broadcast)
export type SpellCheckCast = { spell: Borrow }
-- spell.on_before_cast (broadcast)
export type SpellBeforeCast = { spell: Borrow }
-- spell.on_after_cast (broadcast)
export type SpellAfterCast = { spell: Borrow }
-- spell.on_object_area_target (broadcast)
export type SpellObjectAreaTarget = { spell: Borrow, effIndex: number, targets: Borrow }
-- spell.on_object_target (broadcast)
export type SpellObjectTarget = { spell: Borrow, effIndex: number, target: Borrow }
-- spell.on_dest_target (broadcast)
export type SpellDestTarget = { spell: Borrow, effIndex: number, dest: Borrow }
-- spell.on_effect_launch (cancel)
export type SpellEffectLaunch = { spell: Borrow, effIndex: number, mode: number, preventDefault: boolean }
-- spell.on_effect_launch_target (cancel)
export type SpellEffectLaunchTarget = { spell: Borrow, effIndex: number, mode: number, preventDefault: boolean }
-- spell.on_effect_calc_absorb (broadcast)
export type SpellEffectCalcAbsorb = { spell: Borrow, damageInfo: Borrow, resistAmount: number, absorbAmount: number }
-- spell.on_effect_hit (claim)
export type SpellEffectHit = { caster: Guid, target: Guid, spellId: number }
-- spell.on_before_hit (broadcast)
export type SpellBeforeHit = { spell: Borrow, missInfo: number }
-- spell.on_effect_hit_target (cancel)
export type SpellEffectHitTarget = { spell: Borrow, effIndex: number, mode: number, preventDefault: boolean }
-- spell.on_hit (broadcast)
export type SpellHit = { spell: Borrow }
-- spell.on_after_hit (broadcast)
export type SpellAfterHit = { spell: Borrow }
-- item.on_dummy_effect (claim)
export type ItemDummyEffect = { caster: Guid, spellId: number, effIndex: number, target: Guid }
-- item.on_use (cancel)
export type ItemUse = { player: Guid, item: Guid, targets: Borrow }
-- item.on_quest_accept (claim)
export type ItemQuestAccept = { player: Guid, item: Guid, quest: Handle }
-- item.on_expire (cancel)
export type ItemExpire = { player: Guid, proto: Handle }
-- item.on_remove (cancel)
export type ItemRemove = { player: Guid, item: Guid }
-- item.on_add (broadcast)
export type ItemAdd = { player: Guid, item: Guid }
-- item.on_equip (broadcast)
export type ItemEquip = { player: Guid, item: Guid, bag: number, slot: number }
-- item.on_unequip (broadcast)
export type ItemUnequip = { player: Guid, item: Guid, slot: number }
-- bg.on_start (broadcast)
export type BgStart = { bg: Handle, bgId: number, instanceId: number }
-- bg.on_end (broadcast)
export type BgEnd = { bg: Handle, bgId: number, instanceId: number, winner: number }
-- bg.on_create (broadcast)
export type BgCreate = { bg: Handle, bgId: number, instanceId: number }
-- bg.on_pre_destroy (broadcast)
export type BgPreDestroy = { bg: Handle, bgId: number, instanceId: number }
-- gossip.creature_hello (claim)
export type GossipCreatureHello = { player: Guid, creature: Guid }
-- gossip.creature_select (claim)
export type GossipCreatureSelect = { player: Guid, creature: Guid, sender: number, action: number, code: string }
-- gossip.gameobject_hello (claim)
export type GossipGameobjectHello = { player: Guid, gameobject: Guid }
-- gossip.gameobject_select (claim)
export type GossipGameobjectSelect = { player: Guid, gameobject: Guid, sender: number, action: number, code: string }
-- gossip.item_hello (claim)
export type GossipItemHello = { player: Guid, item: Guid }
-- gossip.item_select (claim)
export type GossipItemSelect = { player: Guid, item: Guid, sender: number, action: number, code: string }
-- gossip.player_menu_select (claim)
export type GossipPlayerMenuSelect = { player: Guid, menuId: number, sender: number, action: number, code: string }
-- gossip.action_chosen (broadcast)
export type GossipActionChosen = { player: Guid, source: Guid, menuId: number, gossipListId: number }
-- gossip.menu_shown (broadcast)
export type GossipMenuShown = { player: Guid, source: Guid, menuId: number, textId: number }
-- core.on_npc_spell_click (claim)
export type CoreNpcSpellClick = { player: Guid, creature: Guid, spellId: number }
-- core.on_effect_script_effect (claim)
export type CoreEffectScriptEffect = { caster: Guid, spellId: number, effIndex: number, target: Guid, originalCaster: Guid }
-- core.on_aura_dummy (claim)
export type CoreAuraDummy = { aura: Borrow, apply: boolean }
-- core.on_effect_dummy (claim)
export type CoreEffectDummy = { caster: Guid, spellId: number, effIndex: number, target: Guid, originalCaster: Guid }

declare EVENT: {
    packet_on_receive: number,
    packet_on_receive_unk: number,
    packet_on_send: number,
    server_on_network_start: number,
    server_on_network_stop: number,
    server_on_socket_open: number,
    server_on_socket_close: number,
    server_on_packet_receive: number,
    server_on_packet_receive_unk: number,
    server_on_packet_send: number,
    server_on_open_state_change: number,
    server_on_config_load: number,
    server_on_shutdown_init: number,
    server_on_shutdown_cancel: number,
    server_on_world_update: number,
    server_on_world_startup: number,
    server_on_world_shutdown: number,
    server_on_lua_state_close: number,
    server_on_map_create: number,
    server_on_map_destroy: number,
    server_on_map_grid_load: number,
    server_on_map_grid_unload: number,
    server_on_map_player_enter: number,
    server_on_map_player_leave: number,
    server_on_map_update: number,
    server_on_event_trigger: number,
    server_on_weather_change: number,
    server_on_auction_add: number,
    server_on_auction_remove: number,
    server_on_auction_successful: number,
    server_on_auction_expire: number,
    server_on_addon_message: number,
    server_on_world_delete_creature: number,
    server_on_world_delete_gameobject: number,
    server_on_lua_state_open: number,
    server_on_game_start: number,
    server_on_game_stop: number,
    server_on_event_raised: number,
    player_on_character_create: number,
    player_on_character_delete: number,
    player_on_login: number,
    player_on_logout: number,
    player_on_spell_cast: number,
    player_on_kill_player: number,
    player_on_kill_creature: number,
    player_on_killed_by_creature: number,
    player_on_duel_request: number,
    player_on_duel_start: number,
    player_on_duel_end: number,
    player_on_give_xp: number,
    player_on_level_change: number,
    player_on_money_change: number,
    player_on_reputation_change: number,
    player_on_talents_change: number,
    player_on_talents_reset: number,
    player_on_chat: number,
    player_on_whisper: number,
    player_on_group_chat: number,
    player_on_guild_chat: number,
    player_on_channel_chat: number,
    player_on_emote: number,
    player_on_text_emote: number,
    player_on_save: number,
    player_on_bind_to_instance: number,
    player_on_update_zone: number,
    player_on_map_change: number,
    player_on_equip: number,
    player_on_first_login: number,
    player_on_can_use_item: number,
    player_on_loot_item: number,
    player_on_enter_combat: number,
    player_on_leave_combat: number,
    player_on_repop: number,
    player_on_resurrect: number,
    player_on_loot_money: number,
    player_on_quest_abandon: number,
    player_on_learn_talents: number,
    player_on_environmental_death: number,
    player_on_trade_accept: number,
    player_on_command: number,
    player_on_skill_change: number,
    player_on_learn_spell: number,
    player_on_achievement_complete: number,
    player_on_discover_area: number,
    player_on_update_area: number,
    player_on_trade_init: number,
    player_on_send_mail: number,
    player_on_quest_status_changed: number,
    player_on_quest_start: number,
    player_on_quest_end: number,
    guild_on_add_member: number,
    guild_on_remove_member: number,
    guild_on_motd_change: number,
    guild_on_info_change: number,
    guild_on_create: number,
    guild_on_disband: number,
    guild_on_money_withdraw: number,
    guild_on_money_deposit: number,
    guild_on_item_move: number,
    guild_on_event: number,
    guild_on_bank_event: number,
    group_on_add_member: number,
    group_on_invite_member: number,
    group_on_remove_member: number,
    group_on_leader_change: number,
    group_on_disband: number,
    group_on_create: number,
    group_on_member_accept: number,
    vehicle_on_install: number,
    vehicle_on_uninstall: number,
    vehicle_on_install_accessory: number,
    vehicle_on_add_passenger: number,
    vehicle_on_remove_passenger: number,
    creature_on_enter_combat: number,
    creature_on_leave_combat: number,
    creature_on_target_died: number,
    creature_on_died: number,
    creature_on_spawn: number,
    creature_on_reach_wp: number,
    creature_on_receive_emote: number,
    creature_on_damage_taken: number,
    creature_on_pre_combat: number,
    creature_on_owner_attacked: number,
    creature_on_owner_attacked_at: number,
    creature_on_hit_by_spell: number,
    creature_on_spell_hit_target: number,
    creature_on_just_summoned_creature: number,
    creature_on_summoned_creature_despawn: number,
    creature_on_summoned_creature_died: number,
    creature_on_summoned: number,
    creature_on_reset: number,
    creature_on_reach_home: number,
    creature_on_corpse_removed: number,
    creature_on_move_in_los: number,
    creature_on_dummy_effect: number,
    creature_on_quest_accept: number,
    creature_on_quest_reward: number,
    creature_on_dialog_status: number,
    creature_on_add: number,
    creature_on_remove: number,
    gameobject_on_spawn: number,
    gameobject_on_dummy_effect: number,
    gameobject_on_quest_accept: number,
    gameobject_on_quest_reward: number,
    gameobject_on_dialog_status: number,
    gameobject_on_destroyed: number,
    gameobject_on_damaged: number,
    gameobject_on_loot_state_change: number,
    gameobject_on_go_state_changed: number,
    gameobject_on_add: number,
    gameobject_on_remove: number,
    gameobject_on_use: number,
    gameobject_on_activate: number,
    gameobject_on_trap_sprung: number,
    spell_on_cast: number,
    spell_on_aura_application: number,
    spell_on_dispel: number,
    spell_on_periodic_tick: number,
    spell_on_periodic_update: number,
    spell_on_aura_calc_amount: number,
    spell_on_calc_periodic: number,
    spell_on_check_proc: number,
    spell_on_proc: number,
    spell_on_check_cast: number,
    spell_on_before_cast: number,
    spell_on_after_cast: number,
    spell_on_object_area_target: number,
    spell_on_object_target: number,
    spell_on_dest_target: number,
    spell_on_effect_launch: number,
    spell_on_effect_launch_target: number,
    spell_on_effect_calc_absorb: number,
    spell_on_effect_hit: number,
    spell_on_before_hit: number,
    spell_on_effect_hit_target: number,
    spell_on_hit: number,
    spell_on_after_hit: number,
    item_on_dummy_effect: number,
    item_on_use: number,
    item_on_quest_accept: number,
    item_on_expire: number,
    item_on_remove: number,
    item_on_add: number,
    item_on_equip: number,
    item_on_unequip: number,
    bg_on_start: number,
    bg_on_end: number,
    bg_on_create: number,
    bg_on_pre_destroy: number,
    gossip_creature_hello: number,
    gossip_creature_select: number,
    gossip_gameobject_hello: number,
    gossip_gameobject_select: number,
    gossip_item_hello: number,
    gossip_item_select: number,
    gossip_player_menu_select: number,
    gossip_action_chosen: number,
    gossip_menu_shown: number,
    core_on_npc_spell_click: number,
    core_on_effect_script_effect: number,
    core_on_aura_dummy: number,
    core_on_effect_dummy: number,
}

-- The payload is typed by the event, which one call cannot
-- express; annotate the handler to get the fields checked:
--     OnEvent(EVENT.player_on_login, function(e: PlayerLogin)
declare function OnEvent(id: number,
    handler: (payload: any) -> boolean?): ()
