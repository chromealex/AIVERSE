#!/usr/bin/env python3
"""aiverse_protoc: proto3 -> C++20 code generator (stdlib only, no protoc / libprotobuf).

Generates plain structs with encode/decode over engine/grpc/pb/Pb.h and, with --grpc,
typed gRPC client stubs over engine/grpc/Grpc.h.

  aiverse_protoc.py --out DIR [-I DIR ...] [--grpc] [--check] file.proto [...]

Output per input `dir/name.proto`: DIR/dir/name.pb.h, DIR/dir/name.pb.cpp and (with --grpc;
always, so build systems know the outputs) DIR/dir/name.grpc.h. Paths are relative to the first -I root
that contains the file. Output is byte-stable (no timestamps), so it can be checked in.

Supported: proto3 scalars, string/bytes, enums (open: unknown values kept), nested messages and
enums, oneof, map<k,v>, repeated (packed + unpacked on decode), proto3 `optional`, imports,
services (unary, client/server/bidi streaming). Unknown fields are skipped on decode.
Not supported (readable error): proto2, groups, extensions, recursive messages.
"""
import argparse
import os
import re
import sys

SCALARS = {
    # proto type: (C++ type, wire type, Writer method, Reader method)
    'double': ('double', 1, 'f64', 'f64'),
    'float': ('float', 5, 'f32', 'f32'),
    'int32': ('int32_t', 0, 'int32', 'int32'),
    'int64': ('int64_t', 0, 'int64', 'int64'),
    'uint32': ('uint32_t', 0, 'uint32', 'uint32'),
    'uint64': ('uint64_t', 0, 'uint64', 'uint64'),
    'sint32': ('int32_t', 0, 'sint32', 'sint32'),
    'sint64': ('int64_t', 0, 'sint64', 'sint64'),
    'fixed32': ('uint32_t', 5, 'fixed32', 'fixed32'),
    'fixed64': ('uint64_t', 1, 'fixed64', 'fixed64'),
    'sfixed32': ('int32_t', 5, 'sfixed32', 'sfixed32'),
    'sfixed64': ('int64_t', 1, 'sfixed64', 'sfixed64'),
    'bool': ('bool', 0, 'boolean', 'boolean'),
    'string': ('std::string', 2, 'string', 'string'),
    'bytes': ('std::string', 2, 'string', 'string'),
}
CPP_KEYWORDS = set('''alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t
char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await
co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false
float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or
or_eq private protected public register reinterpret_cast requires return short signed sizeof static
static_assert static_cast struct switch template this thread_local throw true try typedef typeid
typename union unsigned using virtual void volatile wchar_t while xor xor_eq NULL EOF errno assert
encode decode serialize parse None'''.split())


class ProtoError(Exception):
    pass


def ident(name):
    return name + '_' if name in CPP_KEYWORDS else name


def camel(name):
    return ''.join(x[:1].upper() + x[1:] for x in name.split('_') if x)


TOKEN = re.compile(r'''
    (?P<ws>\s+) | (?P<lc>//[^\n]*) | (?P<bc>/\*.*?\*/) |
    (?P<str>"(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*') |
    (?P<num>[-+]?(?:0[xX][0-9a-fA-F]+|\d+(?:\.\d*)?(?:[eE][-+]?\d+)?)) |
    (?P<id>\.?[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*) |
    (?P<sym>[{}()\[\];=,<>:.-])
''', re.X | re.S)


def tokenize(text, path):
    out, pos, line = [], 0, 1
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise ProtoError(f'{path}:{line}: unexpected character {text[pos]!r}')
        kind = m.lastgroup
        val = m.group(kind)
        if kind not in ('ws', 'lc', 'bc'):
            out.append((kind, val, line))
        line += val.count('\n')
        pos = m.end()
    return out


class Field:
    def __init__(self, name, number, type_name, label, line):
        self.name, self.number, self.type_name, self.label, self.line = name, number, type_name, label, line
        self.oneof = None      # oneof name
        self.map = None        # (key type, value type name)
        self.resolved = None   # ('scalar', t) | ('enum', Enum) | ('message', Message)
        self.map_value = None  # resolved map value


