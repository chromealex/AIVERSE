// Run against the actual generated WebAssembly; no DOM or graphics device required.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

async function verify(web) {
  const buildings = await require(path.join(web, 'buildings.js'))();
  const caves = await require(path.join(web, 'caverns.js'))();
  const seed = JSON.parse(fs.readFileSync(path.join(web, 'building.recipe.json'), 'utf8'));
  const styles = buildings.FS.readdir('/engine/assets/building-styles').filter(name => name.endsWith('.json'));
  assert.ok(styles.length >= 13);
  for (const file of styles) {
    const style = file.slice(0, -5);
    const request = { ...seed, style, textureResolution: style === seed.style ? seed.textureResolution : 128 };
    const result = JSON.parse(buildings.ccall('demo_build', 'string', ['string'], [JSON.stringify(request)]));
    assert.ok(!result.error, style + ': ' + result.error);
    const bytes = Buffer.from(buildings.FS.readFile('/demo-project/content/' + result.path));
    const recipe = JSON.parse(buildings.FS.readFile('/demo-project/content/' + result.recipePath, {encoding: 'utf8'}));
    assert.equal(bytes.toString('ascii', 0, 4), 'glTF');
    assert.equal(bytes.readUInt32LE(8), bytes.length);
    assert.equal(recipe.style, style);
    assert.equal(recipe.physicalSockets.length, seed.physicalSockets.length);
    assert.ok(result.triangles > 0);
    if (style === seed.style) {
      const digest = crypto.createHash('sha256').update(bytes).digest('hex');
      console.log('Initial Studio recipe GLB SHA-256', digest);
    }
  }
  function mesh(mode) {
    const count = caves._rebuild(mode, 150);
    assert.ok(count > 0 && count % 3 === 0);
    const positions = new Uint8Array(caves.HEAPF32.buffer, caves._vertex_data(), count * 6 * 4);
    return crypto.createHash('sha256').update(positions).digest('hex');
  }
  const initial = mesh(0);
  assert.equal(new Set([initial, mesh(1), mesh(2)]).size, 3);
  assert.equal(mesh(0), initial);
  console.log(styles.length + ' building styles exported; Carve, Fill and baseline reset verified.');
}
if (process.argv.length !== 3) throw new Error('Usage: node verify.cjs <web-output-directory>');
verify(path.resolve(process.argv[2])).catch(error => { console.error(error); process.exitCode = 1; });
