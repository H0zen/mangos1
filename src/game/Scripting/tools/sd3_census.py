#!/usr/bin/env python3
"""Measure what the SD3 scripts actually do, so MAI can be designed to absorb
them rather than to look as though it might.

    python src/game/Scripting/tools/sd3_census.py [--verbose]

Three questions, in order of how much they decide:

  1. WHAT DOES A GUARD LOOK LIKE?  There are ten thousand `if`s in SD3 and only
     a few dozen kinds of action. The expression language is therefore the part
     that decides whether absorption works; if it comes out too poor, MAI ends
     up beside SD3 instead of replacing it, and we have three systems instead
     of two. This is measured first for that reason.

  2. WHAT DOES A SCRIPT DO?  The action vocabulary, by frequency, so the ones
     worth having as primitives are chosen by evidence.

  3. WHICH CLASSES CANNOT BE DATA?  Named individually, not estimated -- a
     percentage nobody can audit is not a plan.

This does not parse C++. It matches text, and it is honest about that: the
numbers are a shape, accurate to a few percent, and the residual list at the
end is the part meant to be read by a human rather than trusted.
"""
import collections
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SEAM = os.path.dirname(HERE)
SCRIPTS = os.path.join(SEAM, 'sd3', 'engine', 'scripts')


def sources(root):
    for path, _dirs, names in os.walk(root):
        for name in sorted(names):
            if name.endswith('.cpp'):
                full = os.path.join(path, name)
                yield (os.path.relpath(full, root).replace('\\', '/'),
                       io.open(full, encoding='utf-8', errors='replace').read())


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def bodies(text):
    """(name, base, body) for every `struct X : public Y { ... }`."""
    for match in re.finditer(r'(?:struct|class)\s+(\w+)\s*:\s*public\s+([\w:]+)'
                             r'[^{;]*\{', text):
        depth, i = 0, match.end() - 1
        while i < len(text):
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        yield match.group(1), match.group(2), text[match.end():i]


def loop_headers(body):
    """The text inside every `for (...)` / `while (...)`, brackets balanced."""
    for match in re.finditer(r'(?<![A-Za-z_])(?:for|while)\s*\(', body):
        depth, i = 1, match.end()
        while i < len(body) and depth:
            if body[i] == '(':
                depth += 1
            elif body[i] == ')':
                depth -= 1
            i += 1
        yield body[match.end():i - 1]


def conditions(body):
    """The text inside every `if (...)`, brackets balanced."""
    for match in re.finditer(r'\bif\s*\(', body):
        depth, i = 1, match.end()
        while i < len(body) and depth:
            if body[i] == '(':
                depth += 1
            elif body[i] == ')':
                depth -= 1
            i += 1
        yield body[match.end():i - 1]


# What a guard is ABOUT. Ordered: the first that matches wins, so the specific
# forms are listed before the general ones.
GUARD_FORMS = [
    # Looking an object up and checking it was found. The commonest guard in
    # SD3 by a wide margin, and not a condition at all in MAI: a selector that
    # finds nothing simply does not fire the rule.
    ('find an object',     r'GetSingleCreatureFromStorage|GetSingleGameObjectFromStorage'
                           r'|GetClosest(?:Creature|GameObject)WithEntry|GetCreature\(|GetGameObject\('
                           r'|SelectAttackingTarget|GetTargetByType|SelectHostileTarget'),
    # A test on the hook's own parameter -- which waypoint, which data slot,
    # which quest. In MAI the rule matches the event, so the `if` disappears.
    ('match the event',    r'\bui(?:Type|Data|PointId|EventType|SpellId|Emote|MiscValue)\b'
                           r'|\beventType\b|GetQuestId\(\)|SD3_SpellId'),
    ('timer expired',      r'm_ui\w*Timer\s*<=?\s*u?i?Diff'),
    ('health percent',     r'GetHealthPercent|HealthBelowPct|HealthAbovePct'),
    ('escort state',       r'GetPlayerForEscort|HasEscortState|IsEscorted'),
    ('instance state',     r'(?:pInstance|m_pInstance)->GetData|GetInstanceData'
                           r'|m_auiEncounter'),
    ('has an instance',    r'^\s*!?\s*m_?p?[Ii]nstance\s*$'),
    ('a phase/step field', r'm_ui\w*(?:Phase|Stage|Step|Event)\b'),
    ('a flag field',       r'm_b\w+'),
    ('a counter field',    r'm_ui\w*(?:Count|Killed|Alive|Index)\b'),
    ('cast succeeded',     r'CAST_OK|DoCastSpellIfCan'),
    ('who the target is',  r'GetTypeId\(\)\s*[!=]=\s*TYPEID|->ToPlayer|IsPlayer\(\)|IsCreature\(\)'),
    ('entry is',           r'GetEntry\(\)\s*[!=]='),
    ('alive / dead',       r'\bIs[Aa]live\(\)|\bIsDead\(\)'),
    ('in combat',          r'IsInCombat|getVictim\(\)'),
    ('distance / range',   r'IsWithinDist|GetDistance|WithinDist|IsInRange|IsWithinLOS|InReach'),
    ('has aura',           r'HasAura|GetAura\('),
    ('random roll',        r'\b[uf]?rand\('),
    ('quest state',        r'GetQuestStatus|IsActiveQuest|HasQuest'),
    ('map / difficulty',   r'GetMap\(\)|IsRegularDifficulty|GetDifficulty'),
    ('gameobject state',   r'GetGoState|GetLootState'),
    ('faction / hostile',  r'IsHostileTo|IsFriendlyTo|getFaction'),
    ('null check',         r'^\s*!?\s*[\w>.\-]+\s*$'),
]

