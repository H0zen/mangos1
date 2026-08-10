#!/usr/bin/env python3
"""Measure how much of SD3 is a rule set wearing C++.

    python survey_sd3.py <sd3/engine/scripts> [--list <category>]

WHY MEASURE BEFORE CONVERTING

"Convert what can be converted" is not a plan until someone says which of 319
files that is, and the answer is not guessable from the outside: SD3 holds
four-line gossip handlers and two-thousand-line instance state machines in the
same directory, under the same naming convention.

So this reads every file and sorts it into what it actually IS. Nothing is
converted here and nothing is approximated -- a file lands in `mechanical` only
when every statement in its AI is one of the handful of shapes MAI already has
a verb for. Everything else is named by what stopped it, because that list is
the actual roadmap: the shapes that appear forty times are worth a verb, and
the ones that appear once are worth leaving in C++ for ever.

THE SHAPE THAT CONVERTS

    uint32 m_uiThingTimer;                       -- a rule
    void Reset()  { m_uiThingTimer = 4000; }     -- its initial delay
    void UpdateAI(diff)
    {
        if (m_uiThingTimer < diff)               -- when it fires
        {
            DoCastSpellIfCan(target, SPELL_X);   -- what it does
            m_uiThingTimer = 7000;               -- and when again
        }
        else { m_uiThingTimer -= diff; }
    }

which is, exactly, `when timer_in_combat (initial=4000 repeat=7000) cast_spell
spell=X`. Anything else in that function -- a counter, a summon loop, a call
into instance data, a check on a member that is not a timer -- and the file is
not mechanical, because MAI would be guessing at what it meant.
"""
import collections
import io
import os
import re
import sys

EOL = chr(10)

# What a file registers with SD3. A file that registers anything other than a
# creature AI is not a rule set: an instance script holds encounter state, a
# gossip handler is a conversation, and neither is "when X, do Y".
REGISTRATIONS = (
    # DEFINES one, not merely talks to one. A boss that calls
    # m_pInstance->SetData() is still a boss; counting it as an instance script
    # because it holds the pointer overstated this category by a wide margin.
    ('instance',   re.compile(r':\s*public\s+(ScriptedInstance|InstanceData)\b')),
    ('gossip',     re.compile(r'\bOnGossipHello\b|\bOnGossipSelect\b')),
    ('quest',      re.compile(r'\bOnQuestAccept\b|\bOnQuestRewarded\b')),
    ('gameobject', re.compile(r'\bGameObjectScript\b|\bOnGameObjectUse\b')),
    ('item',       re.compile(r'\bItemScript\b|\bOnItemUse\b')),
    ('escort',     re.compile(r'\bnpc_escortAI\b|\bfollower_ai\b|\bFollowerAI\b')),
    ('pet',        re.compile(r'\bPetAI\b|\bScriptedPetAI\b')),
    ('effect',     re.compile(r'\bEffectDummyCreature\b|\bEffectAuraDummy\b')),
)

# Statements a mechanical AI is allowed to contain. Everything is anchored so a
# near-miss does not pass: `DoCastSpellIfCan` is allowed, `DoCast` is not, and
# the difference is that one reports whether it worked.
ALLOWED = (
    re.compile(r'^\s*$'),
    re.compile(r'^\s*//'),
    re.compile(r'^\s*[{}]\s*$'),
    re.compile(r'^\s*m_ui\w*Timer\s*=\s*\d+\s*;\s*$'),
    # A random re-arm is not an obstacle: it is exactly what `repeat` and
    # `repeat_max` are, and MAI has carried the pair since the first rule.
    re.compile(r'^\s*m_ui\w*Timer\s*=\s*urand\s*\(\s*\d+\s*,\s*\d+\s*\)\s*;\s*$'),
    # "the timer is running" -- a guard, not a decision about the world.
    re.compile(r'^\s*if\s*\(\s*m_ui\w*Timer\s*\)\s*$'),
    re.compile(r'^\s*m_ui\w*Timer\s*-=\s*\w+\s*;\s*$'),
    # `<` and `<=` alike. The two are not the same to the millisecond and the
    # scripts use both interchangeably, which is itself a small argument for
    # moving them: a rule says `repeat`, and the comparison stops being the
    # script author's problem.
    re.compile(r'^\s*if\s*\(\s*m_ui\w*Timer\s*<=?\s*\w+\s*\)\s*$'),
    re.compile(r'^\s*if\s*\(\s*\w+_Timer\s*<=?\s*\w+\s*\)\s*$'),
    re.compile(r'^\s*else\s*$'),
    re.compile(r'^\s*DoCastSpellIfCan\s*\('),
    re.compile(r'^\s*if\s*\(\s*DoCastSpellIfCan\s*\('),
    re.compile(r'^\s*if\s*\(\s*Unit\*\s*\w+\s*=\s*m_creature->SelectAttackingTarget\s*\('),
    re.compile(r'^\s*DoMeleeAttackIfReady\s*\(\s*\)\s*;\s*$'),
    re.compile(r'^\s*DoScriptText\s*\('),
    re.compile(r'^\s*if\s*\(\s*!m_creature->SelectHostileTarget\(\)\s*\|\|\s*!m_creature->getVictim\(\)\s*\)\s*$'),
    re.compile(r'^\s*return\s*;\s*$'),
)

TIMER = re.compile(r'\bm_ui(\w*?)Timer\b')


def classify(path):
    """(category, reason) for one file."""
    text = io.open(path, encoding='utf-8', errors='replace').read()

    for name, pattern in REGISTRATIONS:
        if pattern.search(text):
            return name, None

    if 'CreatureScript' not in text and 'ScriptedAI' not in text:
        return 'other', 'registers no creature AI'

    body = update_body(text)
    if body is None:
        return 'creature-ai', 'no UpdateAI to read'

    for line in body.split(EOL):
        if not any(rule.match(line) for rule in ALLOWED):
            return 'creature-ai', line.strip()[:70]

    if not TIMER.search(body):
        return 'creature-ai', 'no timers -- nothing to convert'

    return 'mechanical', None


def update_body(text):
    """The text of UpdateAI, brace-matched, or None."""
    start = text.find('void UpdateAI(')
    if start < 0:
        return None

    open_brace = text.find('{', start)
    if open_brace < 0:
        return None

    depth = 0
    for i in range(open_brace, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[open_brace + 1:i]
    return None


def main():
    if len(sys.argv) < 2:
        sys.stderr.write(__doc__)
        return 1

    root = sys.argv[1]
    wanted = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == '--list' else None

    counts = collections.Counter()
    reasons = collections.Counter()
    listed = []

    for where, _dirs, names in os.walk(root):
        for name in sorted(names):
            if not name.endswith('.cpp'):
                continue

            path = os.path.join(where, name)
            category, reason = classify(path)
            counts[category] += 1
            if reason:
                reasons[reason] += 1
            if wanted and category == wanted:
                listed.append(os.path.relpath(path, root))

    total = sum(counts.values())
    print('%d file(s)' % total)
    for category, count in counts.most_common():
        print('  %-14s %4d  (%4.1f%%)' % (category, count, 100.0 * count / total))

    print(EOL + 'what stopped the creature AIs, most common first:')
    for reason, count in reasons.most_common(20):
        print('  %3d  %s' % (count, reason))

    for name in listed:
        print('  ' + name)

    return 0


if __name__ == '__main__':
    sys.exit(main())
