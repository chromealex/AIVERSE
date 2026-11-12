// Protocol test of gameanalytics.js against a fake GameAnalytics JS SDK (node plugins/gameanalytics/web/test.mjs).
// The fake keeps the real SDK's names and argument order (dist/GameAnalytics.d.ts, SDK 5.0.0).
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';

const calls = [];
// plain copies: objects made inside the vm context have another Object prototype
const plain = (v) => (v && typeof v === 'object' ? JSON.parse(JSON.stringify(v)) : v);
const rec = (name) => (...args) => { calls.push([name, ...args.map(plain)]); };
const GameAnalytics = {};
for (const m of ['setEnabledInfoLog', 'setEnabledVerboseLog', 'configureBuild', 'configureAvailableResourceCurrencies',
  'configureAvailableResourceItemTypes', 'configureAvailableCustomDimensions01', 'configureAvailableCustomDimensions02',
  'configureAvailableCustomDimensions03', 'configureUserId', 'initialize', 'setEnabledEventSubmission', 'setCustomDimension01',
  'setCustomDimension02', 'setCustomDimension03', 'setGlobalCustomEventFields', 'addDesignEvent', 'addProgressionEvent',
  'addResourceEvent', 'addBusinessEvent']) GameAnalytics[m] = rec(m);
const window = {
  gameanalytics: { GameAnalytics, EGAProgressionStatus: { Start: 1, Complete: 2, Fail: 3 }, EGAResourceFlowType: { Source: 1, Sink: 2 } },
  AiversePlugins: { plugins: {}, register(n, p) { this.plugins[n] = p; } },
};
const src = fs.readFileSync(fileURLToPath(new URL('./gameanalytics.js', import.meta.url)), 'utf8');
vm.runInNewContext(src, { window, Object, Array, String, Number, Math, Error });
const ga = window.AiversePlugins.plugins.gameanalytics;
assert.ok(ga, 'registered');

assert.throws(() => ga.event({ name: 'x', fields: {} }), /not initialized/);
assert.throws(() => ga.init({}), /gameKey and secretKey/);
ga.init({ gameKey: 'g'.repeat(32), secretKey: 's'.repeat(40), build: 1.2, currencies: ['coins'], itemTypes: ['robot'], dimensions01: ['pve', 'pvp'] });
assert.deepEqual(calls.find(c => c[0] === 'initialize'), ['initialize', 'g'.repeat(32), 's'.repeat(40)]);
assert.deepEqual(calls.find(c => c[0] === 'configureBuild'), ['configureBuild', '1.2']);
const init = calls.map(c => c[0]);
assert.ok(init.indexOf('configureAvailableResourceCurrencies') < init.indexOf('initialize'), 'configure before initialize');
calls.length = 0;

ga.progression({ status: 'complete', path: ['pve', 'stage_3'], score: 120, fields: { kills: 7, mode: 'pve', nested: { a: 1 } } });
ga.progression({ status: 'start', path: ['pve'], fields: {} });
ga.design({ id: 'match_end:pve:win', value: 93.5, fields: { kills: 7 } });
ga.event({ name: 'robot_unlocked', fields: { robot_id: 'r2' } });
ga.resource({ flow: 'sink', currency: 'coins', amount: 50, itemType: 'robot', itemId: 'r2_level', fields: {} });
ga.purchase({ currency: 'USD', priceMicros: 990000, itemType: 'gems', itemId: 'gems_100', cartType: '', fields: {} });
ga.user({ id: 'u1', properties: { dimension01: 'pvp', cohort: 3 } });
ga.consent({ analytics: false, tracking: false });
assert.deepEqual(calls, [
  ['addProgressionEvent', 2, 'pve', 'stage_3', undefined, 120, { kills: 7, mode: 'pve' }],
  ['addProgressionEvent', 1, 'pve', undefined, undefined, undefined, {}],
  ['addDesignEvent', 'match_end:pve:win', 93.5, { kills: 7 }],
  ['addDesignEvent', 'robot_unlocked', undefined, { robot_id: 'r2' }],
  ['addResourceEvent', 2, 'coins', 50, 'robot', 'r2_level', {}],
  ['addBusinessEvent', 'USD', 99, 'gems', 'gems_100', 'shop', {}],
  ['configureUserId', 'u1'],
  ['setCustomDimension01', 'pvp'],
  ['setGlobalCustomEventFields', { cohort: '3' }],
  ['setEnabledEventSubmission', false],
]);

calls.length = 0;
ga.init({ gameKey: 'g', secretKey: 's', customEvents: false });
calls.length = 0;
ga.event({ name: 'only_for_appmetrica', fields: {} });
assert.deepEqual(calls, [], 'customEvents: false keeps raw events out of GameAnalytics');
console.log('gameanalytics web plugin: OK');