class Enum:
    def __init__(self, name, parent, file):
        self.name, self.parent, self.file = name, parent, file
        self.values = []


class Message:
    def __init__(self, name, parent, file):
        self.name, self.parent, self.file = name, parent, file
        self.fields, self.messages, self.enums, self.oneofs = [], [], [], []


class Method:
    def __init__(self, name, req, resp, cstream, sstream):
        self.name, self.req, self.resp, self.cstream, self.sstream = name, req, resp, cstream, sstream
        self.req_t = self.resp_t = None


class Service:
    def __init__(self, name, file):
        self.name, self.file, self.methods = name, file, []


class ProtoFile:
    def __init__(self, path, rel):
        self.path, self.rel = path, rel
        self.package, self.imports = '', []
        self.messages, self.enums, self.services = [], [], []


class Parser:
    def __init__(self, tokens, f):
        self.t, self.i, self.f = tokens, 0, f

    def err(self, msg):
        line = self.t[min(self.i, len(self.t) - 1)][2] if self.t else 0
        raise ProtoError(f'{self.f.rel}:{line}: {msg}')

    def peek(self):
        return self.t[self.i][1] if self.i < len(self.t) else None

    def next(self):
        if self.i >= len(self.t):
            self.err('unexpected end of file')
        tok = self.t[self.i]
        self.i += 1
        return tok

    def expect(self, val):
        tok = self.next()
        if tok[1] != val:
            self.i -= 1
            self.err(f"expected '{val}', got '{tok[1]}'")
        return tok

    def _kind(self, kind, what):
        tok = self.next()
        if tok[0] != kind:
            self.i -= 1
            self.err(f"expected {what}, got '{tok[1]}'")
        return tok[1]

    def ident(self):
        return self._kind('id', 'identifier')

    def number(self):
        return int(self._kind('num', 'number'), 0)

    def string(self):
        return self._kind('str', 'string')[1:-1]

    def skip_balanced(self, open_, close):
        depth = 0
        while True:
            v = self.next()[1]
            if v == open_:
                depth += 1
            elif v == close:
                depth -= 1
                if depth == 0:
                    return

    def skip_option(self):
        # after 'option': name = constant-or-aggregate ;
        while self.peek() != '=':
            self.next()
        self.expect('=')
        if self.peek() == '{':
            self.skip_balanced('{', '}')
        else:
            self.next()
        self.expect(';')

    def skip_field_options(self):
        if self.peek() == '[':
            self.skip_balanced('[', ']')

    def skip_statement(self):
        while self.next()[1] != ';':
            pass

    def parse_file(self):
        f = self.f
        if self.peek() != 'syntax':
            self.err("missing 'syntax = \"proto3\";' (proto2 is not supported)")
        self.next()
        self.expect('=')
        syn = self.string()
        self.expect(';')
        if syn != 'proto3':
            self.err(f"only proto3 is supported, got syntax '{syn}'")
        while self.i < len(self.t):
            kw = self.peek()
            if kw == 'package':
                self.next()
                f.package = self.ident()
                self.expect(';')
            elif kw == 'import':
                self.next()
                if self.peek() in ('public', 'weak'):
                    self.next()
                f.imports.append(self.string())
                self.expect(';')
            elif kw == 'option':
                self.next()
                self.skip_option()
            elif kw == 'message':
                f.messages.append(self.parse_message(None))
            elif kw == 'enum':
                f.enums.append(self.parse_enum(None))
            elif kw == 'service':
                f.services.append(self.parse_service())
            elif kw == 'extend':
                self.err('extensions are not supported')
            elif kw == ';':
                self.next()
            else:
                self.err(f"unexpected '{kw}' at top level")

    def parse_enum(self, parent):
        self.expect('enum')
        e = Enum(self.ident(), parent, self.f)
        self.expect('{')
        while self.peek() != '}':
            if self.peek() == 'option':
                self.next()
                self.skip_option()
            elif self.peek() == 'reserved':
                self.skip_statement()
            elif self.peek() == ';':
                self.next()
            else:
                name = self.ident()
                self.expect('=')
                num = self.number()
                self.skip_field_options()
                self.expect(';')
                e.values.append((name, num))
        self.expect('}')
        if not e.values or e.values[0][1] != 0:
            self.err(f"enum '{e.name}': the first value must be 0 in proto3")
        return e

    def parse_field(self, label, oneof=None):
        line = self.t[self.i][2]
        if self.peek() == 'map':
            self.next()
            self.expect('<')
            kt = self.ident()
            self.expect(',')
            vt = self.ident()
            self.expect('>')
            name = self.ident()
            self.expect('=')
            num = self.number()
            self.skip_field_options()
            self.expect(';')
            if kt not in SCALARS or kt in ('double', 'float', 'bytes'):
                self.err(f"map '{name}': invalid key type '{kt}'")
            fld = Field(name, num, None, 'map', line)
            fld.map = (kt, vt)
            return fld
        type_name = self.ident()
        if type_name == 'group':
            self.err('groups are not supported')
        name = self.ident()
        self.expect('=')
        num = self.number()
        self.skip_field_options()
        self.expect(';')
        fld = Field(name, num, type_name, label, line)
        fld.oneof = oneof
        return fld

    def parse_message(self, parent):
        self.expect('message')
        m = Message(self.ident(), parent, self.f)
        self.expect('{')
        while self.peek() != '}':
            kw = self.peek()
            if kw == 'message':
                m.messages.append(self.parse_message(m))
            elif kw == 'enum':
                m.enums.append(self.parse_enum(m))
            elif kw == 'option':
                self.next()
                self.skip_option()
            elif kw == 'reserved':
                self.skip_statement()
            elif kw in ('extensions', 'extend'):
                self.err('extensions are not supported')
            elif kw == 'required':
                self.err("'required' is proto2 only")
            elif kw == 'oneof':
                self.next()
                oname = self.ident()
                m.oneofs.append(oname)
                self.expect('{')
                while self.peek() != '}':
                    if self.peek() == 'option':
                        self.next()
                        self.skip_option()
                    else:
                        m.fields.append(self.parse_field('oneof', oname))
                self.expect('}')
            elif kw == ';':
                self.next()
            elif kw in ('repeated', 'optional'):
                self.next()
                m.fields.append(self.parse_field(kw))
            else:
                m.fields.append(self.parse_field('single'))
        self.expect('}')
        nums, names = {}, set()
        for fl in m.fields:
            where = f"{self.f.rel}:{fl.line}: message '{m.name}'"
            if fl.number in nums:
                raise ProtoError(f"{where}: field number {fl.number} used by '{nums[fl.number]}' and '{fl.name}'")
            if not (1 <= fl.number <= 536870911) or 19000 <= fl.number <= 19999:
                raise ProtoError(f"{where}: invalid field number {fl.number}")
            if fl.name in names:
                raise ProtoError(f"{where}: duplicate field '{fl.name}'")
            nums[fl.number] = fl.name
            names.add(fl.name)
        return m

    def parse_service(self):
        self.expect('service')
        s = Service(self.ident(), self.f)
        self.expect('{')
        while self.peek() != '}':
            if self.peek() == 'option':
                self.next()
                self.skip_option()
                continue
            if self.peek() == ';':
                self.next()
                continue
            self.expect('rpc')
            name = self.ident()
            self.expect('(')
            cs = self.peek() == 'stream'
            if cs:
                self.next()
            req = self.ident()
            self.expect(')')
            self.expect('returns')
            self.expect('(')
            ss = self.peek() == 'stream'
            if ss:
                self.next()
            resp = self.ident()
            self.expect(')')
            if self.peek() == '{':
                self.skip_balanced('{', '}')
            else:
                self.expect(';')
            s.methods.append(Method(name, req, resp, cs, ss))
        self.expect('}')
        return s


