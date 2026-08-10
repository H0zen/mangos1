#!/usr/bin/env python3
"""Turn `creature_ai_scripts` into MAI rules, one SQL file per creature.

    python convert_eventai.py <creature_ai.tsv> <output-directory>

The export:

    SELECT id,creature_id,event_type,event_inverse_phase_mask,event_chance,
           event_flags,event_param1..4,
           action1_type,action1_param1..3,
           action2_type,action2_param1..3,
           action3_type,action3_param1..3,
           COALESCE(comment,'')
    FROM creature_ai_scripts ORDER BY creature_id,id

WHAT CHANGES SHAPE, AND WHY IT IS WORTH DOING

EventAI gives a row exactly three action slots. Not because three is a natural
number of things to do, but because a table needs a fixed width -- so a
creature that does four things on aggro is two rows with a duplicated event,
and the second row's trigger is a lie told to get more columns.

Here a rule starts a SEQUENCE, and a sequence is as long as it needs to be. The
three slots become three steps, all at time zero, and the fourth costs a row
rather than a fiction.

The event's own parameters keep their names from rules.manifest, so
`health_below percent_max=30 percent_min=0` says what `event_param1=30
event_param2=0` never could -- and, being named and typed, can be checked
against the world when it loads.

WHAT DOES NOT CHANGE

Phases stay a bitmask. They are EventAI's whole notion of state and they
deserve to become named states, but not in the same commit that moves twenty
thousand rows: a conversion has to be checkable against what it converted, and
"the same, but better" is not checkable. The mask comes across verbatim.
"""
import ast
import collections
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAI = os.path.dirname(HERE)

TAB = chr(9)
EOL = chr(10)

COLUMNS = ('id creature_id event_type phase_mask chance flags '
           'event_param1 event_param2 event_param3 event_param4 '
           'a1 a1p1 a1p2 a1p3 a2 a2p1 a2p2 a2p3 a3 a3p1 a3p2 a3p3 '
           'comment').split()

# The EventAI mapping is DECLARED, in eventai.map, and read here through the
# generator's own parser rather than copied. It used to be a table in this file
# and a second table in MaiRuleLowering.cpp, which is exactly the drift the
# manifests exist to end -- and it was not hypothetical: the positional table
# that lived here put a target selector into the cast flags of 6,556 rows and
# dropped the real cast flags of 3,873.
import gen_actions

# EventAI's event_type -> the MAI rule it means, and its four parameters.
RULES = {
    0:  ('timer_in_combat', ['initial', 'initial_max', 'repeat', 'repeat_max']),
    1:  ('timer_ooc', ['initial', 'initial_max', 'repeat', 'repeat_max']),
    2:  ('health_below', ['percent_max', 'percent_min', 'repeat', 'repeat_max']),
    3:  ('mana_below', ['percent_max', 'percent_min', 'repeat', 'repeat_max']),
    4:  ('aggro', []),
    5:  ('killed_unit', ['repeat', 'repeat_max']),
    6:  ('died', []),
    7:  ('evaded', []),
    8:  ('hit_by_spell', ['spell', 'school', 'repeat', 'repeat_max']),
    9:  ('target_in_range', ['min', 'max', 'repeat', 'repeat_max']),
    10: ('saw_unit', ['in_combat', 'range', 'repeat', 'repeat_max']),
    11: ('spawned', ['condition', 'value']),
    12: ('target_health_below',
         ['percent_max', 'percent_min', 'repeat', 'repeat_max']),
    13: ('target_casting', ['repeat', 'repeat_max']),
    14: ('friendly_hurt', ['radius', 'missing_hp', 'repeat', 'repeat_max']),
    15: ('friendly_controlled', ['radius', 'repeat', 'repeat_max']),
    16: ('friendly_missing_buff',
         ['radius', 'spell', 'repeat', 'repeat_max']),
    17: ('summoned_unit', ['creature', 'repeat', 'repeat_max']),
    18: ('target_mana_below',
         ['percent_max', 'percent_min', 'repeat', 'repeat_max']),
    19: ('quest_accepted', ['quest']),
    20: ('quest_completed', ['quest']),
    21: ('reached_home', []),
    22: ('received_emote', ['emote', 'condition', 'value']),
    23: ('has_aura', ['spell', 'stacks', 'repeat', 'repeat_max']),
    24: ('target_has_aura', ['spell', 'stacks', 'repeat', 'repeat_max']),
    25: ('summon_died', ['creature', 'repeat', 'repeat_max']),
    26: ('summon_despawned', ['creature', 'repeat', 'repeat_max']),
    27: ('missing_aura', ['spell', 'stacks', 'repeat', 'repeat_max']),
    28: ('target_missing_aura',
         ['spell', 'stacks', 'repeat', 'repeat_max']),
    29: ('timer', ['initial', 'initial_max', 'repeat', 'repeat_max']),
    30: ('received_ai_event', ['event', 'sender']),
    31: ('reached_waypoint', ['waypoint', 'path']),
    32: ('energy_below',
         ['percent_max', 'percent_min', 'repeat', 'repeat_max']),
}


