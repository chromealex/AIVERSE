"""Small syntax highlighter for the documentation pages (stdlib only).

highlight(code, lang) returns escaped HTML with <span class="hl-*"> tokens. One regex per
language family, scanned left to right: comments, strings, numbers, keywords, types,
preprocessor lines, function calls. Unknown languages come back escaped and unchanged.
"""
import html
import re

C_KEYWORDS = '''alignas alignof auto break case catch class const consteval constexpr constinit
continue co_await co_return co_yield decltype default delete do else enum explicit export
extern false final for friend goto if inline mutable namespace new noexcept nullptr operator
override private protected public return sizeof static static_assert static_cast dynamic_cast
reinterpret_cast const_cast struct switch template this throw true try typedef typename union
using virtual volatile while concept requires kernel vertex fragment device constant thread
threadgroup in out uniform layout precision highp mediump lowp discard'''
C_TYPES = '''void bool char short int long float double signed unsigned size_t uint8_t uint16_t uint32_t
uint64_t int8_t int16_t int32_t int64_t std string vector map array span optional unique_ptr
shared_ptr float2 float3 float4 float4x4 half half2 half3 half4 uint uint2 uint3 uint4 int2
int3 int4 vec2 vec3 vec4 ivec2 ivec3 ivec4 uvec2 uvec3 uvec4 mat3 mat4 sampler2D texture2d
Fixed Entity'''
PY_KEYWORDS = '''and as assert async await break class continue def del elif else except False finally
for from global if import in is lambda None nonlocal not or pass raise return True try while
with yield print self'''
JS_KEYWORDS = '''async await break case catch class const continue default delete do else export extends
false finally for from function if import in instanceof let new null return static super switch
this throw true try typeof undefined var void while yield interface type enum implements'''
LUA_KEYWORDS = '''and break do else elseif end false for function goto if in local nil not or repeat
return then true until while self game view'''
SH_KEYWORDS = '''if then else elif fi for while do done case esac in function return export local
set unset source cd echo exit sudo'''
CMAKE_KEYWORDS = '''if elseif else endif foreach endforeach function endfunction macro endmacro set
option add_library add_executable target_link_libraries target_include_directories
target_compile_definitions add_subdirectory include find_package add_test project
cmake_minimum_required install message list string PUBLIC PRIVATE INTERFACE ON OFF'''

LANGS = {
    'c': 'c', 'cpp': 'c', 'c++': 'c', 'cc': 'c', 'h': 'c', 'hpp': 'c', 'cxx': 'c', 'objc': 'c',
    'objective-c': 'c', 'mm': 'c', 'glsl': 'c', 'hlsl': 'c', 'metal': 'c', 'msl': 'c',
    'json': 'json', 'jsonc': 'json', 'json5': 'json',
    'python': 'py', 'py': 'py',
    'js': 'js', 'javascript': 'js', 'ts': 'js', 'typescript': 'js', 'tsx': 'js', 'jsx': 'js',
    'lua': 'lua',
    'sh': 'sh', 'bash': 'sh', 'shell': 'sh', 'zsh': 'sh', 'console': 'sh', 'powershell': 'sh', 'ps1': 'sh',
    'cmake': 'cmake', 'yaml': 'yaml', 'yml': 'yaml',
}


def _words(text):
    return '|'.join(sorted(set(text.split()), key=len, reverse=True))