def chain(t):
    parts = []
    while t is not None:
        parts.append(t.name)
        t = t.parent
    return list(reversed(parts))


def proto_name(t):
    pkg = t.file.package
    return '.'.join(([pkg] if pkg else []) + chain(t))


def cpp_name(t):
    pkg = t.file.package
    ns = '::' + '::'.join(pkg.split('.')) if pkg else ''
    return ns + '::' + '::'.join(chain(t))


class Registry:
    def __init__(self):
        self.types = {}

    def add_file(self, f):
        def walk(items):
            for it in items:
                full = proto_name(it)
                if full in self.types:
                    raise ProtoError(f"{f.rel}: '{full}' is already defined")
                self.types[full] = it
                if isinstance(it, Message):
                    walk(it.messages)
                    walk(it.enums)
        walk(f.messages)
        walk(f.enums)

    def resolve(self, name, scope, where):
        if name.startswith('.'):
            t = self.types.get(name[1:])
            if t:
                return t
        else:
            first = name.split('.')[0]
            for i in range(len(scope), -1, -1):
                # protobuf resolves the first component innermost-first, then the full name in that scope
                if '.'.join(scope[:i] + [first]) in self.types or '.'.join(scope[:i] + [name]) in self.types:
                    cand = '.'.join(scope[:i] + [name])
                    if cand in self.types:
                        return self.types[cand]
        raise ProtoError(f"{where}: unknown type '{name}'")


