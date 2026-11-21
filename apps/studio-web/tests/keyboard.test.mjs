import test from 'node:test';
import assert from 'node:assert/strict';
import { shortcutKey } from '../src/keyboard.ts';

test('shortcuts use the physical key, independent of keyboard layout', () => {
  assert.equal(shortcutKey({ code: 'KeyW', key: 'ц' }), 'w');
  assert.equal(shortcutKey({ code: 'KeyF', key: 'а' }), 'f');
  assert.equal(shortcutKey({ code: 'KeyZ', key: 'Я' }), 'z');
  assert.equal(shortcutKey({ code: 'Digit1', key: '!' }), '1');
  assert.equal(shortcutKey({ code: 'ArrowDown', key: 'ArrowDown' }), 'arrowdown');
  assert.equal(shortcutKey({ code: 'Home', key: 'Home' }), 'home');
  assert.equal(shortcutKey({ key: 'Delete' }), 'delete');
});
