#!/usr/bin/env python3
"""Error handling and stability of tools/protoc/aiverse_protoc.py (stdlib only)."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROTOC = os.path.join(HERE, '..', '..', 'tools', 'protoc', 'aiverse_protoc.py')
failures = 0


def run(files, *extra):
    with tempfile.TemporaryDirectory() as d:
        for name, text in files.items():
            with open(os.path.join(d, name), 'w') as f:
                f.write(text)
        out = os.path.join(d, 'out')
        r = subprocess.run([sys.executable, PROTOC, '--out', out, '-I', d, *extra] +
                           [os.path.join(d, n) for n in files if n.endswith('.proto') and not n.startswith('_')],
                           capture_output=True, text=True)
        gen = {}
        for root, _, names in os.walk(out):
            for n in names:
                with open(os.path.join(root, n)) as f:
                    gen[os.path.relpath(os.path.join(root, n), out)] = f.read()
        return r.returncode, r.stderr, gen


def expect_error(name, files, needle):
    global failures
    rc, err, _ = run(files)
    if rc == 0 or needle not in err:
        failures += 1
        print(f'FAIL {name}: rc={rc} stderr={err!r}')


expect_error('proto2', {'a.proto': 'syntax = "proto2"; message A { optional int32 x = 1; }'}, 'only proto3')
expect_error('unknown type', {'a.proto': 'syntax = "proto3"; message A { Missing x = 1; }'}, "unknown type 'Missing'")
expect_error('self', {'a.proto': 'syntax = "proto3"; message A { A x = 1; }'}, 'recursive')
expect_error('cycle', {'a.proto': 'syntax = "proto3"; message A { B b = 1; } message B { A a = 1; }'}, 'recursive')
expect_error('dup number', {'a.proto': 'syntax = "proto3"; message A { int32 x = 1; int32 y = 1; }'}, 'field number 1')
expect_error('enum zero', {'a.proto': 'syntax = "proto3"; enum E { E_A = 1; }'}, 'first value must be 0')
expect_error('extensions', {'a.proto': 'syntax = "proto3"; message A { extensions 100 to 200; }'}, 'extensions')
expect_error('missing import', {'a.proto': 'syntax = "proto3"; import "nope.proto";'}, "cannot find 'nope.proto'")

# Imports across files + nested scope resolution + options + comments.
rc, err, gen = run({
    'common.proto': 'syntax = "proto3"; package p.common; message Id { string v = 1; }',
    'svc.proto': '''syntax = "proto3";
        package p.svc; import "common.proto"; option java_package = "x";
        /* block */ message Req { p.common.Id id = 1 [deprecated = true]; // line
          message Inner { repeated Req.Inner self_list = 1; } repeated Inner inner = 2; reserved 5, 6; reserved "old"; }
        service S { rpc Get(Req) returns (Req) { option deprecated = true; } }''',
}, '--grpc')
if rc != 0 or 'svc.grpc.h' not in gen or '#include "common.pb.h"' not in gen.get('svc.pb.h', ''):
    failures += 1
    print(f'FAIL imports: rc={rc} {err} {sorted(gen)}')
# Deterministic output.
rc2, _, gen2 = run({
    'common.proto': 'syntax = "proto3"; package p.common; message Id { string v = 1; }',
    'svc.proto': 'syntax = "proto3"; package p.svc; import "common.proto"; message Req { p.common.Id id = 1; }',
})
rc3, _, gen3 = run({
    'common.proto': 'syntax = "proto3"; package p.common; message Id { string v = 1; }',
    'svc.proto': 'syntax = "proto3"; package p.svc; import "common.proto"; message Req { p.common.Id id = 1; }',
})
if gen2 != gen3:
    failures += 1
    print('FAIL: output is not deterministic')

print('protoc_test:', 'OK' if not failures else f'{failures} failed')
sys.exit(1 if failures else 0)
