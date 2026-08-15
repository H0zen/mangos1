-- MAI migration. Run once, in this order.
--
-- Everything here is idempotent-by-guard rather than idempotent-by-luck: each
-- step checks what it is about to rely on and stops with a message rather than
-- doing half of it. A migration that gives up loudly costs an evening; one that
-- half-succeeds costs a weekend of working out which half.
--
-- WHAT THIS DOES
--
--   1. merges the three text tables into mai_text, ids unchanged
--   2. creates the MAI tables
--   3. loads the converted scripts   (the per-entity files beside this one)
--   4. drops the tables MAI replaces
--
-- STEP 4 IS SEPARATE AND LAST, and commented out. Run the first three, restart
-- the server, watch it for as long as your nerve requires, and only then drop
-- anything. The old tables cost a few megabytes; a rollback that needs them and
-- cannot have them costs considerably more.

-- ---------------------------------------------------------------------------
-- 1. TEXT
--
-- The three ranges are disjoint -- measured, not assumed:
--
--     creature_ai_texts     -2,005         ..             -1     1,015 rows
--     script_texts          -1,999,926     ..     -1,000,000     2,474 rows
--     db_script_string       2,000,000,001 ..  2,000,006,007       537 rows
--
--     rows sharing an entry between any two of them:  0
--
-- So no id changes and no reference is rewritten. `entry` widens to INT because
-- the DB-script range needs it; that is the only column that differs.

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
    `type`            TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `language`        TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `emote`           SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `comment`         TEXT,

    PRIMARY KEY (`entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='MAI: everything anything says';

DELIMITER //
CREATE PROCEDURE `mai_merge_texts`()
BEGIN
    DECLARE clashes INT DEFAULT 0;

    SELECT COUNT(*) INTO clashes FROM (
        SELECT `entry` FROM `creature_ai_texts`
        UNION ALL SELECT `entry` FROM `script_texts`
        UNION ALL SELECT `entry` FROM `db_script_string`
    ) `all_texts` GROUP BY `entry` HAVING COUNT(*) > 1 LIMIT 1;

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

-- ---------------------------------------------------------------------------
-- 2. THE TABLES
--
-- mai_script / mai_step   a sequence: what happens, and when, from its start
-- mai_rule / mai_rule_step  when a sequence starts
--
-- Their reasoning is in src/game/Scripting/mai/schema.sql, which this file
-- deliberately repeats rather than references: a migration someone runs at
-- three in the morning should not send them to a source tree.

DROP TABLE IF EXISTS `mai_step`;
DROP TABLE IF EXISTS `mai_script`;
DROP TABLE IF EXISTS `mai_rule_step`;
DROP TABLE IF EXISTS `mai_rule`;

CREATE TABLE `mai_script`
(
    `id`      INT UNSIGNED NOT NULL,
    `kind`    ENUM('quest_start','quest_end','spell','go_use',
                   'go_template_use','creature_death','creature_movement',
                   'gossip','event','internal') NOT NULL,
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
    `seq`      SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `at_ms`    INT UNSIGNED NOT NULL DEFAULT 0,
    `action`   VARCHAR(48) NOT NULL,
    `params`   VARCHAR(512) NOT NULL DEFAULT '',
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

CREATE TABLE `mai_rule`
(
    `creature`   INT UNSIGNED NOT NULL,
    `id`         INT UNSIGNED NOT NULL,
    `rule`       VARCHAR(48) NOT NULL,
    `params`     VARCHAR(512) NOT NULL DEFAULT '',
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
    `action`   VARCHAR(48) NOT NULL,
    `params`   VARCHAR(512) NOT NULL DEFAULT '',

    PRIMARY KEY (`creature`, `rule`, `seq`),
    CONSTRAINT `mai_rule_step_belongs_to_a_rule`
        FOREIGN KEY (`creature`, `rule`) REFERENCES `mai_rule` (`creature`, `id`)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='MAI: what a rule does';

-- ---------------------------------------------------------------------------
-- 3. THE SCRIPTS
--
-- The per-entity files beside this one. From a shell:
--
--     cat dbmai/*.sql          | mysql <world>     # 686 sequences
--     cat dbmai_eventai/*.sql  | mysql <world>     # 5,822 creatures
--
-- They are separate files on purpose: a dungeon should land, be reviewed and
-- be reverted on its own, and two people editing two different bosses should
-- not have a conflict at all.

-- ---------------------------------------------------------------------------
-- 4. DROPPING WHAT MAI REPLACES  -- NOT YET. Deliberately commented out.
--
-- Run steps 1 to 3, restart, and watch. Then come back and uncomment. These
-- tables are a few megabytes; a rollback that needs them and cannot have them
-- is an evening you do not get back.
--
-- Nothing below is reachable from the server once MAI is the only engine, but
-- "not reachable" and "not needed" are different claims and only the first has
-- been demonstrated.

-- DROP TABLE IF EXISTS `db_scripts`;
-- DROP TABLE IF EXISTS `db_script_string`;
-- DROP TABLE IF EXISTS `creature_ai_scripts`;
-- DROP TABLE IF EXISTS `creature_ai_texts`;
-- DROP TABLE IF EXISTS `creature_ai_summons`;
-- DROP TABLE IF EXISTS `script_texts`;
