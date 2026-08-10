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

# manifest type -> (C++ storage, what the loader checks it against)
TYPES = {
    'u32':        ('uint32', None),
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


def parse(path):
    """(facets, [(category, catid, [(name, id, params, facets)])])"""
    facets, cats, cur = {}, [], None
    seen = {}
    for lineno, raw in enumerate(open(path, encoding='utf-8'), 1):
        line = raw.split('#')[0].rstrip()
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


def emit(facets, cats):
    head = open(os.path.join(os.path.dirname(MAI), 'ScriptTypes.h'),
                encoding='utf-8').read()
    licence = head[:head.index('#ifndef')].rstrip()

    out = [licence, '',
           '// GENERATED FROM actions.manifest -- DO NOT EDIT.',
           '// Regenerate with: python src/game/Scripting/mai/tools/gen_actions.py',
           '',
           '#ifndef MANGOS_MAI_ACTIONS_GEN_H',
           '#define MANGOS_MAI_ACTIONS_GEN_H',
           '',
           '#include "Platform/Define.h"',
           '',
           '#include <cstddef>',
           '',
           'namespace mai',
           '{',
           '    /// What a parameter is, and therefore what the loader checks',
           '    /// it against before a script is allowed to run.',
           '    enum class ParamType : uint8',
           '    {']
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
    out.append('    enum class ActionId : uint16')
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
        out.append('    inline constexpr ParamSpec g_params%s[] =' % ident_name)
        out.append('    {')
        for pname, kind, _cxx, _check, optional in full:
            out.append('        { "%s", ParamType::%s, %s },'
                       % (pname, camel(kind), 'true' if optional else 'false'))
        out.append('    };')
        out.append('')

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
    out.append('    struct ActionSpec')
    out.append('    {')
    out.append('        ActionId          id;')
    out.append('        char const*       name;       ///< "cast_spell"')
    out.append('        ParamSpec const*  params;')
    out.append('        std::size_t       arity;      ///< own parameters + facets')
    out.append('        std::size_t       own;        ///< how many are the verb\'s own')
    out.append('        uint8             facets;     ///< a mask of Facet')
    out.append('    };')
    out.append('')
    out.append('    inline constexpr ActionSpec g_actionSpecs[] =')
    out.append('    {')
    for ident_name, name, _id, params, used in rows:
        count = len(params) + sum(len(facets[f]) for f in used)
        mask = ' | '.join('Facet' + camel(f) for f in used) or '0'
        out.append('        { ActionId::%s, "%s", %s, %d, %d, %s },'
                   % (ident_name, name,
                      ('g_params%s' % ident_name) if count else 'nullptr',
                      count, len(params), mask))
    out.append('    };')
    out.append('')
    out.append('    /// The shape of @a id, or nullptr when nothing carries it.')
    out.append('    inline ActionSpec const* SpecOf(ActionId id)')
    out.append('    {')
    out.append('        for (ActionSpec const& spec : g_actionSpecs)')
    out.append('        {')
    out.append('            if (spec.id == id)')
    out.append('            {')
    out.append('                return &spec;')
    out.append('            }')
    out.append('        }')
    out.append('')
    out.append('        return nullptr;')
    out.append('    }')
    out.append('')
    out.append('    /// The verb @a name names, or ActionId::None.')
    out.append('    inline ActionId ActionNamed(char const* name);')
    out.append('}')
    out.append('')
    out.append('#endif //MANGOS_MAI_ACTIONS_GEN_H')
    return '\n'.join(out) + '\n', len(rows)


def main():
    try:
        facets, cats = parse(MANIFEST)
        text, count = emit(facets, cats)
    except Bad as err:
        sys.stderr.write('error: %s\n' % err)
        return 1

    old = (open(OUTPUT, encoding='utf-8').read()
           if os.path.exists(OUTPUT) else None)
    if '--check' in sys.argv:
        if old != text:
            sys.stderr.write('error: %s is stale -- regenerate it\n' % OUTPUT)
            return 1
        print('MaiActions.gen.h is up to date')
        return 0

    open(OUTPUT, 'w', encoding='utf-8', newline='\n').write(text)
    print('%s: %d categories, %d actions' % (MANIFEST, len(cats), count))
    return 0


if __name__ == '__main__':
    sys.exit(main())
