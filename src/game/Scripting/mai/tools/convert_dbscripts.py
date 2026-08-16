#!/usr/bin/env python3
"""Turn a `db_scripts` export into MAI's tables, one SQL file per entity.

    python convert_dbscripts.py <db_scripts.tsv> <output-directory>

The export is what the query in the differential test's fixture produces:

    SELECT script_type,id,delay,command,datalong,datalong2,buddy_entry,
           search_radius,data_flags,dataint,dataint2,dataint3,dataint4,x,y,z,o
    FROM db_scripts ORDER BY script_type,id,delay,script_guid

ONE FILE PER ENTITY, not one enormous migration. A dungeon's scripts should be
reviewable on their own, land on their own, and be revertable on their own --
and a conflict between two people editing two different bosses should not be a
conflict at all. Each file carries the schema it assumes, in pseudocode, in
comments, so it can be read years from now without the DDL to hand.

The parameter names come from actions.manifest, parsed here rather than
duplicated: if a verb's parameters are ever renamed, the conversion follows and
nothing has to remember to.
"""
import collections
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAI = os.path.dirname(HERE)
MANIFEST = os.path.join(MAI, 'actions.manifest')

# script_type -> the kind column. The numbers are DBScriptType's, in order.
KINDS = ['quest_start', 'quest_end', 'spell', 'go_use', 'go_template_use',
         'creature_death', 'creature_movement', 'gossip', 'event', 'internal']

TAB = chr(9)
EOL = chr(10)

COLUMNS = ('script_type id delay command datalong datalong2 buddy_entry '
           'search_radius data_flags dataint dataint2 dataint3 dataint4 '
           'x y z o').split()


def joined(path):
    """The file's lines, with a trailing backslash meaning "and the next one".

    The same reader gen_actions.py uses, and it has to be: a verb with four
    optional parameters does not fit in eighty columns, so the manifest wraps
    it. Reading the manifest a line at a time instead gets half a declaration
    and a bare `\\` where a `name:type` belongs.
    """
    held = ''
    for raw in io.open(path, encoding='utf-8'):
        line = raw.split('#')[0].rstrip()
        if line.endswith('\\'):
            held += line[:-1]
            continue
        yield held + line
        held = ''
    if held:
        yield held


def load_manifest(path):
    """id -> (name, own parameter names, facet names), from the manifest."""
    facets, actions = {}, {}
    for raw in joined(path):
        line = raw.strip()
        if not line or line.startswith('category'):
            continue

        facet = re.match(r'^facet\s+(\w+)\s+(.*)$', line)
        if facet:
            facets[facet.group(1)] = [p.split(':')[0]
                                      for p in facet.group(2).split()]
            continue

        parts = line.split()
        if len(parts) < 2 or not parts[1].isdigit():
            continue

        name, ident = parts[0], int(parts[1])
        own, used = [], []
        for token in parts[2:]:
            if token in facets:
                used.append(token)
            else:
                param, kind = token.split(':', 1)
                own.append((param, kind.rstrip('?'), kind.endswith('?')))
        actions[ident] = (name, own, used)
    return facets, actions


def quote(text):
    return "'" + text.replace('\\', '\\\\').replace("'", "\\'") + "'"


def number(text):
    """A float that reads as an integer when it is one, so a coordinate keeps
    its precision and a spell id does not gain a `.0`."""
    value = float(text)
    return str(int(value)) if value == int(value) else repr(value)


def params_for(row, name, own, used, facets, problems=None):
    """The `name=value` text for one row, in the manifest's own order.

    A db_scripts row carries exactly TWO generic longs, so only the verb's
    first two own parameters have a column to come from. A third has no source
    at all and is left absent -- which is what it means, and what the verb's
    own default then supplies.

    Written as "datalong for the first, datalong2 for the rest" this quietly
    gave every parameter past the second a COPY of the second. On one_world
    that was 531 `temp_summon_creature` rows whose `scatter` -- a radius in
    yards -- became the despawn delay in milliseconds, so a summon meant to
    appear where it was told scattered over 300,000 yards.

    The other 15 verbs with a third parameter were untouched only by luck: the
    163 `cast_spell` rows all happen to carry `datalong2` = 0, so their
    `credit_owner` and `stop_if_refused` were dropped as absent instead of
    being set true. The value was a plausible number in a plausible column
    either way, which is why this survived a conversion and a load.
    """
    pairs = []

    for i, (param, kind, optional) in enumerate(own):
        if i > 1:
            # No column to read. A required one is a mismatch between the
            # manifest and what db_scripts can express, and is worth saying
            # out loud rather than filling with a zero.
            if not optional and problems is not None:
                problems['%s wants %s and db_scripts has no column for it'
                         % (name, param)] += 1
            continue

        raw = row['datalong'] if i == 0 else row['datalong2']
        # A zero is dropped only where it is allowed to be absent. Dropping a
        # required one leaves a row the loader refuses, which the round-trip
        # test catches and a database would not.
        if int(raw) != 0 or not optional:
            pairs.append('%s=%s' % (param, raw))

    for facet in used:
        if facet == 'texts':
            for i, param in enumerate(facets[facet]):
                value = int(row['dataint' if i == 0 else 'dataint%d' % (i + 1)])
                if value != 0:
                    pairs.append('%s=%d' % (param, value))
        elif facet == 'at':
            for param, column in zip(facets[facet], ('x', 'y', 'z', 'o')):
                pairs.append('%s=%s' % (param, number(row[column])))

    return ' '.join(pairs)


