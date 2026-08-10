-- MAI storage.
--
-- Two tables where the DB scripts had ten, and seventeen columns become five
-- that mean something. What follows is the reasoning; the DDL is at the bottom.
--
--
-- WHAT WAS WRONG WITH THE OLD SHAPE
--
--   dbscripts_on_quest_start, _quest_end, _spell, _go_use, _go_template_use,
--   _creature_death, _creature_movement, _gossip, _event  ... nine tables with
--   identical columns, differing only in what the `id` means. A change to the
--   command set was nine ALTERs, and a tool that read one read none of the
--   others.
--
--   datalong, datalong2, dataint, dataint2, dataint3, dataint4 ... six columns
--   whose meaning depends on `command`, documented in a C++ union nobody
--   editing SQL is looking at. `cast_spell` and `play_sound` have the same
--   shape, so a sound id in a spell slot loads without a word and fails once,
--   at run time, in front of a player.
--
--   delay in SECONDS, added straight to game time. No pause shorter than a
--   second could be written at all.
--
--
-- THE SHAPE HERE
--
--   mai_script   one row per sequence. What it is called and what starts it.
--   mai_step     one row per thing that happens, with its time and its verb.
--
--   A step's parameters are `name=value` pairs in one text column, named by
--   the manifest:
--
--       cast_spell    spell=11962 flags=1
--       talk          text0=-1000123 text1=-1000124
--       temp_summon_creature  entry=2044 despawn_delay=300000
--                             x=-10953.3 y=988.509 z=98.984 o=5.349
--
--   That is worse than a column per parameter for a machine and much better
--   for the person editing it, which is the trade that matters: the machine
--   validates it at load against the manifest -- every name, every type, every
--   id checked against the world -- and says which script, which step and
--   which parameter is wrong. A column per parameter cannot be validated at
--   all beyond its SQL type, and there is no SQL type for "a spell that
--   exists".
--
--   It also means the command set can grow without an ALTER. The manifest is
--   the schema; this table is where the values live.
--
--
-- PSEUDOCODE
--
--   script  := id, kind, name, comment
--   kind    := quest_start | quest_end | spell | go_use | go_template_use
--            | creature_death | creature_movement | gossip | event | internal
--
--   step    := script, at_ms, action, params, buddy, comment
--   at_ms   := milliseconds from the start of the sequence   -- not from the
--                                                               previous step
--   action  := a verb from actions.manifest
--   params  := "name=value name=value ..."   -- names and types from the same
--   buddy   := entry, radius_or_guid, flags  -- who the step really acts on
--
--
-- ORDER. Steps run in at_ms order, and steps sharing an at_ms run in the order
-- their `seq` gives them. Nothing anywhere relies on primary-key order or on
-- the order rows were inserted, because neither is a thing SQL promises.

DROP TABLE IF EXISTS `mai_step`;
DROP TABLE IF EXISTS `mai_script`;

CREATE TABLE `mai_script`
(
    `id`      INT UNSIGNED NOT NULL,
    `kind`    ENUM('quest_start','quest_end','spell','go_use',
                   'go_template_use','creature_death','creature_movement',
                   'gossip','event','internal') NOT NULL,

    -- What the id means depends on the kind: a quest id, a spell id, a
    -- creature entry, a gameobject guid. It was the same before; what is new
    -- is that the kind is a column rather than a table name.
    `name`    VARCHAR(128) NOT NULL DEFAULT '',
    `comment` VARCHAR(255) NOT NULL DEFAULT '',

    PRIMARY KEY (`kind`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='MAI: one row per sequence';

CREATE TABLE `mai_step`
(
    `kind`     ENUM('quest_start','quest_end','spell','go_use',
                    'go_template_use','creature_death','creature_movement',
                    'gossip','event','internal') NOT NULL,
    `script`   INT UNSIGNED NOT NULL,

    -- Ties are broken by this, so two steps at the same instant have a defined
    -- order instead of whichever one the storage engine hands back first.
    `seq`      SMALLINT UNSIGNED NOT NULL DEFAULT 0,

    `at_ms`    INT UNSIGNED NOT NULL DEFAULT 0,
    `action`   VARCHAR(48) NOT NULL,
    `params`   VARCHAR(512) NOT NULL DEFAULT '',

    -- Not part of `params` because it modifies the step rather than being an
    -- argument of the verb: any action at all may be redirected at a creature
    -- found nearby. Declared as a parameter it would have been repeated on
    -- forty-seven verbs and still been wrong about what it changes.
    `buddy_entry` INT UNSIGNED NOT NULL DEFAULT 0,
    `buddy_range` INT UNSIGNED NOT NULL DEFAULT 0,
    `buddy_flags` TINYINT UNSIGNED NOT NULL DEFAULT 0,

    `comment`  VARCHAR(255) NOT NULL DEFAULT '',

    PRIMARY KEY (`kind`, `script`, `seq`),
    KEY `by_time` (`kind`, `script`, `at_ms`),
    CONSTRAINT `mai_step_belongs_to_a_script`
        FOREIGN KEY (`kind`, `script`) REFERENCES `mai_script` (`kind`, `id`)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='MAI: one row per action';
