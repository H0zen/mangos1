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
--       cast_spell    spell=11962 flags=1   -- flags are CAST flags here
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
    -- The last three are MAI's own and have no `dbscripts_on_*` table behind
    -- them: `aura_apply`/`aura_remove` are a dummy aura going on and coming
    -- off, keyed by spell; `branch` is a sequence nothing in the world starts
    -- -- only `start_script` and `random_script` do, which is what lets a
    -- straight line say "one of these".
    --
    -- `item_use` is a player using an item, keyed by item entry, and it is the
    -- only kind that runs INLINE: its answer is whether the item's own spell
    -- may go ahead, and an answer that arrives on the next map tick is not an
    -- answer. `refuse_use` is what says no.
    `kind`    ENUM('quest_start','quest_end','spell','go_use',
                   'go_template_use','creature_death','creature_movement',
                   'gossip','event','internal',
                   'aura_apply','aura_remove','branch','item_use',
                   'areatrigger') NOT NULL,

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
                    'gossip','event','internal',
                    'aura_apply','aura_remove','branch','item_use',
                    'areatrigger') NOT NULL,
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

    -- Out of 100, rolled per step. Not the sequence's roll: `random_script`
    -- is how a script picks ONE of several, and this is how it says "and
    -- sometimes a third as well".
    `chance`   TINYINT UNSIGNED NOT NULL DEFAULT 100,

    `comment`  VARCHAR(255) NOT NULL DEFAULT '',

    PRIMARY KEY (`kind`, `script`, `seq`),
    KEY `by_time` (`kind`, `script`, `at_ms`),
    CONSTRAINT `mai_step_belongs_to_a_script`
        FOREIGN KEY (`kind`, `script`) REFERENCES `mai_script` (`kind`, `id`)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='MAI: one row per action';

-- ---------------------------------------------------------------------------
-- RULES. What a sequence starts FROM.
--
-- A sequence says what happens and when, counted from its own start. A rule
-- says when it starts. That is the whole difference between the two systems
-- folded together here, and each of them spent real effort faking the other:
--
--   EventAI faked sequences with timers. A creature that says one line, waits
--   three seconds and says another is three rows, a phase field and two
--   timers, because there is nowhere to write "then".
--
--   The DB scripts faked rules by having ten tables. `dbscripts_on_quest_end`
--   IS a rule -- "when a quest ends" -- expressed as a table name, and unable
--   to carry a condition or a chance.
--
-- THE THREE ACTION SLOTS ARE GONE. EventAI gave every row exactly three, not
-- because three is a natural number of things to do but because a table needs
-- a fixed width; a creature doing four things on aggro was two rows with the
-- same trigger, the second a fiction told to get more columns. A rule's steps
-- are rows now, and the fourth costs one.
--
-- PSEUDOCODE
--
--   rule  := creature, id, trigger, params, phase_mask, chance, flags
--   step  := creature, rule, seq, action, params
--
-- Phases stay a bitmask, deliberately. They are EventAI's whole notion of
-- state and deserve to become named states -- but not in the same change that
-- moves twenty thousand rows, because a conversion has to be checkable against
-- what it converted and "the same, but better" is not checkable.

DROP TABLE IF EXISTS `mai_rule_step`;
DROP TABLE IF EXISTS `mai_rule`;