# How a verb reads in prose. A clause in [brackets] is dropped when a parameter
# it names was not given. Nothing here changes what runs -- it is the comment at
# the top of the file, so that opening one tells you what the script DOES before
# it tells you what is in it.
PROSE = {
    'talk':                    'say [$text0][ or $text1][ or $text2][ or $text3]',
    'emote':                   'play emote $emote',
    'play_sound':              'play sound $sound',
    'play_movie':              'show movie $movie',
    'field_set':               'set field $field to $value',
    'flag_set':                'set flags $value on field $field',
    'flag_remove':             'clear flags $value on field $field',
    'morph_to_entry_or_model': 'morph into $entry',
    'mount_to_entry_or_model': 'mount $entry',
    'change_entry':            'become creature $entry',
    'update_template':         'become creature $entry of faction $faction',
    'set_equipment_slots':     'reset equipment',
    'modify_npc_flags':        'change npc flags $flag',
    'set_faction':             'change faction to $faction',
    'move_to':                 'walk to ($x, $y, $z)[ at speed $speed]',
    'teleport_to':             'teleport to map $map at ($x, $y, $z)',
    'movement':                'switch to movement type $type[ wandering $wander_distance]',
    'set_run':                 'set running to $run',
    'turn_to':                 'turn to face target $target',
    'move_dynamic':            'move to a spot $max_dist away[ but no closer than $min_dist]',
    'send_taxi_path':          'put the player on taxi path $path',
    'pause_waypoints':         'set waypoint pause to $pause',
    'set_fly':                 'set flying to $enable',
    'stand_state':             'change stand state to $state',
    'cast_spell':              'cast spell $spell',
    'remove_aura':             'remove aura $spell',
    'attack_start':            'start attacking the target',
    'despawn_self':            'despawn[ after $delay]',
    'respawn':                 'respawn',
    'respawn_go':              'respawn gameobject $guid[, despawning again after $despawn_delay]',
    'despawn_go':              'despawn gameobject $guid[ for $respawn_time]',
    'open_door':               'open door $guid[, closing after $reset_delay]',
    'close_door':              'close door $guid[, opening after $reset_delay]',
    'activate_object':         'use the object',
    'reset_go':                'reset the object',
    'go_lock_state':           'set object lock state $state',
    'temp_summon_creature':    'summon creature $entry at ($x, $y, $z)[ for $despawn_delay]',
    'set_activeobject':        'set active to $activate',
    'quest_explored':          'credit quest $quest as explored[ within $distance]',
    'kill_credit':             'give kill credit for $entry[ (group: $group_credit)]',
    'create_item':             'give item $item[ x$amount]',
    'send_mail':               'send mail template $template[ from $alt_sender]',
    'join_lfg':                'join the queue for area $area',
    'xp_user':                 'set xp gain flags $flags',
    'terminate_script':        'stop here[ unless creature $entry is within $search_dist]',
    'terminate_cond':          'stop here if condition $condition[, failing quest $fail_quest]',
    'send_ai_event_around':    'send ai event $event to everything within $radius',
}