def resolve_file(f, reg):
    def res_type(tn, scope, where):
        if tn in SCALARS:
            return ('scalar', tn)
        t = reg.resolve(tn, scope, where)
        return ('enum', t) if isinstance(t, Enum) else ('message', t)

    def walk(m):
        scope = proto_name(m).split('.')
        for fl in m.fields:
            where = f'{f.rel}:{fl.line}'
            if fl.map:
                fl.resolved = ('scalar', fl.map[0])
                fl.map_value = res_type(fl.map[1], scope, where)
            else:
                fl.resolved = res_type(fl.type_name, scope, where)
        for sub in m.messages:
            walk(sub)
    for m in f.messages:
        walk(m)
    pkg_scope = f.package.split('.') if f.package else []
    for s in f.services:
        for md in s.methods:
            where = f'{f.rel}: rpc {s.name}.{md.name}'
            md.req_t = reg.resolve(md.req, pkg_scope, where)
            md.resp_t = reg.resolve(md.resp, pkg_scope, where)
            for t in (md.req_t, md.resp_t):
                if not isinstance(t, Message):
                    raise ProtoError(f"{where}: '{t.name}' is not a message")


# ---------------------------------------------------------------------------------------------
# C++ generation
# ---------------------------------------------------------------------------------------------
def cpp_type(res):
    kind, t = res
    return SCALARS[t][0] if kind == 'scalar' else cpp_name(t)


def default_init(res):
    kind, t = res
    if kind == 'scalar':
        if t in ('string', 'bytes'):
            return ''
        return ' = false' if t == 'bool' else ' = 0'
    return ' = {}' if kind == 'enum' else ''


def is_default(res, expr):
    kind, t = res
    if kind == 'enum':
        return f'static_cast<int32_t>({expr}) == 0'
    if t in ('string', 'bytes'):
        return f'{expr}.empty()'
    if t == 'bool':
        return f'!{expr}'
    if t in ('double', 'float'):
        return f'::aiverse::pb::isZeroBits({expr})'
    return f'{expr} == 0'


def write_value(res, num, expr, w='w'):
    kind, t = res
    if kind == 'scalar':
        return f'{w}.{SCALARS[t][2]}({num}, {expr});'
    if kind == 'enum':
        return f'{w}.int32({num}, static_cast<int32_t>({expr}));'
    return f'{w}.message({num}, {expr});'


def read_value(res, target, r='r', wt='wt'):
    kind, t = res
    if kind == 'scalar':
        return f'if (!{r}.{SCALARS[t][3]}({wt}, {target})) return false;'
    if kind == 'enum':
        return f'if (!{r}.enumeration({wt}, {target})) return false;'
    return f'if (!{r}.message({wt}, {target})) return false;'


def packable(res):
    return res[0] == 'enum' or (res[0] == 'scalar' and res[1] not in ('string', 'bytes'))


def case_enum(oneof):
    return camel(oneof) + 'Case'


