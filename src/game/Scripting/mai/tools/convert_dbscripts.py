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

COLUMNS = ('script_type id delay command datalong datalong2 buddy_entry '
           'search_radius data_flags dataint dataint2 dataint3 dataint4 '
           'x y z o').split()


def load_manifest(path):
    """id -> (name, own parameter names, facet names), from the manifest."""
    facets, actions = {}, {}
    for raw in io.open(path, encoding='utf-8'):
        line = raw.split('#')[0].strip()
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
                own.append(token.split(':')[0])
        actions[ident] = (name, own, used)
    return facets, actions


def quote(text):
    return "'" + text.replace('\\', '\\\\').replace("'", "\\'") + "'"


def number(text):
    """A float that reads as an integer when it is one, so a coordinate keeps
    its precision and a spell id does not gain a `.0`."""
    value = float(text)
    return str(int(value)) if value == int(value) else repr(value)


def params_for(row, name, own, used, facets):
    """The `name=value` text for one row, in the manifest's own order."""
    pairs = []

    for i, param in enumerate(own):
        raw = row['datalong'] if i == 0 else row['datalong2']
        if int(raw) != 0:
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


HEADER = """-- MAI: %(title)s
--
-- Converted from `db_scripts` by tools/convert_dbscripts.py. Do not hand-edit
-- the conversion; edit the source rows and convert again, or edit this and
-- retire the source. Both are fine; doing both is not.
--
-- SCHEMA ASSUMED (see mai/schema.sql for the DDL)
--
--   mai_script(kind, id, name, comment)
--       kind    which moment starts it -- %(kinds)s
--       id      what that moment is keyed by: a quest, a spell, an entry, a guid
--
--   mai_step(kind, script, seq, at_ms, action, params, buddy_*, comment)
--       seq     breaks ties between steps sharing an at_ms
--       at_ms   MILLISECONDS from the start of the sequence, not from the step
--               before it. The source column was seconds; it is multiplied here.
--       action  a verb from actions.manifest
--       params  "name=value ..." -- names and types from the same manifest, and
--               checked against the world when the script loads
--       buddy_* who the step really acts on, when it is not the source
--
-- %(count)d step(s) in %(scripts)d script(s).

"""


def emit(path, title, kinds, scripts):
    lines = []
    steps = sum(len(rows) for rows in scripts.values())
    lines.append(HEADER % {'title': title,
                           'kinds': ', '.join(sorted(kinds)),
                           'count': steps,
                           'scripts': len(scripts)})

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
                params_for(row, name, own, used, facets),
                (int(row['buddy_entry']), int(row['search_radius']),
                 int(row['data_flags'])))

        entities[(kind, ident)].setdefault((kind, ident), []).append(step)

    written = 0
    steps = 0
    for key in sorted(entities):
        kind, ident = key
        # Steps sorted by time, insertion order kept among equals -- the order
        # the runner will walk them in, made explicit by `seq` so nothing has
        # to rely on how the rows come back out.
        for rows in entities[key].values():
            rows.sort(key=lambda step: step[0])

        path = os.path.join(target, '%s_%05d.sql' % (kind, ident))
        steps += emit(path, '%s %d' % (kind, ident), [kind], entities[key])
        written += 1

    print('%d file(s), %d step(s)' % (written, steps))
    if unknown:
        print('commands with no MAI action, skipped:')
        for command, count in unknown.most_common():
            print('  %d  (%d row(s))' % (command, count))
    return 0


if __name__ == '__main__':
    sys.exit(main())