def optional_names(path):
    """Which parameters a manifest declares optional -- a zero may be dropped
    only there, which is the lesson the db_scripts conversion taught."""
    optional = collections.defaultdict(set)
    current = None
    for raw in io.open(path, encoding='utf-8'):
        line = raw.split('#')[0].strip()
        if not line or line.startswith('category') or line.startswith('facet'):
            continue
        parts = line.split()
        if len(parts) < 2 or not parts[1].isdigit():
            continue
        current = parts[0]
        for token in parts[2:]:
            if ':' in token and token.endswith('?'):
                optional[current].add(token.split(':')[0])
    return optional


def pairs(names, values, optional):
    """`name=value` for the ones worth writing down."""
    out = []
    for name, value in zip(names, values):
        if int(value) == 0 and name in optional:
            continue
        out.append('%s=%s' % (name, value))
    return ' '.join(out)


def quote(text):
    return "'" + text.replace('\\', '\\\\').replace("'", "\\'") + "'"


HEADER = """-- MAI: creature %(entry)s
--
-- WHAT THIS CREATURE DOES
--
%(story)s
--
-- Converted from `creature_ai_scripts`. The prose above is generated from the
-- rows below; edit the rows, convert again, and it follows.
--
-- Three action slots became a sequence. EventAI gave every row exactly three,
-- not because three is a natural number of things to do but because a table
-- needs a fixed width -- so a creature doing four things on aggro was two rows
-- with the same trigger, the second one a fiction told to get more columns.
-- Here the fourth costs a row of mai_step.

"""


# The two TargetFlags this conversion can set. Named here because the values
# are the DB's own and MaiTargeting.h is where the names live.
BUDDY_AS_TARGET = 0x01
COMMAND_ADDITIONAL = 0x08

# TARGET_T_*, for the prose. The numbers are EventAI's own and are what goes
# into the `select` column; these names are only ever read by a person.
SELECTORS = (
    'itself', 'the victim', 'second on threat', 'last on threat',
    'anyone on threat', 'anyone but the top', 'whoever triggered it',
    "that unit's owner", 'a player on threat', 'a player but the top',
    'whoever sent the event',
)


def load_summons(path):
    """spawn id -> (x, y, z, o, despawn). Empty when no export was given."""
    summons = {}
    if not path:
        return summons

    for raw in io.open(path, encoding='utf-8'):
        cells = raw.rstrip(EOL).split(TAB)
        if len(cells) != 6:
            continue
        summons[int(cells[0])] = tuple(cells[1:])
    return summons


def build_step(verb, slots, flags, pin_slot, pin_value, form, args, names,
               optional, summons, problems):
    """One action slot, as (verb, params, select, flags) or None."""
    filled = {}
    select = 0
    buddy = flags

    if form == 'entry_or_model':
        # Two columns, one slot: an entry in the first or a model id in the
        # second, and the verb says which it holds with a flag. Both zero is
        # demorph, which is a literal zero rather than an absent parameter.
        if int(args[0]):
            filled[0] = args[0]
        elif int(args[1]):
            filled[0] = args[1]
            buddy |= COMMAND_ADDITIONAL
        else:
            filled[0] = '0'

    elif form == 'summon_spawn':
        # The third column names a row in `creature_ai_summons` holding the
        # position and the despawn time -- resolved here, at conversion, so a
        # step carries its own position and needs no second lookup per summon.
        spawn = int(args[2])
        if spawn not in summons:
            problems['summon %d is not in creature_ai_summons' % spawn] += 1
            return None

        x, y, z, o, despawn = summons[spawn]
        filled[0] = args[0]
        select = int(args[1])
        if int(despawn):
            filled[names.index('despawn_delay')] = despawn
        for axis, value in zip(('x', 'y', 'z', 'o'), (x, y, z, o)):
            filled[names.index(axis)] = value

    else:
        for column, slot in enumerate(slots):
            if slot is None:
                continue
            if slot == 'select':
                select = int(args[column])
                continue
            filled[slot] = args[column]

    if pin_slot is not None:
        filled[pin_slot] = str(pin_value)

    if select >= 11:
        problems['%s: %d is not a target selector' % (verb, select)] += 1
        return None

    # In slot order, and a zero is dropped only where the manifest says the
    # parameter is optional -- the lesson the db_scripts conversion taught, and
    # the reason an unused EventAI text id (which is 0) does not become text 0.
    written = []
    for slot in sorted(filled):
        name = names[slot]
        value = filled[slot]
        try:
            if int(float(value)) == 0 and float(value) == 0 and name in optional:
                continue
        except ValueError:
            pass
        written.append('%s=%s' % (name, value))

    return (verb, ' '.join(written), select, buddy)