def _rules(family):
    num = r'(?P<num>\b(?:0[xX][0-9a-fA-F_]+|\d[\d_]*(?:\.\d+)?(?:[eE][+-]?\d+)?)[uUlLfF]*\b)'
    if family == 'c':
        return re.compile(
            r'(?P<com>//[^\n]*|/\*.*?\*/)|(?P<pre>^[ \t]*#[ \t]*\w+)|'
            r'(?P<str>"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|<[\w./-]+\.h(?:pp)?>)|' + num +
            rf'|(?P<kw>\b(?:{_words(C_KEYWORDS)})\b)|(?P<ty>\b(?:{_words(C_TYPES)})\b|\b[A-Z][A-Za-z0-9]*[a-z][A-Za-z0-9]*\b)'
            r'|(?P<fn>\b[a-z_]\w*(?=\s*\())', re.S | re.M)
    if family == 'json':
        return re.compile(
            r'(?P<com>//[^\n]*|/\*.*?\*/)|(?P<key>"(?:\\.|[^"\\\n])*"(?=\s*:))|(?P<str>"(?:\\.|[^"\\\n])*")|'
            r'(?P<num>-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)|(?P<kw>\b(?:true|false|null)\b)', re.S)
    if family == 'py':
        return re.compile(
            r'(?P<com>#[^\n]*)|(?P<str>"""[\s\S]*?"""|\'\'\'[\s\S]*?\'\'\'|[rbfu]*"(?:\\.|[^"\\\n])*"|[rbfu]*\'(?:\\.|[^\'\\\n])*\')|'
            + num + rf'|(?P<kw>\b(?:{_words(PY_KEYWORDS)})\b)|(?P<fn>\b[A-Za-z_]\w*(?=\s*\())', re.S)
    if family == 'js':
        return re.compile(
            r'(?P<com>//[^\n]*|/\*.*?\*/)|(?P<str>`(?:\\.|[^`\\])*`|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\')|'
            + num + rf'|(?P<kw>\b(?:{_words(JS_KEYWORDS)})\b)|(?P<ty>\b[A-Z][A-Za-z0-9]*\b)|(?P<fn>\b[a-z_$][\w$]*(?=\s*\())', re.S)
    if family == 'lua':
        return re.compile(
            r'(?P<com>--\[\[.*?\]\]|--[^\n]*)|(?P<str>\[\[.*?\]\]|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\')|'
            + num + rf'|(?P<kw>\b(?:{_words(LUA_KEYWORDS)})\b)|(?P<fn>\b[A-Za-z_]\w*(?=\s*\())', re.S)
    if family == 'sh':
        return re.compile(
            r'(?P<com>(?:^|(?<=\s))#[^\n]*)|(?P<str>"(?:\\.|[^"\\])*"|\'[^\']*\')|(?P<var>\$\{?\w+\}?)|'
            r'(?P<opt>(?<=\s)--?[A-Za-z][\w-]*)|'
            rf'(?P<kw>\b(?:{_words(SH_KEYWORDS)})\b)|(?P<fn>^[ \t]*(?:[\w./-]+=\S*\s+)*[\w./-]+)', re.M)
    if family == 'cmake':
        return re.compile(
            r'(?P<com>#[^\n]*)|(?P<str>"(?:\\.|[^"\\])*")|(?P<var>\$\{[^}]*\})|'
            rf'(?P<kw>\b(?:{_words(CMAKE_KEYWORDS)})\b)|(?P<fn>\b\w+(?=\s*\())', re.I)
    if family == 'yaml':
        return re.compile(r'(?P<com>#[^\n]*)|(?P<key>^[ \t-]*[\w.-]+(?=\s*:))|(?P<str>"[^"\n]*"|\'[^\'\n]*\')|' + num, re.M)
    return None


_CACHE = {}


def highlight(code, lang):
    family = LANGS.get((lang or '').lower())
    if family is None:
        return html.escape(code)
    rx = _CACHE.get(family)
    if rx is None:
        rx = _CACHE[family] = _rules(family)
    out, pos = [], 0
    for m in rx.finditer(code):
        kind = m.lastgroup
        if m.start() > pos:
            out.append(html.escape(code[pos:m.start()]))
        text = m.group(0)
        if family == 'sh' and kind == 'fn':   # leading spaces / VAR=value prefixes stay plain
            lead = len(text) - len(text.lstrip())
            out.append(html.escape(text[:lead]))
            text = text[lead:]
        out.append(f'<span class="hl-{kind}">{html.escape(text)}</span>')
        pos = m.end()
    out.append(html.escape(code[pos:]))
    return ''.join(out)