CREATE TABLE `mai_rule`
(
    `creature`   INT UNSIGNED NOT NULL,
    `id`         INT UNSIGNED NOT NULL,

    `rule`       VARCHAR(48) NOT NULL,
    `params`     VARCHAR(512) NOT NULL DEFAULT '',

    -- WHETHER, as opposed to WHEN. The trigger says a health threshold was
    -- crossed; the guard says "and we have not enraged yet". Empty on every
    -- rule converted from EventAI, because EventAI had no way to say it.
    --
    --     enraged=0            fires only while that is still zero
    --     kills>=3 phase!=2    all of them must hold
    --
    -- The names are the creature's own and are interned to eight slots when
    -- its rules load. Six comparisons, no `or`, no nesting -- a script that
    -- needs more is a program and belongs in C++.
    `guard`      VARCHAR(255) NOT NULL DEFAULT '',

    -- How soon to come round again when a step's cast was REFUSED: already
    -- casting, silenced, out of range, target immune. Zero -- and every
    -- converted rule is zero -- means the ordinary repeat.
    --
    -- EventAI re-armed on FIRING and never learnt whether the cast worked, so
    -- 20,732 rules rest on zero meaning what it always meant. ScriptDev
    -- re-arms only on SUCCESS, retrying every tick until it lands, and this is
    -- that idea with the interval written down instead of implied.
    `retry`      INT UNSIGNED NOT NULL DEFAULT 0,

    -- The phases this rule does NOT fire in. Inverted, as EventAI had it, and
    -- kept inverted so a converted row means what it meant.
    `phase_mask` INT UNSIGNED NOT NULL DEFAULT 0,
    `chance`     TINYINT UNSIGNED NOT NULL DEFAULT 100,
    `flags`      INT UNSIGNED NOT NULL DEFAULT 0,
    `comment`    VARCHAR(255) NOT NULL DEFAULT '',

    PRIMARY KEY (`creature`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='MAI: when a sequence starts';

CREATE TABLE `mai_rule_step`
(
    `creature` INT UNSIGNED NOT NULL,
    `rule`     INT UNSIGNED NOT NULL,
    `seq`      SMALLINT UNSIGNED NOT NULL DEFAULT 0,

    -- Milliseconds from the moment the rule fired, and the whole point of
    -- folding the two systems together. EventAI's three action slots were
    -- simultaneous because a table needs a fixed width, so a creature that
    -- said one line, waited three seconds and said another was three rows, a
    -- phase and two timers -- there was nowhere to write "then".
    --
    -- Zero on all 27,561 converted steps, because that is what they were. It
    -- is written the moment something wants it: Shirrak summons his focus fire
    -- three times, one second apart, and in C++ that is a counter cycling
    -- 1-2-3 with a timer rewritten at each beat.
    `at_ms`    INT UNSIGNED NOT NULL DEFAULT 0,

    `action`   VARCHAR(48) NOT NULL,
    `params`   VARCHAR(512) NOT NULL DEFAULT '',

    -- The buddy search, the same one `mai_step` has had since the DB scripts:
    -- find a creature of this entry within this range and let the step act on
    -- it. Absent from rules until now, and that was inherited rather than
    -- designed -- EventAI had no buddy, so the rule tables grew without one.
    -- "Detonate one of the adds" is a buddy search with `random` set.
    `buddy_entry` INT UNSIGNED NOT NULL DEFAULT 0,
    `buddy_range` INT UNSIGNED NOT NULL DEFAULT 0,

    -- WHO the step acts on, asked at the moment it runs. Not part of `params`
    -- for the same reason the buddy is not: it modifies the step rather than
    -- being an argument of the verb. EventAI declared it as a parameter, which
    -- is why its `cast` took three arguments and its `remove_aura` took the
    -- target FIRST -- one concept, a different column per verb.
    --
    -- Zero is "itself", which is what a step with nothing to choose gets. The
    -- numbers are EventAI's own TARGET_T_*, unchanged.
    `select`   TINYINT UNSIGNED NOT NULL DEFAULT 0,

    -- How the selected unit joins the step: 0x01 means the creature acts ON it
    -- (`cast_spell`), and without the bit the selected unit IS the actor
    -- (`set_unit_field`). The same flags `mai_step` uses.
    `buddy_flags` TINYINT UNSIGNED NOT NULL DEFAULT 0,

    -- Out of 100, and 100 is always -- the SAME meaning the rule chance has,
    -- so nobody has to remember which of the two inverts. A rule chance asks
    -- whether the whole thing happens and is rolled once for all its steps; a
    -- step chance asks whether this one line gets said. Thespia casts her
    -- cloud every time and comments on it half the time.
    `chance`       TINYINT UNSIGNED NOT NULL DEFAULT 100,

    -- Whom to try when `select` finds nobody. 255 is "nothing", which is what
    -- every converted step has and what makes a step whose selector came up
    -- empty simply not happen. ScriptDev writes this fallback by hand at
    -- almost every selection:
    --
    --     pTarget = SelectAttackingTarget(RANDOM, 1, 0, SELECT_FLAG_PLAYER);
    --     if (!pTarget) { pTarget = m_creature->getVictim(); }
    `select_else`  TINYINT UNSIGNED NOT NULL DEFAULT 255,

    -- Whom the step ACTS AS, when that is not the creature whose rule it is.
    -- The symmetric half of `select`, and the lever that was missing: a
    -- selector could always choose whom a step acts ON, and the only way to
    -- change who acts was `buddy_flags` 2, ReverseDirection -- which does not
    -- choose, it swaps.
    --
    -- "The thing I just summoned attacks a random player" needs both halves at
    -- once and cannot be said with a swap. 255 is "leave the source alone",
    -- which is what every converted step means.
    `select_source` TINYINT UNSIGNED NOT NULL DEFAULT 255,

    -- What the selector will ACCEPT, as opposed to which one it is: a player,
    -- somebody with mana, somebody out of melee range. Creature.h SelectFlags.
    -- Orthogonal to `select`, which is why it is a second column and not more
    -- values in the first: every selector can be narrowed by every flag.
    `select_flags` TINYINT UNSIGNED NOT NULL DEFAULT 0,

    -- What this step is FOR, in words. `mai_step` has had one since the DB
    -- scripts and this did not, which was an accident of where each table came
    -- from rather than a decision: 20,732 converted rules had nothing to say,
    -- so nobody missed it. A hand-written encounter has a great deal to say,
    -- and a reader looking at `cast_spell spell=24883` deserves to be told it
    -- is the self-stun that everything below depends on.
    `comment`      VARCHAR(255) NOT NULL DEFAULT '',

    PRIMARY KEY (`creature`, `rule`, `seq`),
    CONSTRAINT `mai_rule_step_belongs_to_a_rule`
        FOREIGN KEY (`creature`, `rule`) REFERENCES `mai_rule` (`creature`, `id`)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='MAI: what a rule does';

-- ---------------------------------------------------------------------------
-- TEXT. One table, and the ids do not change.
--
-- There were three: creature_ai_texts for EventAI, script_texts for SD3,
-- db_script_string for the DB scripts. Identical in every column but one --
-- entry, content_default, nine locales, sound, type, language, emote, comment
-- -- and differing only in which system was allowed to read them.
--
-- The obvious fear is that merging them means renumbering, and renumbering
-- means rewriting every one of the 27,561 references that point at a text. It
-- does not, and this is measured rather than hoped:
--
--     creature_ai_texts     -2,005        ..            -1     1,015 rows
--     script_texts          -1,999,926    ..    -1,000,000     2,474 rows
--     db_script_string       2,000,000,001 .. 2,000,006,007      537 rows
--
--     rows sharing an entry between any two of them:  0
--
-- Three disjoint ranges, chosen to be disjoint by whoever laid them out, and
-- still disjoint after a decade of edits. So the merge is a union with the ids
-- kept verbatim, every existing reference keeps pointing at what it pointed
-- at, and nothing in the 686 + 5,822 converted files has to be touched.
--
-- `entry` is INT rather than the MEDIUMINT two of the three used, because the
-- DB-script range needs it. That is the only column that changes at all.
--
-- The guard at the bottom is the point of writing the ranges down: if the
-- three ever DO collide, this refuses to merge rather than silently keeping
-- whichever row happened to be inserted last -- which would give a creature
-- someone else's line and be found by a player, not by us.

DROP TABLE IF EXISTS `mai_text`;

CREATE TABLE `mai_text`
(
    `entry`           INT NOT NULL,
    `content_default` TEXT NOT NULL,
    `content_loc1`    TEXT,
    `content_loc2`    TEXT,
    `content_loc3`    TEXT,
    `content_loc4`    TEXT,
    `content_loc5`    TEXT,
    `content_loc6`    TEXT,
    `content_loc7`    TEXT,
    `content_loc8`    TEXT,
    `sound`           MEDIUMINT UNSIGNED NOT NULL DEFAULT 0,

    -- say, yell, text emote, boss emote, whisper, boss whisper. The same
    -- meanings in all three tables, checked: every row in every one of them
    -- uses a value in 0..6.
    `type`            TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `language`        TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `emote`           SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `comment`         TEXT,

    PRIMARY KEY (`entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='MAI: everything anything says';

-- Refuse to merge if the ranges have started to overlap. A duplicate entry
-- would otherwise be resolved by insertion order, which is to say by accident.
DELIMITER //
CREATE PROCEDURE `mai_merge_texts`()
BEGIN
    DECLARE clashes INT DEFAULT 0;

    SELECT COUNT(*) INTO clashes FROM (
        SELECT entry FROM `creature_ai_texts`
        UNION ALL SELECT entry FROM `script_texts`
        UNION ALL SELECT entry FROM `db_script_string`
    ) all_texts GROUP BY entry HAVING COUNT(*) > 1 LIMIT 1;

    IF clashes > 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT =
            'mai_text: the three text tables share an entry; merging would '
            'give something someone else''s line. Renumber before merging.';
    END IF;

    INSERT INTO `mai_text` SELECT * FROM `creature_ai_texts`;
    INSERT INTO `mai_text` SELECT * FROM `script_texts`;
    INSERT INTO `mai_text` SELECT * FROM `db_script_string`;
END //
DELIMITER ;

CALL `mai_merge_texts`();
DROP PROCEDURE `mai_merge_texts`;