def duration(ms):
    if ms == 0:
        return 'at once'
    if ms >= 60000 and ms % 60000 == 0:
        return 'at ' + str(ms // 60000) + 'm'
    if ms % 1000 == 0:
        return 'at ' + str(ms // 1000) + 's'
    return 'at ' + str(ms) + 'ms'


def prose(action, params, buddy):
    """One step, as a sentence."""
    values = dict(pair.split('=', 1) for pair in params.split() if '=' in pair)
    text = PROSE.get(action)
    if not text:
        return action + ' ' + params

    # Bracketed clauses survive only when every name inside them was given.
    out, i = '', 0
    while i < len(text):
        if text[i] == '[':
            close = text.index(']', i)
            clause = text[i + 1:close]
            names = re.findall(r'\$(\w+)', clause)
            if all(n in values for n in names):
                out += clause
            i = close + 1
        else:
            out += text[i]
            i += 1

    for name in sorted(values, key=len, reverse=True):
        out = out.replace('$' + name, values[name])
    out = re.sub(r'\$\w+', '?', out)

    if buddy[0]:
        where = ('guid ' + str(buddy[1])) if buddy[2] & 0x10 \
            else ('within ' + str(buddy[1]))
        out += ' -- through creature ' + str(buddy[0]) + ' (' + where + ')'
    return out


HEADER = """-- MAI: %(title)s
--
-- WHAT THIS SCRIPT DOES
--
%(story)s
--
-- The prose above is generated from the rows below and is the point of reading
-- this file; the rows are the point of running it. Edit the rows, convert
-- again, and the prose follows. See mai/schema.sql for what the columns mean.

"""



def emit(path, title, kinds, scripts):
    lines = []
    steps = sum(len(rows) for rows in scripts.values())
    story = []
    for key in sorted(scripts):
        for at_ms, action, params, buddy in scripts[key]:
            story.append('--   %-9s %s'
                         % (duration(at_ms), prose(action, params, buddy)))
    lines.append(HEADER % {'title': title, 'story': EOL.join(story)})

    lines.append('INSERT INTO `mai_script` (`kind`, `id`, `name`) VALUES')
    values = []
    for (kind, ident) in sorted(scripts):
        values.append("(%s, %d, %s)" % (quote(kind), ident,
                                        quote('%s %d' % (kind, ident))))
    lines.append(',\n'.join(values) + ';')
    lines.append('')

    lines.append('INSERT INTO `mai_step` (`kind`, `script`, `seq`, `at_ms`, '
                 '`action`, `params`, `buddy_entry`, `buddy_range`, '
                 '`buddy_flags`) VALUES')
    values = []
    for key in sorted(scripts):
        kind, ident = key
        for seq, (at_ms, action, params, buddy) in enumerate(scripts[key]):
            values.append("(%s, %d, %d, %d, %s, %s, %d, %d, %d)"
                          % (quote(kind), ident, seq, at_ms, quote(action),
                             quote(params), buddy[0], buddy[1], buddy[2]))
    lines.append(',\n'.join(values) + ';')
    lines.append('')

    io.open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
    return steps


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        return 1

    source, target = sys.argv[1], sys.argv[2]
    facets, actions = load_manifest(MANIFEST)
    os.makedirs(target, exist_ok=True)

    # Grouped by entity: one file per script id, which is one quest, one spell,
    # one creature or one placed object. A dungeon lands as a handful of files
    # that can be reviewed and reverted apart from each other.
    entities = collections.defaultdict(dict)
    unknown = collections.Counter()

    # Kept apart from `unknown`, which is keyed by command NUMBER. One counter
    # for both would print a name through a `%d`.
    problems = collections.Counter()

    for raw in io.open(source, encoding='utf-8'):
        cells = raw.rstrip('\n').split('\t')
        if len(cells) != len(COLUMNS):
            continue
        row = dict(zip(COLUMNS, cells))

        command = int(row['command'])
        if command not in actions:
            unknown[command] += 1
            continue

        name, own, used = actions[command]
        kind = KINDS[int(row['script_type'])] \
            if int(row['script_type']) < len(KINDS) else 'internal'
        ident = int(row['id'])

        step = (int(row['delay']) * 1000,
                name,
                params_for(row, name, own, used, facets, problems),
                (int(row['buddy_entry']), int(row['search_radius']),
                 int(row['data_flags'])))

        entities[(kind, ident)].setdefault((kind, ident), []).append(step)

    # A machine-readable copy of the same conversion, so a test can prove the
    # SQL says exactly what the rows it came from said. Without it the only way
    # to check 686 files would be to load them into a database, which is a
    # dependency a unit test must not have.
    fixture = io.open(os.path.join(target, 'converted.tsv'), 'w',
                      encoding='utf-8', newline='\n')

    written = 0
    steps = 0
    for key in sorted(entities):
        kind, ident = key
        # Steps sorted by time, insertion order kept among equals -- the order
        # the runner will walk them in, made explicit by `seq` so nothing has
        # to rely on how the rows come back out.
        for rows in entities[key].values():
            rows.sort(key=lambda step: step[0])

        for rows in entities[key].values():
            for seq, (at_ms, action, params, buddy) in enumerate(rows):
                cells = [kind, str(ident), str(seq), str(at_ms), action,
                         params, str(buddy[0]), str(buddy[1]),
                         str(buddy[2])]
                fixture.write(TAB.join(cells) + EOL)

        path = os.path.join(target, '%s_%05d.sql' % (kind, ident))
        steps += emit(path, '%s %d' % (kind, ident), [kind], entities[key])
        written += 1

    fixture.close()
    print('%d file(s), %d step(s)' % (written, steps))
    if unknown:
        print('commands with no MAI action, skipped:')
        for command, count in unknown.most_common():
            print('  %d  (%d row(s))' % (command, count))
    if problems:
        print('parameters the manifest wants and db_scripts cannot supply:')
        for what, count in problems.most_common():
            print('  %s  (%d row(s))' % (what, count))
    return 0


if __name__ == '__main__':
    sys.exit(main())