def main():
    if len(sys.argv) not in (3, 4):
        sys.stderr.write(__doc__)
        return 1

    source, target = sys.argv[1], sys.argv[2]
    summons = load_summons(sys.argv[3] if len(sys.argv) == 4 else None)
    os.makedirs(target, exist_ok=True)

    # One mapping, read through the generator that also emits the C++ half.
    facets, cats = gen_actions.parse(gen_actions.MANIFEST)
    known = gen_actions.shapes(facets, cats)
    mapping = {row[0]: row
               for row in gen_actions.parse_map(gen_actions.EVENTAI, known)}

    action_optional = optional_names(os.path.join(MAI, 'actions.manifest'))
    rule_optional = optional_names(os.path.join(MAI, 'rules.manifest'))

    creatures = collections.defaultdict(list)
    unknown_rules = collections.Counter()
    unknown_actions = collections.Counter()

    for raw in io.open(source, encoding='utf-8'):
        cells = raw.rstrip(EOL).split(TAB)
        if len(cells) != len(COLUMNS):
            continue
        row = dict(zip(COLUMNS, cells))

        event = int(row['event_type'])
        if event not in RULES:
            unknown_rules[event] += 1
            continue

        rule, names = RULES[event]
        values = [row['event_param%d' % (i + 1)] for i in range(len(names))]

        steps = []
        for slot in ('a1', 'a2', 'a3'):
            kind = int(row[slot])
            if kind == 0:
                continue

            if kind not in mapping:
                unknown_actions['action type %d is not in eventai.map' % kind] += 1
                continue

            _kind, verb, slots, flags, pin, pinned, form, refused = mapping[kind]
            if refused:
                unknown_actions['action type %d %s' % (kind, refused)] += 1
                continue

            args = [row['%sp%d' % (slot, i + 1)] for i in range(3)]
            step = build_step(verb, slots, flags, pin, pinned, form, args,
                              known[verb][1], action_optional[verb], summons,
                              unknown_actions)
            if step:
                steps.append(step)

        if not steps:
            continue

        creatures[int(row['creature_id'])].append({
            'id': int(row['id']),
            'rule': rule,
            'params': pairs(names, values, rule_optional[rule]),
            'phase_mask': int(row['phase_mask']),
            'chance': int(row['chance']),
            'flags': int(row['flags']),
            'steps': steps,
            'comment': row['comment'],
        })

    written = 0
    rules = 0
    steps = 0

    fixture = io.open(os.path.join(target, 'converted.tsv'), 'w',
                      encoding='utf-8', newline=EOL)

    for entry in sorted(creatures):
        story = []
        for rule in creatures[entry]:
            when = 'when ' + rule['rule']
            if rule['params']:
                when += ' (' + rule['params'] + ')'
            if rule['chance'] and rule['chance'] != 100:
                when += ', %d%% of the time' % rule['chance']
            if rule['phase_mask']:
                when += ', except in phases 0x%02X' % rule['phase_mask']
            story.append('--   ' + when)
            for verb, params, select, _buddy in rule['steps']:
                told = '--       ' + verb
                if params:
                    told += ' ' + params
                # The selector is the thing EventAI could never say out loud:
                # `cast_spell spell=11962 flags=1` meant "at the victim", and
                # the 1 was in a column called flags.
                if select:
                    told += ' -> ' + SELECTORS[select]
                story.append(told)
            if rule['comment']:
                story.append('--       -- ' + rule['comment'])

        lines = [HEADER % {'entry': entry, 'story': EOL.join(story)}]

        lines.append('INSERT INTO `mai_rule` (`creature`, `id`, `rule`, '
                     '`params`, `phase_mask`, `chance`, `flags`, `comment`) '
                     'VALUES')
        values = []
        for rule in creatures[entry]:
            values.append('(%d, %d, %s, %s, %d, %d, %d, %s)'
                          % (entry, rule['id'], quote(rule['rule']),
                             quote(rule['params']), rule['phase_mask'],
                             rule['chance'], rule['flags'],
                             quote(rule['comment'])))
        lines.append((',' + EOL).join(values) + ';')
        lines.append('')

        lines.append('INSERT INTO `mai_rule_step` (`creature`, `rule`, `seq`, '
                     '`action`, `params`, `select`, `buddy_flags`) VALUES')
        values = []
        for rule in creatures[entry]:
            for seq, (verb, params, select, buddy) in enumerate(rule['steps']):
                values.append('(%d, %d, %d, %s, %s, %d, %d)'
                              % (entry, rule['id'], seq, quote(verb),
                                 quote(params), select, buddy))
                fixture.write(TAB.join([str(entry), str(rule['id']),
                                        str(seq), verb, params,
                                        str(select), str(buddy)]) + EOL)
                steps += 1
            rules += 1
        lines.append((',' + EOL).join(values) + ';')
        lines.append('')

        path = os.path.join(target, 'creature_%06d.sql' % entry)
        io.open(path, 'w', encoding='utf-8', newline=EOL).write(EOL.join(lines))
        written += 1

    fixture.close()
    print('%d file(s), %d rule(s), %d step(s)' % (written, rules, steps))
    if unknown_rules:
        print('event types with no MAI rule:',
              ', '.join('%d (%d)' % kv for kv in unknown_rules.most_common()))
    if unknown_actions:
        print('action types with no MAI verb:',
              ', '.join('%d (%d)' % kv
                        for kv in unknown_actions.most_common()))
    return 0


if __name__ == '__main__':
    sys.exit(main())
