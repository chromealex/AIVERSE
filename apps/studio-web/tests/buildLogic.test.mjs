import test from 'node:test';
import assert from 'node:assert/strict';
import { buildErrors, buildPhase, buildSteps, formatBytes, missingToolchain, nativeBuildProgress, runTarget } from '../src/panels/buildLogic.ts';

test('native build progress follows the most recent valid generator output', () => {
  assert.deepEqual(nativeBuildProgress('[0/2] Re-checking directories\n[125/1770] Building Model.cpp.o\nwarning: unused'), {
    value:125,max:1770,label:'125 / 1770',action:'Building Model.cpp.o'
  });
  assert.deepEqual(nativeBuildProgress('[  8%] Built target core\n[101%] invalid'), {
    value:8,max:100,label:'8%',action:'Built target core'
  });
  assert.equal(nativeBuildProgress('[0/0] empty\nConfiguring done'),null);
});

test('bytes', () => {
  assert.equal(formatBytes(512), '512 B');
  assert.equal(formatBytes(2048), '2 KB');
  assert.equal(formatBytes(5 * 1024 * 1024), '5.0 MB');
  assert.equal(formatBytes('x'), '0 B');
});
test('steps, errors and missing toolchains from the aiversebuild log', () => {
  const log = '$ aiversebuild --target web\n\n==> Пак контента\n    ok\n\n==> Сборка web\nОШИБКА: для сборки web не хватает: Emscripten.\nAIVERSE_MISSING_TOOLCHAIN web: emsdk, cmake\n\n=== FAILED ===\n';
  assert.deepEqual(buildSteps(log), ['Пак контента', 'Сборка web']);
  assert.deepEqual(missingToolchain(log), { target: 'web', ids: ['emsdk', 'cmake'] });
  assert.deepEqual(buildErrors(log), ['ОШИБКА: для сборки web не хватает: Emscripten.']);
  assert.equal(buildPhase(false, 1, 3, log), 'missing-toolchain');
  assert.equal(buildPhase(true, 0, 0, log), 'running');
  assert.equal(buildPhase(false, 0, 0, ''), 'idle');
  assert.equal(buildPhase(false, 1, 0, 'x\n=== OK ===\n'), 'ok');
  assert.equal(buildPhase(false, 1, 1, 'x\n=== FAILED ===\n'), 'failed');
});
test('run label', () => {
  assert.deepEqual(runTarget('native-editor'), { target: 'Studio with project modules', estimate: false });
  assert.deepEqual(runTarget('build-ios'), { target: 'ios', estimate: false });
  assert.deepEqual(runTarget('estimate'), { target: '', estimate: true });
});
