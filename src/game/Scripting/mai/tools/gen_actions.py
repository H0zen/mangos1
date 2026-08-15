#!/usr/bin/env python3
"""Turn actions.manifest into the header MAI compiles against.

    python src/game/Scripting/mai/tools/gen_actions.py [--check]

Deliberately the same shape as the seam's gen_events.py, down to --check: one
declaration file, one generator, a committed result, and a ctest that fails
when the two drift. A second generator that worked a second way would be a
second thing to learn for no gain.

What comes out is not code that does anything -- it is the vocabulary as data:
the verb enum, and for each verb the names, types and optionality of its
parameters. The loader validates against that table, the text parser reads
parameter names from it, and the lowering from `dbscripts_on_*` targets it. All
three would otherwise be three hand-maintained copies of one list, which is
exactly the drift the event manifest was written to end.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAI = os.path.dirname(HERE)
MANIFEST = os.path.join(MAI, 'actions.manifest')
OUTPUT = os.path.join(MAI, 'MaiActions.gen.h')

# The rules use the same declaration shape -- name, number, typed parameters --
# so they come out of the same generator rather than a second copy of it. Only
# the names of the things emitted differ.
RULES = os.path.join(MAI, 'rules.manifest')
RULES_OUTPUT = os.path.join(MAI, 'MaiRules.gen.h')

# EventAI's own union does NOT have the same shape, so its mapping cannot be
# generic and has to be declared. See eventai.map's own header.
#
# NOTHING IS EMITTED FROM IT ANY MORE. The C++ half went when the engine did:
# the server reads `mai_rule` now, so the only reader left is the SQL
# conversion. It is still PARSED here, and still checked against
# actions.manifest, because that check is what fails when a parameter is
# renamed -- and the conversion is the one thing that would otherwise notice
# by writing the wrong column into twenty thousand rows.
EVENTAI = os.path.join(MAI, 'eventai.map')

# The TargetFlags value that says the creature acts ON the selected unit rather
# than the selected unit being the actor. Spelled out rather than imported:
# this generator emits a number, and MaiTargeting.h is where the name lives.
BUDDY_AS_TARGET = 0x01

# manifest type -> (C++ storage, what the loader checks it against)
TYPES = {
    'u32':        ('uint32', None),
    'u64':        ('uint64', None),
    'i32':        ('int32', None),
    'f32':        ('float', None),
    'bool':       ('bool', None),
    'ms':         ('uint32', None),
    'flags':      ('uint32', None),
    'field':      ('uint32', 'Field'),
    'spell':      ('uint32', 'Spell'),
    'creature':   ('uint32', 'Creature'),
    'gameobject': ('uint32', 'GameObject'),
    'item':       ('uint32', 'Item'),
    'quest':      ('uint32', 'Quest'),
    'faction':    ('uint32', 'Faction'),
    'map':        ('uint32', 'Map'),
    'emote':      ('uint32', 'Emote'),
    'sound':      ('uint32', 'Sound'),
    'movie':      ('uint32', 'Movie'),
    'taxi':       ('uint32', 'TaxiPath'),
    'mail':       ('uint32', 'MailTemplate'),
    'area':       ('uint32', 'Area'),
    'text':       ('int32', 'Text'),

    # A NAME in the table and a slot at run time. The only type whose value is
    # not a number on the way in, which is why the parser has to know about it
    # rather than the loader translating first: interning needs the creature it
    # belongs to, and the parser is where the creature is known.
    'state':      ('uint32', 'State'),
}


class Bad(Exception):
    pass


def camel(text):
    return ''.join(p[:1].upper() + p[1:] for p in text.split('_') if p)


def parse_param(spec, where):
    if ':' not in spec:
        raise Bad('%s: parameter %r is not name:type' % (where, spec))
    name, kind = spec.split(':', 1)
    optional = kind.endswith('?')
    kind = kind.rstrip('?')
    if kind not in TYPES:
        raise Bad('%s: unknown parameter type %r' % (where, kind))
    cxx, check = TYPES[kind]
    return name, kind, cxx, check, optional


def joined(path):
    """The file's lines, with a trailing backslash meaning "and the next one".

    Purely so the manifest can stay inside eighty columns: a verb with four
    optional parameters does not fit on one line, and the alternative -- a
    long line -- is the one thing the coding standard says not to write.
    """
    held, at = '', 0
    for lineno, raw in enumerate(open(path, encoding='utf-8'), 1):
        line = raw.split('#')[0].rstrip()
        if line.endswith('\\'):
            held += line[:-1]
            at = at or lineno
            continue
        yield (at or lineno), held + line
        held, at = '', 0
    if held:
        yield at, held


def parse(path):
    """(facets, [(category, catid, [(name, id, params, facets)])])"""
    facets, cats, cur = {}, [], None
    seen = {}
    for lineno, line in joined(path):
        if not line.strip():
            continue
        where = '%s:%d' % (path, lineno)

        facet = re.match(r'^facet\s+(\w+)\s+(.*)$', line)
        if facet:
            facets[facet.group(1)] = [parse_param(s, where)
                                      for s in facet.group(2).split()]
            continue

        head = re.match(r'^category\s+(\w+)\s*=\s*0x([0-9A-Fa-f]{2})\s*$', line)
        if head:
            cur = (head.group(1), int(head.group(2), 16), [])
            cats.append(cur)
            continue

        if cur is None:
            raise Bad('%s: an action before any category' % where)

        parts = line.split()
        if len(parts) < 2:
            raise Bad('%s: expected "name id [params]"' % where)
        name, ident = parts[0], int(parts[1])
        if ident in seen:
            raise Bad('%s: id %d is already %s -- identity is ABI, never '
                      'reuse a number' % (where, ident, seen[ident]))
        seen[ident] = name

        params, used = [], []
        for token in parts[2:]:
            if token in facets:
                used.append(token)
            else:
                params.append(parse_param(token, where))
        cur[2].append((name, ident, params, used))
    return facets, cats


def shapes(facets, cats):
    """action name -> (CamelName, [parameter names, in slot order])"""
    out = {}
    for _cat, _catid, actions in cats:
        for name, _ident, params, used in actions:
            full = [p[0] for p in params]
            for facet in used:
                full.extend(p[0] for p in facets[facet])
            out[name] = (camel(name), full)
    return out


# The shapes a column mapping cannot express. Kept as names rather than as a
# flag each, because they are not composable: a verb has one of them or none.
FORMS = ('plain', 'entry_or_model', 'summon_spawn')


def parse_map(path, known):
    """[(type, verb|None, slots, flags, pinSlot, pinValue, form, refused)]"""
    rows, seen = [], {}

    for lineno, raw in enumerate(open(path, encoding='utf-8'), 1):
        line = raw.split('#')[0].strip()
        if not line:
            continue
        where = '%s:%d' % (path, lineno)

        parts = line.split()
        keyword = parts.pop(0)

        if keyword not in ('action', 'refuse'):
            raise Bad('%s: expected "action" or "refuse", got %r'
                      % (where, keyword))

        if not parts:
            raise Bad('%s: %s needs an action type' % (where, keyword))

        try:
            kind = int(parts.pop(0))
        except ValueError:
            raise Bad('%s: action type must be a number' % where)

        if kind in seen:
            raise Bad('%s: action type %d is already mapped at line %d'
                      % (where, kind, seen[kind]))
        seen[kind] = lineno

        if keyword == 'refuse':
            if not parts:
                raise Bad('%s: refuse needs a reason' % where)
            rows.append((kind, None, [], 0, None, 0, 'plain',
                         ' '.join(parts)))
            continue

        if not parts:
            raise Bad('%s: action needs a verb' % where)

        verb = parts.pop(0)
        if verb not in known:
            raise Bad('%s: %r is not a verb in actions.manifest'
                      % (where, verb))

        _ident, names = known[verb]

        if len(parts) < 3:
            raise Bad('%s: %s needs three columns (use - for unused)'
                      % (where, verb))

        slots = []
        for column in parts[:3]:
            if column == '-':
                slots.append(None)
            elif column == 'select':
                slots.append('select')
            elif column in names:
                slots.append(names.index(column))
            else:
                # The whole point of naming parameters rather than numbering
                # slots: a rename in actions.manifest fails HERE, loudly,
                # instead of quietly moving a value one column over.
                raise Bad('%s: %s has no parameter %r -- it takes %s'
                          % (where, verb, column,
                             ', '.join(names) if names else 'none'))

        flags, pin_slot, pin_value, form = 0, None, 0, 'plain'

        for extra in parts[3:]:
            if extra == 'buddy_as_target':
                flags |= BUDDY_AS_TARGET
            elif extra.startswith('form='):
                form = extra[len('form='):]
                if form not in FORMS:
                    raise Bad('%s: unknown form %r' % (where, form))
            elif '=' in extra:
                pinned, value = extra.split('=', 1)
                if pinned not in names:
                    raise Bad('%s: %s has no parameter %r to pin'
                              % (where, verb, pinned))
                if pin_slot is not None:
                    raise Bad('%s: %s pins more than one parameter'
                              % (where, verb))
                pin_slot = names.index(pinned)
                try:
                    pin_value = int(value)
                except ValueError:
                    raise Bad('%s: %r is not a number' % (where, value))
            else:
                raise Bad('%s: unknown modifier %r' % (where, extra))

        rows.append((kind, verb, slots, flags, pin_slot, pin_value, form,
                     None))

    return rows


def emit(facets, cats, what='Action', guard='ACTIONS', source='actions'):
    # Facets are the actions' own vocabulary and are declared in their header.
    # Nothing stops rules.manifest writing `facet`, so say why it cannot rather
    # than emitting a name that does not exist.
    if what != 'Action' and facets:
        raise Bad('%s.manifest: facets belong to actions.manifest -- the Facet '
                  'enum is declared once, in MaiActions.gen.h' % source)

    head = open(os.path.join(os.path.dirname(MAI), 'ScriptTypes.h'),
                encoding='utf-8').read()
    licence = head[:head.index('#ifndef')].rstrip()

    out = [licence, '',
           '// GENERATED FROM %s.manifest -- DO NOT EDIT.' % source,
           '// Regenerate with: python src/game/Scripting/mai/tools/gen_actions.py',
           '',
           '#ifndef MANGOS_MAI_%s_GEN_H' % guard,
           '#define MANGOS_MAI_%s_GEN_H' % guard,
           '',
           '#include "Platform/Define.h"' if what == 'Action'
               else '#include "MaiActions.gen.h"',
           '',
           '#include <cstddef>',
           '',
           'namespace mai',
           '{']

    # The parameter vocabulary is one vocabulary, not one per manifest: the
    # rules header includes the actions header, so emitting these a second
    # time would redefine them rather than declare them.
    if what == 'Action':
        out.append('    /// What a parameter is, and therefore what the loader'
                   ' checks')
        out.append('    /// it against before a script is allowed to run.')
        out.append('    enum class ParamType : uint8')
        out.append('    {')
        for kind in sorted(TYPES):
            out.append('        %s,' % camel(kind))
        out.append('    };')
        out.append('')
        out.append('    struct ParamSpec')
        out.append('    {')
        out.append('        char const* name;')
        out.append('        ParamType   type;')
        out.append('        bool        optional;')
        out.append('    };')
        out.append('')

    out.append('    /// The verbs. The numbers are the DB-script command ids,')
    out.append('    /// unchanged, so an existing row lowers by number.')
    out.append('    enum class %sId : uint16' % what)
    out.append('    {')
    out.append('        None = 0xFFFF,')
    rows = []
    for cat, _catid, actions in cats:
        out.append('')
        out.append('        // %s' % cat)
        for name, ident, params, used in actions:
            out.append('        %-26s = %d,' % (camel(name), ident))
            rows.append((camel(name), name, ident, params, used))
    out.append('    };')
    out.append('')

    for ident_name, name, _id, params, used in rows:
        full = list(params)
        for facet in used:
            full.extend(facets[facet])
        if not full:
            continue
        out.append('    inline constexpr ParamSpec g_%sParams%s[] =' % (what.lower(), ident_name))
        out.append('    {')
        for pname, kind, _cxx, _check, optional in full:
            out.append('        { "%s", ParamType::%s, %s },'
                       % (pname, camel(kind), 'true' if optional else 'false'))
        out.append('    };')
        out.append('')

    if what == 'Action':
        out.append('    /// Which shared facets an action carries, and therefore')
        out.append('    /// where its own parameters stop and theirs begin. The')
        out.append('    /// lowering from `dbscripts_on_*` reads this instead of')
        out.append('    /// having a case per verb: a DB row is two datalongs plus')
        out.append('    /// exactly these facets, so knowing which ones apply is')
        out.append('    /// the whole of the mapping.')
        out.append('    enum Facet : uint8')
        out.append('    {')
        for i, facet in enumerate(sorted(facets)):
            out.append('        Facet%-10s = 1 << %d,' % (camel(facet), i))
        out.append('    };')
        out.append('')
    out.append('    struct %sSpec' % what)
    out.append('    {')
    out.append('        %-17s id;' % (what + 'Id'))
    out.append('        char const*       name;       ///< "cast_spell"')
    out.append('        ParamSpec const*  params;')
    out.append('        std::size_t       arity;      ///< own parameters + facets')
    out.append('        std::size_t       own;        ///< how many are the verb\'s own')
    out.append('        uint8             facets;     ///< a mask of Facet')
    out.append('    };')
    out.append('')
    out.append('    inline constexpr %sSpec g_%sSpecs[] =' % (what, what.lower()))
    out.append('    {')
    for ident_name, name, _id, params, used in rows:
        count = len(params) + sum(len(facets[f]) for f in used)
        mask = ' | '.join('Facet' + camel(f) for f in used) or '0'
        out.append('        { %sId::%s, "%s", %s, %d, %d, %s },'
                   % (what, ident_name, name,
                      ('g_%sParams%s' % (what.lower(), ident_name)) if count else 'nullptr',
                      count, len(params), mask))
    out.append('    };')
    out.append('')
    out.append('    /// The shape of @a id, or nullptr when nothing carries it.')
    out.append('    inline %sSpec const* SpecOf(%sId id)' % (what, what))
    out.append('    {')
    out.append('        for (%sSpec const& spec : g_%sSpecs)' % (what, what.lower()))
    out.append('        {')
    out.append('            if (spec.id == id)')
    out.append('            {')
    out.append('                return &spec;')
    out.append('            }')
    out.append('        }')
    out.append('')
    out.append('        return nullptr;')
    out.append('    }')
    out.append('}')
    out.append('')
    out.append('#endif //MANGOS_MAI_%s_GEN_H' % guard)
    return '\n'.join(out) + '\n', len(rows)


def main():
    try:
        facets, cats = parse(MANIFEST)
        text, count = emit(facets, cats)
        rule_facets, rule_cats = parse(RULES)
        rules, rule_count = emit(rule_facets, rule_cats, 'Rule', 'RULES',
                                 'rules')
        known = shapes(facets, cats)
        # Parsed for its own sake: nothing is emitted, but a parameter renamed
        # in actions.manifest fails HERE rather than in the next conversion.
        mapping = parse_map(EVENTAI, known)
    except Bad as err:
        sys.stderr.write('error: %s\n' % err)
        return 1

    written = ((OUTPUT, text), (RULES_OUTPUT, rules))

    if '--check' in sys.argv:
        for path, wanted in written:
            old = (open(path, encoding='utf-8').read()
                   if os.path.exists(path) else None)
            if old != wanted:
                sys.stderr.write('error: %s is stale -- regenerate it\n'
                                 % path)
                return 1
        print('MaiActions.gen.h and MaiRules.gen.h are up to date; '
              'eventai.map names %d action type(s)' % len(mapping))
        return 0

    for path, wanted in written:
        open(path, 'w', encoding='utf-8', newline='\n').write(wanted)
    print('%s: %d categories, %d actions' % (MANIFEST, len(cats), count))
    print('%s: %d categories, %d rules' % (RULES, len(rule_cats), rule_count))
    print('%s: %d action type(s) mapped' % (EVENTAI, len(mapping)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