# Constructs no table can hold. `new <thing>AI(` is factory boilerplate every
# SD3 script has and is not evidence of anything.
HARD_FORMS = [
    ('builds a container',  r'\bstd::(?:vector|list|map|set|deque)\b'),
    ('allocates',           r'\bnew\s+(?!\w*AI\s*\()(?!\w*Instance\w*\s*\()\w+'),
    ('walks something',     r'\b(?:for|while)\s*\('),
    ('talks to the wire',   r'\bWorldPacket\b|\bSMSG_|\bCMSG_'),
    ('does trigonometry',   r'\b(?:sqrt|atan2|asin|acos|sin|cos|pow)\s*\('),
    ('queries the DB',      r'\b(?:WorldDatabase|CharacterDatabase)\b'),
    ('declares a type',     r'\bstruct\s+\w+\s*\{|\benum\s+\w*\s*\{'),
]

# A loop is the commonest reason a class looks hard, and most loops are one of
# a few shapes that MAI can express as a selector or a broadcast.
LOOP_FORMS = [
    ('over the threat list',    r'ThreatList|getThreatList|HostileReference'),
    ('over players on the map', r'GetPlayers|PlayerList|m_mapRefManager|MapRefManager'
                                r'|GroupReference'),
    # Encounter arrays and "the four adds": a counted walk over a fixed span,
    # which is a declared collection in MAI rather than an index.
    ('over a fixed array',      r'\b(?:uint8|uint32|int|size_t)\s+\w+\s*=\s*[A-Z0-9_]+\s*;'
                                r'|\b\w+\s*<\s*(?:countof|\d+|MAX_|TYPE_|sizeof)'),
    # Guids the script stashed itself -- summons, adds, doors. A group in MAI.
    ('over stored guids',       r'Guid(?:List|Set|Vector)|m_(?:lu?i|v)\w*(?:Guid|List|Summon)'
                                r'|std::(?:list|vector|set)<\s*ObjectGuid'),
]


def classify(text, forms, default='other'):
    for label, pattern in forms:
        if re.search(pattern, text, re.M):
            return label
    return default


def main():
    verbose = '--verbose' in sys.argv
    if not os.path.isdir(SCRIPTS):
        sys.stderr.write('error: no SD3 scripts at %s\n' % SCRIPTS)
        return 1

    guards = collections.Counter()
    guard_terms = collections.Counter()
    hard_reasons = collections.Counter()
    loop_shapes = collections.Counter()
    by_base = collections.Counter()
    residual = []
    soft = 0
    classes = 0
    lines = 0
    files = 0

    for name, raw in sources(SCRIPTS):
        files += 1
        lines += raw.count('\n') + 1
        text = strip_comments(raw)

        for cls, base, body in bodies(text):
            classes += 1
            by_base[base] += 1

            for cond in conditions(body):
                guards[classify(cond, GUARD_FORMS)] += 1
                # How many things one guard tests at once: `&&`/`||` count.
                guard_terms[min(1 + len(re.findall(r'&&|\|\|', cond)), 5)] += 1

            reasons = [label for label, pattern in HARD_FORMS
                       if re.search(pattern, body)]
            if not reasons:
                soft += 1
                continue

            for reason in reasons:
                hard_reasons[reason] += 1
            if 'walks something' in reasons:
                for header in loop_headers(body):
                    loop_shapes[classify(header, LOOP_FORMS)] += 1
            residual.append((name, cls, base, reasons))

    hard = len(residual)
    print('SD3 census -- %d files, %d lines, %d script classes'
          % (files, lines, classes))
    print()
    print('  expressible as data : %5d  (%.1f%%)'
          % (soft, 100.0 * soft / classes))
    print('  needs C++ today     : %5d  (%.1f%%)'
          % (hard, 100.0 * hard / classes))
    print()

    total_guards = sum(guards.values())
    print('-- 1. what a guard tests (%d conditions) --' % total_guards)
    for label, count in guards.most_common():
        print('   %-24s %6d  %5.1f%%'
              % (label, count, 100.0 * count / total_guards))
    print()
    print('   terms per guard:', ', '.join(
        '%s=%d' % ('%d+' % k if k == 5 else k, v)
        for k, v in sorted(guard_terms.items())))
    print()

    print('-- 2. why a class needs C++ --')
    for label, count in hard_reasons.most_common():
        print('   %-24s %6d' % (label, count))
    print()
    total_loops = sum(loop_shapes.values())
    if total_loops:
        print('   of the loops (%d):' % total_loops)
        for label, count in loop_shapes.most_common():
            print('     %-22s %6d  %5.1f%%'
                  % (label, count, 100.0 * count / total_loops))
    print()

    print('-- 3. the residual, by base class --')
    per_base = collections.Counter(base for _f, _c, base, _r in residual)
    for base, count in per_base.most_common(12):
        print('   %-26s %4d of %4d' % (base, count, by_base[base]))

    if verbose:
        print()
        print('-- every residual class --')
        for name, cls, base, reasons in sorted(residual):
            print('   %-52s %-26s %s' % (name, cls, ', '.join(reasons)))
    else:
        print()
        print('   (--verbose lists all %d by name)' % hard)

    return 0


if __name__ == '__main__':
    sys.exit(main())