class Gen:
    def __init__(self, f):
        self.f = f
        self.h, self.c = [], []

    def ordered(self, msgs):
        """Order siblings so by-value members are complete before use (repeated is a soft edge)."""
        ids = {id(m) for m in msgs}
        out, state = [], {}

        def deps(m, hard):
            found = []

            def collect(mm):
                for fl in mm.fields:
                    soft = fl.label == 'repeated'
                    if soft and hard:
                        continue
                    for r in (fl.resolved, fl.map_value):
                        if r and r[0] == 'message':
                            found.append(r[1])
                for sub in mm.messages:
                    collect(sub)
            collect(m)
            res = []
            for d in found:
                node = d
                while node is not None and id(node) not in ids:
                    node = node.parent
                if node is not None:
                    if node is m:
                        if hard and d is m:
                            raise ProtoError(f"{self.f.rel}: message '{m.name}' contains itself by value; "
                                             f"recursive messages are not supported (use repeated)")
                        continue
                    res.append(node)
            return res

        def visit(m, stack):
            s = state.get(id(m))
            if s == 2:
                return
            if s == 1:
                raise ProtoError(f"{self.f.rel}: messages {' -> '.join(x.name for x in stack + [m])} contain each "
                                 f"other by value; recursive messages are not supported")
            state[id(m)] = 1
            for d in deps(m, True):
                visit(d, stack + [m])
            for d in deps(m, False):
                if state.get(id(d)) is None:
                    visit(d, stack + [m])
            state[id(m)] = 2
            out.append(m)
        for m in msgs:
            visit(m, [])
        return out

    def gen_enum(self, e, p):
        self.h.append(f'{p}enum class {e.name} : int32_t {{')
        for name, num in e.values:
            self.h.append(f'{p}    {ident(name)} = {num},')
        self.h.append(f'{p}}};')

    def field_decl(self, fl):
        n = ident(fl.name)
        if fl.map:
            return f'std::map<{SCALARS[fl.map[0]][0]}, {cpp_type(fl.map_value)}> {n};'
        t = cpp_type(fl.resolved)
        if fl.label == 'repeated':
            return f'std::vector<{t}> {n};'
        if fl.label == 'optional' or fl.resolved[0] == 'message':
            return f'std::optional<{t}> {n};'
        return f'{t} {n}{default_init(fl.resolved)};'

    def gen_message(self, m, depth):
        p = '    ' * depth
        q = p + '    '
        h = self.h
        h.append(f'{p}struct {m.name} {{')
        for e in m.enums:
            self.gen_enum(e, q)
        for sub in self.ordered(m.messages):
            self.gen_message(sub, depth + 1)
        for fl in m.fields:
            if fl.oneof is None:
                h.append(q + self.field_decl(fl))
        for on in m.oneofs:
            members = [fl for fl in m.fields if fl.oneof == on]
            ce = case_enum(on)
            h.append(f'{q}enum class {ce} : int32_t {{')
            h.append(f'{q}    None = 0,')
            for fl in members:
                h.append(f'{q}    {ident(fl.name)} = {fl.number},')
            h.append(f'{q}}};')
            h.append(f'{q}{ce} {on}_case = {ce}::None;')
            for fl in members:
                h.append(f'{q}{cpp_type(fl.resolved)} {ident(fl.name)}{default_init(fl.resolved)};')
            for fl in members:
                n = ident(fl.name)
                h.append(f'{q}bool has_{fl.name}() const {{ return {on}_case == {ce}::{n}; }}')
                h.append(f'{q}{cpp_type(fl.resolved)} &set_{fl.name}() {{ {on}_case = {ce}::{n}; return {n}; }}')
        h.append(f'{q}void encode(::aiverse::pb::Writer &w) const;')
        h.append(f'{q}bool decode(::aiverse::pb::Reader &r);')
        h.append(f'{q}std::string serialize() const {{ std::string s; ::aiverse::pb::Writer w(s); encode(w); return s; }}')
        h.append(f'{q}// Replaces *this; false on malformed input (the message is then partially filled).')
        h.append(f'{q}bool parse(std::string_view bytes) {{ *this = {m.name}{{}}; ::aiverse::pb::Reader r(bytes); return decode(r) && r.ok(); }}')
        h.append(f'{q}bool operator==(const {m.name} &) const = default;')
        h.append(f'{p}}};')
        self.gen_impl(m)

    def gen_impl(self, m):
        cn = cpp_name(m)[2:]
        c = self.c
        fields = sorted(m.fields, key=lambda x: x.number)
        c.append(f'void {cn}::encode(::aiverse::pb::Writer &w) const {{')
        if not fields:
            c.append('    (void)w;')
        for fl in fields:
            n = ident(fl.name)
            if fl.map:
                c.append(f'    for (const auto &[k, v] : {n})')
                c.append(f'        w.mapEntry({fl.number}, [&](::aiverse::pb::Writer &e) {{')
                c.append(f'            {write_value(("scalar", fl.map[0]), 1, "k", "e")}')
                c.append(f'            {write_value(fl.map_value, 2, "v", "e")}')
                c.append('        });')
            elif fl.label == 'repeated':
                if packable(fl.resolved):
                    kind, t = fl.resolved
                    raw = 'rawEnum' if kind == 'enum' else 'raw_' + SCALARS[t][2]
                    c.append(f'    w.packed({fl.number}, {n}, [](::aiverse::pb::Writer &e, auto v) {{ e.{raw}(v); }});')
                else:
                    c.append(f'    for (const auto &v : {n})')
                    c.append(f'        {write_value(fl.resolved, fl.number, "v")}')
            elif fl.oneof is not None:
                c.append(f'    if ({fl.oneof}_case == {case_enum(fl.oneof)}::{n})')
                c.append(f'        {write_value(fl.resolved, fl.number, n)}')
            elif fl.label == 'optional' or fl.resolved[0] == 'message':
                c.append(f'    if ({n})')
                c.append(f'        {write_value(fl.resolved, fl.number, "*" + n)}')
            else:
                c.append(f'    if (!({is_default(fl.resolved, n)}))')
                c.append(f'        {write_value(fl.resolved, fl.number, n)}')
        c.append('}')
        c.append(f'bool {cn}::decode(::aiverse::pb::Reader &r) {{')
        c.append('    uint32_t field = 0;')
        c.append('    ::aiverse::pb::WireType wt{};')
        c.append('    while (r.next(field, wt)) {')
        if fields:
            c.append('        switch (field) {')
        for fl in fields:
            n = ident(fl.name)
            c.append(f'        case {fl.number}: {{')
            ls = []
            if fl.map:
                ls.append(f'{SCALARS[fl.map[0]][0]} k{{}};')
                ls.append(f'{cpp_type(fl.map_value)} v{{}};')
                ls.append('::aiverse::pb::Reader e;')
                ls.append('if (!r.sub(wt, e)) return false;')
                ls.append('uint32_t ef = 0;')
                ls.append('::aiverse::pb::WireType ewt{};')
                ls.append('while (e.next(ef, ewt)) {')
                ls.append('    if (ef == 1) {')
                ls.append('        ' + read_value(('scalar', fl.map[0]), 'k', 'e', 'ewt'))
                ls.append('    } else if (ef == 2) {')
                ls.append('        ' + read_value(fl.map_value, 'v', 'e', 'ewt'))
                ls.append('    } else if (!e.skip(ewt)) {')
                ls.append('        return false;')
                ls.append('    }')
                ls.append('}')
                ls.append('if (!e.ok()) return false;')
                ls.append(f'{n}[k] = std::move(v);')
            elif fl.label == 'repeated':
                if packable(fl.resolved):
                    kind, t = fl.resolved
                    fn = 'repeatedEnum' if kind == 'enum' else 'repeated_' + SCALARS[t][3]
                    ls.append(f'if (!r.{fn}(wt, {n})) return false;')
                else:
                    ls.append(f'{n}.emplace_back();')
                    ls.append(read_value(fl.resolved, f'{n}.back()'))
            elif fl.oneof is not None:
                ce = case_enum(fl.oneof)
                ls.append(f'if ({fl.oneof}_case != {ce}::{n}) {{')
                ls.append(f'    {fl.oneof}_case = {ce}::{n};')
                ls.append(f'    {n} = {{}};')
                ls.append('}')
                ls.append(read_value(fl.resolved, n))
            elif fl.label == 'optional' or fl.resolved[0] == 'message':
                ls.append(f'if (!{n}) {n}.emplace();')
                ls.append(read_value(fl.resolved, f'*{n}'))
            else:
                ls.append(read_value(fl.resolved, n))
            c.extend('            ' + x for x in ls)
            c.append('            break;')
            c.append('        }')
        if fields:
            c.append('        default:')
            c.append('            if (!r.skip(wt)) return false;')
            c.append('        }')
        else:
            c.append('        (void)field;')
            c.append('        if (!r.skip(wt)) return false;')
        c.append('    }')
        c.append('    return r.ok();')
        c.append('}')

    def ns(self):
        return '::'.join(self.f.package.split('.')) if self.f.package else ''

    def run(self):
        base = os.path.splitext(self.f.rel)[0]
        h = self.h
        h += [f'// Generated by tools/protoc/aiverse_protoc.py from {self.f.rel}. Do not edit.',
              '#pragma once', '#include "engine/grpc/pb/Pb.h"']
        for imp in self.f.imports:
            if imp.startswith('google/protobuf/'):
                raise ProtoError(f"{self.f.rel}: import '{imp}': well-known types are not bundled; "
                                 f"copy that .proto into your include root")
            h.append(f'#include "{os.path.splitext(imp)[0]}.pb.h"')
        h += ['#include <cstdint>', '#include <map>', '#include <optional>', '#include <string>',
              '#include <string_view>', '#include <vector>', '']
        if self.ns():
            h.append(f'namespace {self.ns()} {{')
        self.c += [f'// Generated by tools/protoc/aiverse_protoc.py from {self.f.rel}. Do not edit.',
                   f'#include "{base}.pb.h"', '#include <utility>', '']
        if self.ns():
            self.c.append(f'namespace {self.ns()} {{')
        for e in self.f.enums:
            self.gen_enum(e, '')
        for m in self.ordered(self.f.messages):
            self.gen_message(m, 0)
        if self.ns():
            h.append(f'}} // namespace {self.ns()}')
            self.c.append(f'}} // namespace {self.ns()}')
        # implementations were emitted with fully qualified names relative to the package
        pkg_prefix = ('::'.join(self.f.package.split('.')) + '::') if self.f.package else ''
        self.c = [line.replace(f'void {pkg_prefix}', 'void ').replace(f'bool {pkg_prefix}', 'bool ')
                  if line.startswith(('void ', 'bool ')) else line for line in self.c]
        return '\n'.join(h) + '\n', '\n'.join(self.c) + '\n'

    def grpc(self):
        base = os.path.splitext(self.f.rel)[0]
        g = [f'// Generated by tools/protoc/aiverse_protoc.py from {self.f.rel}. Do not edit.',
             '#pragma once', f'#include "{base}.pb.h"', '#include "engine/grpc/Grpc.h"',
             '#include <memory>', '#include <utility>', '']
        if self.ns():
            g.append(f'namespace {self.ns()} {{')
        for s in self.f.services:
            full = (self.f.package + '.' if self.f.package else '') + s.name
            g.append(f'struct {s.name} {{')
            g.append(f'    static constexpr const char *kName = "{full}";')
            for md in s.methods:
                kind = ('Bidi' if md.cstream and md.sstream else 'ClientStream' if md.cstream
                        else 'ServerStream' if md.sstream else 'Unary')
                g.append(f'    static constexpr ::aiverse::grpc::MethodInfo k{md.name}{{"/{full}/{md.name}", '
                         f'::aiverse::grpc::MethodKind::{kind}}};')
            g.append('    // Typed client over a Channel. Callbacks run inside Channel::poll() on the owning thread.')
            g.append('    class Client {')
            g.append('      public:')
            g.append('        explicit Client(::aiverse::grpc::Channel &channel) : channel_(&channel) {}')
            g.append('        ::aiverse::grpc::Channel &channel() const { return *channel_; }')
            for md in s.methods:
                req, resp = cpp_name(md.req_t), cpp_name(md.resp_t)
                if not md.cstream and not md.sstream:
                    g.append(f'        ::aiverse::grpc::Call {md.name}(const {req} &request, '
                             f'::aiverse::grpc::UnaryDone<{resp}> done, ::aiverse::grpc::CallOptions options = {{}}) {{')
                    g.append(f'            return ::aiverse::grpc::unaryCall<{resp}>(*channel_, k{md.name}, '
                             f'request.serialize(), std::move(done), std::move(options));')
                    g.append('        }')
                else:
                    g.append(f'        std::shared_ptr<::aiverse::grpc::Stream<{req}, {resp}>> {md.name}('
                             f'::aiverse::grpc::StreamHandlers<{resp}> handlers, ::aiverse::grpc::CallOptions options = {{}}) {{')
                    g.append(f'            return ::aiverse::grpc::openStream<{req}, {resp}>(*channel_, k{md.name}, '
                             f'std::move(handlers), std::move(options));')
                    g.append('        }')
            g.append('      private:')
            g.append('        ::aiverse::grpc::Channel *channel_;')
            g.append('    };')
            g.append('};')
        if self.ns():
            g.append(f'}} // namespace {self.ns()}')
        g.append('')
        return '\n'.join(g) + '\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description='proto3 -> C++20 generator (no protoc needed)')
    ap.add_argument('--out', required=True)
    ap.add_argument('-I', dest='includes', action='append', default=[])
    ap.add_argument('--grpc', action='store_true', help='also generate typed gRPC client stubs')
    ap.add_argument('--check', action='store_true', help='exit 1 if outputs differ (nothing is written)')
    ap.add_argument('protos', nargs='+')
    a = ap.parse_args(argv)
    roots = [os.path.abspath(i) for i in (a.includes or ['.'])]

    def locate(path):
        full = os.path.abspath(path)
        if os.path.exists(full):
            for r in roots:
                if full.startswith(r + os.sep):
                    return full, os.path.relpath(full, r).replace(os.sep, '/')
            return full, os.path.basename(full)
        for r in roots:
            cand = os.path.join(r, path)
            if os.path.exists(cand):
                return cand, path.replace(os.sep, '/')
        raise ProtoError(f"cannot find '{path}' (searched {', '.join(roots)})")

    files, seen, reg = [], set(), Registry()
    try:
        queue = [locate(p) for p in a.protos]
        requested = {rel for _, rel in queue}
        while queue:
            path, rel = queue.pop(0)
            if rel in seen:
                continue
            seen.add(rel)
            with open(path, encoding='utf-8') as fh:
                text = fh.read()
            f = ProtoFile(path, rel)
            Parser(tokenize(text, rel), f).parse_file()
            files.append(f)
            reg.add_file(f)
            queue += [locate(i) for i in f.imports if not i.startswith('google/protobuf/')]
        for f in files:
            resolve_file(f, reg)
        stale = []
        for f in files:
            if f.rel not in requested:
                continue
            gen = Gen(f)
            hdr, src = gen.run()
            outs = [('.pb.h', hdr), ('.pb.cpp', src)]
            gs = gen.grpc() if a.grpc else None
            if gs:
                outs.append(('.grpc.h', gs))
            base = os.path.join(a.out, os.path.splitext(f.rel)[0])
            for ext, text in outs:
                p = base + ext
                old = None
                if os.path.exists(p):
                    with open(p, encoding='utf-8') as fh:
                        old = fh.read()
                if old == text:
                    continue
                stale.append(p)
                if not a.check:
                    os.makedirs(os.path.dirname(p) or '.', exist_ok=True)
                    with open(p, 'w', encoding='utf-8', newline='\n') as fh:
                        fh.write(text)
        if a.check and stale:
            print('aiverse_protoc: out of date: ' + ', '.join(stale), file=sys.stderr)
            return 1
    except ProtoError as e:
        print(f'aiverse_protoc: error: {e}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
