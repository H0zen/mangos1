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

# EventAI's action_type -> the MAI verb it means, and how its three params map.
# A name, not a number: the two systems number the same verbs differently and
# there is no reason for MAI to inherit either numbering for a translation.
#
# The parameter lists are the MAI parameter names, in the order EventAI's
# param1..3 fill them. An empty slot means EventAI did not use that column.
ACTIONS = {
    1:  ('talk', ['text0', 'text1', 'text2']),
    2:  ('set_faction', ['faction', 'flags']),
    3:  ('morph_to_entry_or_model', ['entry']),
    4:  ('play_sound', ['sound']),
    5:  ('emote', ['emote']),
    6:  ('talk', ['text0', 'text1', 'text2']),
    7:  ('talk', ['text0', 'text1', 'text2']),
    8:  ('talk', ['text0', 'text1', 'text2']),
    9:  ('play_sound', ['sound']),
    10: ('emote', ['emote']),
    11: ('cast_spell', ['spell', 'flags']),
    12: ('temp_summon_creature', ['entry', 'despawn_delay']),
    13: ('threat_change', ['percent']),
    14: ('threat_change', ['percent']),
    15: ('quest_event', ['quest']),
    16: ('cast_event', ['creature', 'spell']),
    17: ('set_unit_field', ['field', 'value']),
    18: ('set_unit_flag', ['value']),
    19: ('remove_unit_flag', ['value']),
    20: ('auto_attack', ['enable']),
    21: ('combat_movement', ['enable', 'melee']),
    22: ('set_phase', ['phase']),
    23: ('inc_phase', ['by']),
    24: ('evade', []),
    25: ('flee_for_assist', []),
    26: ('quest_event', ['quest']),
    27: ('cast_event', ['creature', 'spell']),
    28: ('remove_aura', ['spell']),
    29: ('ranged_movement', ['distance', 'angle']),
    30: ('random_phase', ['a', 'b', 'c']),
    31: ('random_phase', ['a', 'b']),
    32: ('temp_summon_creature', ['entry', 'despawn_delay']),
    33: ('killed_monster', ['creature']),
    34: ('set_instance_data', ['field', 'value']),
    35: ('set_instance_data64', ['field', 'low']),
    36: ('update_template', ['entry', 'faction']),
    37: ('die', []),
    38: ('zone_combat_pulse', []),
    39: ('call_for_help', ['radius']),
    40: ('set_sheath', ['state']),
    41: ('despawn_self', ['delay']),
    42: ('set_invincibility', ['hp', 'percent']),
    43: ('mount_to_entry_or_model', ['entry']),
    44: ('talk', ['text0']),
    45: ('throw_ai_event', ['event', 'radius']),
    46: ('set_throw_mask', ['mask']),
    47: ('stand_state', ['state']),
    48: ('change_movement', ['type', 'wander_distance']),
    49: ('temp_summon_creature', ['entry', 'despawn_delay']),
    50: ('emote_target', ['emote']),
}

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


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        return 1

    source, target = sys.argv[1], sys.argv[2]
    os.makedirs(target, exist_ok=True)

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
            if kind not in ACTIONS:
                unknown_actions[kind] += 1
                continue
            verb, params = ACTIONS[kind]
            args = [row['%sp%d' % (slot, i + 1)] for i in range(len(params))]
            steps.append((verb, pairs(params, args, action_optional[verb])))

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
            for verb, params in rule['steps']:
                story.append('--       ' + verb
                             + ((' ' + params) if params else ''))
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
                     '`action`, `params`) VALUES')
        values = []
        for rule in creatures[entry]:
            for seq, (verb, params) in enumerate(rule['steps']):
                values.append('(%d, %d, %d, %s, %s)'
                              % (entry, rule['id'], seq, quote(verb),
                                 quote(params)))
                fixture.write(TAB.join([str(entry), str(rule['id']),
                                        str(seq), verb, params]) + EOL)
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
