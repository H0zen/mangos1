#!/usr/bin/env python3
"""Generate the seam's two derived files from events.manifest.

The manifest is the source of truth; this script is the only thing that turns
it into C++. Run it after editing the manifest and commit the result, so the
build never needs Python:

    python src/game/Scripting/tools/gen_events.py

CI regenerates and diffs, so a manifest edit that was not regenerated fails
there rather than drifting quietly.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SEAM = os.path.dirname(HERE)
MANIFEST = os.path.join(SEAM, 'events.manifest')
OUTPUT = os.path.join(SEAM, 'ScriptEvents.gen.h')
STUBS = os.path.join(SEAM, 'events.d.luau')

# manifest type -> (C++ member type, Arg factory, Arg accessor, writes back)
SCALARS = {
    'u32':  ('uint32', 'FromNumber', 'AsNumber', True),
    'i32':  ('int32', 'FromSigned', 'AsSigned', True),
    'u64':  ('uint64', 'FromNumber', 'AsNumber', True),
    'i64':  ('int64', 'FromSigned', 'AsSigned', True),
    'f64':  ('double', 'FromReal', 'AsReal', True),
    'bool': ('bool', 'FromFlag', 'AsFlag', True),
    'enum': ('uint32', 'FromNumber', 'AsNumber', True),
}

DOMAIN = {
    'guild': 'Guild', 'group': 'Group', 'quest': 'Quest', 'map': 'Map',
    'bg': 'BattleGround', 'channel': 'Channel', 'auction': 'Auction',
    'ahouse': 'AuctionHouse', 'itemtpl': 'ItemTemplate', 'spellinfo': 'SpellInfo',
    'areatrigger': 'AreaTrigger', 'weather': 'Weather', 'spell': 'Spell',
    'aura': 'Aura', 'auraeffect': 'AuraEffect', 'packet': 'Packet',
    'casttargets': 'CastTargets', 'proc': 'Proc', 'damage': 'Damage',
    'dispel': 'Dispel', 'spelldest': 'SpellDestination', 'session': 'Session',
    'objlist': 'ObjectList', 'objslot': 'ObjectSlot',
}

POLICIES = ('broadcast', 'cancel', 'claim', 'role')


class Bad(Exception):
    pass


def camel(text):
    return ''.join(p[:1].upper() + p[1:] for p in text.split('_') if p)


def parse(path):
    """Return [(category, catid, [(name, localid, policy, [args])])]."""
    cats, cur = [], None
    seen_ids = {}
    for lineno, raw in enumerate(open(path, encoding='utf-8'), 1):
        line = raw.split('#')[0].rstrip()
        if not line.strip():
            continue
        head = re.match(r'^category\s+(\w+)\s*=\s*0x([0-9A-Fa-f]{2})\s*$', line)
        if head:
            cur = (head.group(1), int(head.group(2), 16), [])
            if cur[1] == 0:
                raise Bad('%s:%d: category id 0 is reserved for EventId::None'
                          % (path, lineno))
            cats.append(cur)
            continue
        if cur is None:
            raise Bad('%s:%d: entry before any category' % (path, lineno))
        parts = line.split()
        if len(parts) < 3:
            raise Bad('%s:%d: expected "name id policy [payload]"' % (path, lineno))
        name, local, policy, payload = parts[0], int(parts[1]), parts[2], parts[3:]
        if policy not in POLICIES:
            raise Bad('%s:%d: unknown policy %r' % (path, lineno, policy))
        if not 0 <= local <= 0xFF:
            raise Bad('%s:%d: local id %d out of range' % (path, lineno, local))
        key = (cur[1], local)
        if key in seen_ids:
            raise Bad('%s:%d: id 0x%02X%02X already taken by %s -- identity is '
                      'ABI, never reuse a number'
                      % (path, lineno, cur[1], local, seen_ids[key]))
        seen_ids[key] = name
        cur[2].append((name, local, policy, payload))
    return cats


# manifest type -> (Arg::Kind, Luau type shown in the editor stubs)
RUNTIME = {
    'u32': ('Number', 'number'), 'i32': ('Signed', 'number'),
    'u64': ('Number', 'number'), 'i64': ('Signed', 'number'),
    'f64': ('Real', 'number'), 'bool': ('Flag', 'boolean'),
    'enum': ('Number', 'number'), 'obj': ('Entity', 'Guid'),
    'str': ('Text', 'string'),
}


# The slot a payload-less event still carries.
#
# An event with nothing to say about itself would want an arity of zero, and
# C++ has no zero-length array to Pack into -- so the struct declares one slot
# holding an empty Ref. The run-time table has to agree, and for a while it did
# not: build() rounded the arity up to one and describe() left it at zero, so
# LuauEngine's `spec->arity != count` rejected every payload-less event before
# it reached a single handler. Both views are built from this one row now.
SUBJECT = ('subject', 'Ref', 'FromEntity', 'AsEntity', False, False,
           'Entity', 'None', 'Guid')


def payload_args(payload, where):
    """The declared slots, or the placeholder that stands in for none."""
    args = [a for a in (parse_arg(spec, where) for spec in payload) if a]
    return args if args else [SUBJECT]


def parse_arg(spec, where):
    """(name, cxx, make, read, inout, is_text, kind, domain, luau)."""
    if spec in ('-', '?'):
        return None
    if ':' not in spec:
        raise Bad('%s: argument %r is not name:type' % (where, spec))
    name, kind = spec.split(':', 1)
    inout = kind.endswith('!')
    kind = kind.rstrip('!')
    if kind == 'obj':
        return (name, 'Ref', 'FromEntity', 'AsEntity', inout, False,
                'Entity', 'None', 'Guid')
    if kind == 'str':
        return (name, 'std::string&', 'FromText', None, inout, True,
                'Text', 'None', 'string')
    if kind.startswith('h.') or kind.startswith('b.'):
        dom = kind[2:]
        if dom not in DOMAIN:
            raise Bad('%s: unknown domain %r' % (where, dom))
        if kind[0] == 'h':
            return (name, 'Handle', 'FromNamed', 'AsNamed', inout, False,
                    'Named', DOMAIN[dom], 'Handle')
        return (name, 'Borrow', 'FromLent', 'AsLent', inout, False,
                'Lent', DOMAIN[dom], 'Borrow')
    if kind in SCALARS:
        cxx, make, read, _ = SCALARS[kind]
        rt, luau = RUNTIME[kind]
        return (name, cxx, make, read, inout, False, rt, 'None', luau)
    raise Bad('%s: unknown payload type %r' % (where, kind))


def emit(cats):
    head = open(os.path.join(SEAM, 'ScriptTypes.h'), encoding='utf-8').read()
    licence = head[:head.index('#ifndef')].rstrip()

    out = [licence, '']
    out.append('// GENERATED FROM events.manifest -- DO NOT EDIT.')
    out.append('// Regenerate with: python src/game/Scripting/tools/gen_events.py')
    out.append('')
    out.append('#ifndef MANGOS_SCRIPT_EVENTS_GEN_H')
    out.append('#define MANGOS_SCRIPT_EVENTS_GEN_H')
    out.append('')
    out.append('#include "ScriptTypes.h"')
    out.append('')
    out.append('namespace scripting')
    out.append('{')
    out.append('    /// EventId = (category << 8) | local id. The local ids are the')
    out.append('    /// engines\' own numbers, so a script that registers by number')
    out.append('    /// keeps working. Holes are retired events; never fill one in.')
    out.append('    enum class EventId : uint16')
    out.append('    {')
    out.append('        None = 0,')

    structs, counts = [], {'event': 0, 'role': 0, 'pending': 0}
    for cat, catid, rows in cats:
        out.append('')
        out.append('        // %s' % cat)
        for name, local, policy, payload in rows:
            ident = camel(cat) + camel(name[3:] if name.startswith('on_') else name)
            out.append('        %-44s = 0x%02X%02X,' % (ident, catid, local))
            if policy == 'role':
                counts['role'] += 1
                continue
            if payload == ['?']:
                counts['pending'] += 1
                continue
            counts['event'] += 1
            structs.append(build(ident, policy, payload, cat, name))
    out.append('    };')
    out.append('')
    out.extend(structs)
    out.extend(describe(cats))
    out.append('}')
    out.append('')
    out.append('#endif //MANGOS_SCRIPT_EVENTS_GEN_H')
    return '\n'.join(out) + '\n', counts


def describe(cats):
    """The same events again, as data an engine can walk at run time.

    A struct per event is what a CALL SITE wants: named fields, checked by the
    compiler, no lookup at all. An interpreter wants the opposite -- it is
    handed an id and an array of Args and has to build something a script can
    read, so it needs the names and the kinds while it runs. Both come from
    this one manifest, which is the whole point: a script says `e.amount`
    because the manifest calls the slot amount, and the C++ site sets
    `.amount` for exactly the same reason.
    """
    lines = ['    // -- the same events as run-time data; see describe() in',
             '    //    tools/gen_events.py for why both forms exist.',
             '',
             '    struct ArgSpec',
             '    {',
             '        char const* name;',
             '        Arg::Kind   kind;',
             '        Domain      domain;   ///< None unless Named or Lent',
             '        bool        inout;',
             '    };',
             '',
             '    struct EventSpec',
             '    {',
             '        EventId        id;',
             '        char const*    name;        ///< "player.on_login"',
             '        ArgSpec const* args;',
             '        std::size_t    arity;',
             '        bool           cancellable;',
             '        bool           claimable;',
             '    };',
             '']

    rows = []
    for cat, _catid, entries in cats:
        for name, _local, policy, payload in entries:
            if policy == 'role' or payload == ['?']:
                continue
            ident = camel(cat) + camel(name[3:] if name.startswith('on_')
                                       else name)
            where = '%s/%s' % (cat, name)
            args = payload_args(payload, where)
            lines.append('    inline constexpr ArgSpec g_args%s[] =' % ident)
            lines.append('    {')
            for a in args:
                lines.append('        { "%s", Arg::Kind::%s, Domain::%s, %s },'
                             % (a[0], a[6], a[7],
                                'true' if a[4] else 'false'))
            lines.append('    };')
            lines.append('')
            rows.append((ident, '%s.%s' % (cat, name), len(args), policy))

    lines.append('    inline constexpr EventSpec g_eventSpecs[] =')
    lines.append('    {')
    for ident, full, arity, policy in rows:
        lines.append('        { EventId::%s, "%s", g_args%s, %d, %s, %s },'
                     % (ident, full, ident, arity,
                        'true' if policy == 'cancel' else 'false',
                        'true' if policy == 'claim' else 'false'))
    lines.append('    };')
    lines.append('')
    lines.append('    /// The shape of @a id, or nullptr when nothing carries it.')
    lines.append('    inline EventSpec const* SpecOf(EventId id)')
    lines.append('    {')
    lines.append('        for (EventSpec const& spec : g_eventSpecs)')
    lines.append('        {')
    lines.append('            if (spec.id == id)')
    lines.append('            {')
    lines.append('                return &spec;')
    lines.append('            }')
    lines.append('        }')
    lines.append('')
    lines.append('        return nullptr;')
    lines.append('    }')
    lines.append('')
    return lines


def build(ident, policy, payload, cat, name):
    where = '%s/%s' % (cat, name)
    args = payload_args(payload, where)
    empty = args == [SUBJECT]
    lines = []
    lines.append('    /// %s: %s' % (where, policy))
    lines.append('    struct %s' % ident)
    lines.append('    {')
    lines.append('        static constexpr EventId Id = EventId::%s;' % ident)
    lines.append('        static constexpr std::size_t Arity = %d;' % len(args))
    lines.append('        static constexpr bool Cancellable = %s;'
                 % ('true' if policy == 'cancel' else 'false'))
    lines.append('        static constexpr bool Claimable = %s;'
                 % ('true' if policy == 'claim' else 'false'))
    lines.append('')
    for aname, cxx, _, _, inout, _, _, _, _ in args:
        if empty:
            note = '    ///< placeholder; this event has no payload'
        else:
            note = '    ///< in/out' if inout else ''
        lines.append('        %-14s %s;%s' % (cxx, aname, note))
    lines.append('')
    lines.append('        void Pack(Arg* args) const')
    lines.append('        {')
    for i, (aname, _, make, _, _, _, _, _, _) in enumerate(args):
        lines.append('            args[%d] = Arg::%s(%s);' % (i, make, aname))
    lines.append('        }')
    lines.append('')
    writes = [(i, a) for i, a in enumerate(args) if a[4] and not a[5]]
    if writes:
        lines.append('        void Unpack(Arg const* args)')
        lines.append('        {')
        for i, (aname, cxx, _, read, _, _, _, _, _) in writes:
            cast = '' if cxx in ('Ref', 'Handle', 'Borrow') else 'static_cast<%s>' % cxx
            lines.append('            %s = %s(args[%d].%s());'
                         % (aname, cast, i, read))
        lines.append('        }')
    else:
        lines.append('        void Unpack(Arg const*) {}')
    lines.append('    };')
    lines.append('')
    return '\n'.join(lines)


def emit_luau(cats):
    """The editor's view of the same manifest.

    Luau reads definition files natively, so this is not decoration: a script
    that annotates its handler gets the payload's fields checked against the
    manifest, and a renamed slot becomes a type error in the author's editor
    instead of a nil at run time.
    """
    out = ['--!strict',
           '-- GENERATED FROM events.manifest -- DO NOT EDIT.',
           '-- Regenerate with: python src/game/Scripting/tools/gen_events.py',
           '',
           '-- A guid is 64 bits and a Lua number holds 53, so one crosses as',
           '-- an opaque box: comparable and printable, never rounded.',
           'export type Guid = userdata',
           'export type Handle = userdata',
           '',
           '-- Valid only while the call that lent it is on the stack. Ask.',
           'export type Borrow = { IsLive: (Borrow) -> boolean }',
           '']

    ids = []
    for cat, _catid, entries in cats:
        for name, _local, policy, payload in entries:
            if policy == 'role' or payload == ['?']:
                continue
            ident = camel(cat) + camel(name[3:] if name.startswith('on_')
                                       else name)
            where = '%s/%s' % (cat, name)
            args = payload_args(payload, where)
            fields = ', '.join('%s: %s' % (a[0], a[8]) for a in args)
            out.append('-- %s.%s (%s)' % (cat, name, policy))
            out.append('export type %s = { %s }' % (ident, fields))
            ids.append('%s_%s' % (cat, name))
    out.append('')

    out.append('declare EVENT: {')
    for full in ids:
        out.append('    %s: number,' % full)
    out.append('}')
    out.append('')
    out.append('-- The payload is typed by the event, which one call cannot')
    out.append('-- express; annotate the handler to get the fields checked:')
    out.append('--     OnEvent(EVENT.player_on_login, function(e: PlayerLogin)')
    out.append('declare function OnEvent(id: number,')
    out.append('    handler: (payload: any) -> boolean?): ()')
    return '\n'.join(out) + '\n'


def main():
    try:
        cats = parse(MANIFEST)
        text, counts = emit(cats)
        stubs = emit_luau(cats)
    except Bad as err:
        sys.stderr.write('error: %s\n' % err)
        return 1
    check = '--check' in sys.argv
    old = open(OUTPUT, encoding='utf-8').read() if os.path.exists(OUTPUT) else None
    old_stubs = (open(STUBS, encoding='utf-8').read()
                 if os.path.exists(STUBS) else None)
    if check:
        if old != text:
            sys.stderr.write('error: %s is stale -- regenerate it\n' % OUTPUT)
            return 1
        if old_stubs != stubs:
            sys.stderr.write('error: %s is stale -- regenerate it\n' % STUBS)
            return 1
        print('ScriptEvents.gen.h and events.d.luau are up to date')
        return 0
    open(STUBS, 'w', encoding='utf-8', newline='\n').write(stubs)
    open(OUTPUT, 'w', encoding='utf-8', newline='\n').write(text)
    total = sum(len(r) for _, _, r in cats)
    print('%s: %d categories, %d ids' % (MANIFEST, len(cats), total))
    print('%s: %d event structs, %d role entries skipped, %d awaiting a payload'
          % (OUTPUT, counts['event'], counts['role'], counts['pending']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
