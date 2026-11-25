import * as T from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';

const el = (id) => document.getElementById(id);
const status = (message) => {
  el('status').textContent = message;
};
async function start() {
  const renderer = new T.WebGLRenderer({ canvas: document.querySelector('canvas'), antialias: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.setClearColor(0x202b29);
  renderer.toneMapping = T.ACESFilmicToneMapping;
  const scene = new T.Scene(),
    camera = new T.PerspectiveCamera(38, 1, 0.05, 400);
  const pmrem = new T.PMREMGenerator(renderer);
  const room = new RoomEnvironment();
  scene.environment = pmrem.fromScene(room).texture;
  room.dispose();
  pmrem.dispose();
  scene.add(new T.HemisphereLight(0xe9fff8, 0x5d5047, 1.6));
  const light = new T.DirectionalLight(0xffeed2, 2.3);
  light.position.set(8, 16, 6);
  scene.add(light);
  const controls = new OrbitControls(camera, renderer.domElement);
  const building = new T.Group(),
    cavern = new T.Group();
  scene.add(building, cavern);
  const grid = new T.GridHelper(64, 32, 0x60796f, 0x3a4842);
  building.add(grid);
  let active = 'building',
    marker,
    surface;
  const caveMaterial = new T.MeshStandardMaterial({
    color: 0xcb995d,
    metalness: 0.04,
    roughness: 0.84,
    side: T.DoubleSide,
  });
  function draw() {
    renderer.render(scene, camera);
  }
  controls.addEventListener('change', draw);
  new ResizeObserver(([entry]) => {
    const { width, height } = entry.contentRect;
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    draw();
  }).observe(el('viewport'));
  let recipe = window.buildingRecipe;
  const label = {
    'large-gate': 'Большие ворота',
    'small-gate': 'Малые ворота',
    'personnel-door': 'Дверь',
    'sliding-roof': 'Сдвижная крыша',
  };
  let shapes = [];
  function updatePlan() {
    const names = {};
    el('socket').replaceChildren(new Option('Всё здание', ''));
    el('plan').replaceChildren();
    el('plan').setAttribute('viewBox', [-1, -1, recipe.width + 2, recipe.depth + 2].join(' '));
    el('planFigure').hidden = recipe.physicalSockets.length === 0;
    el('socket').disabled = recipe.physicalSockets.length === 0;
    recipe.physicalSockets.forEach((s, i) => {
      const n = (names[s.kind] = (names[s.kind] || 0) + 1);
      el('socket').add(new Option((label[s.kind] || s.kind) + (n > 1 ? ' ' + n : ''), String(i)));
    });
    el('spec').textContent =
      recipe.width +
      ' × ' +
      recipe.height +
      ' × ' +
      recipe.depth +
      ' м · ' +
      recipe.style +
      ' · seed ' +
      recipe.seed;
    const plan = el('plan');
    function planRect(x, z, width, depth, fill) {
      const r = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
      for (const [key, value] of Object.entries({ x, y: z, width, height: depth, fill }))
        r.setAttribute(key, value);
      plan.append(r);
      return r;
    }
    const footprint = planRect(0, 0, recipe.width, recipe.depth, '#2b3833');
    footprint.setAttribute('stroke', '#8b9a8e');
    footprint.setAttribute('stroke-width', '.16');
    shapes = recipe.physicalSockets.map((s, i) => {
      const [x, , z] = s.closedBoundsMin,
        [u, , v] = s.closedBoundsMax;
      const r = planRect(x, z, u - x, Math.max(0.55, v - z), '#a3bd92');
      r.style.cursor = 'pointer';
      r.onclick = () => {
        el('socket').value = String(i);
        selectSocket();
      };
      return r;
    });
  }
  updatePlan();
  function selectSocket() {
    if (marker) {
      building.remove(marker);
      marker.geometry.dispose();
      marker.material.dispose();
      marker = null;
    }
    const s = recipe.physicalSockets[el('socket').value];
    shapes.forEach((r, i) => r.setAttribute('fill', recipe.physicalSockets[i] === s ? '#ffcd44' : '#a3bd92'));
    el('socketInfo').textContent = s ? s.id : '';
    if (s) {
      const bounds = new T.Box3(new T.Vector3(...s.closedBoundsMin), new T.Vector3(...s.closedBoundsMax));
      marker = new T.Box3Helper(bounds, 0xffcd44);
      marker.material.depthTest = false;
      marker.renderOrder = 10;
      building.add(marker);
      const center = bounds.getCenter(new T.Vector3());
      controls.target.set(recipe.width / 2, recipe.height * 0.38, recipe.depth / 2);
      const roof = s.kind === 'sliding-roof';
      camera.position.set(center.x + 12, roof ? 44 : 17, center.z + (center.z < recipe.depth / 2 ? -32 : 32));
      controls.update();
    }
    draw();
  }
  el('socket').onchange = selectSocket;
  const buildingModule = createBuildings();
  const engine = await buildingModule;
  function generateAsset(module, request) {
    const result = JSON.parse(module.ccall('demo_build', 'string', ['string'], [JSON.stringify(request)]));
    if (result.error) throw Error(result.error);
    const file = (name) => '/demo-project/content/' + name;
    return {
      result,
      bytes: module.FS.readFile(file(result.path)),
      recipe: JSON.parse(module.FS.readFile(file(result.recipePath), { encoding: 'utf8' })),
    };
  }
  const binary = generateAsset(engine, recipe).bytes;
  el('downloadModel').href = URL.createObjectURL(new Blob([binary], { type: 'model/gltf-binary' }));
  let model = await new GLTFLoader().parseAsync(binary.buffer, '');
  building.add(model.scene);
  grid.position.set(12, -0.08, 9);
  const wasm = await createCaverns();
  let triangles = 0;
  function rebuild() {
    const mode = Number(el('mode').value),
      radius = Number(el('radius').value);
    el('radius').disabled = !mode;
    el('radiusText').textContent = radius + ' см';
    const count = wasm._rebuild(mode, radius);
    const start = wasm._vertex_data() / 4;
    const values = wasm.HEAPF32.slice(start, start + count * 6);
    if (!count) throw Error('Пустая поверхность');
    const buffer = new T.InterleavedBuffer(values, 6),
      geometry = new T.BufferGeometry();
    geometry.setAttribute('position', new T.InterleavedBufferAttribute(buffer, 3, 0));
    geometry.setAttribute('normal', new T.InterleavedBufferAttribute(buffer, 3, 3));
    if (surface) {
      cavern.remove(surface);
      surface.geometry.dispose();
    }
    surface = new T.Mesh(geometry, caveMaterial);
    cavern.add(surface);
    triangles = count / 3;
    status(triangles.toLocaleString('ru-RU') + ' треугольников');
    draw();
  }
  function reset() {
    const isBuilding = active === 'building';
    if (isBuilding) {
      const bounds = new T.Box3().setFromObject(model.scene),
        size = bounds.getSize(new T.Vector3()),
        center = bounds.getCenter(new T.Vector3());
      const span = Math.max(size.x, size.y, size.z);
      controls.target.copy(center);
      camera.position.copy(center).addScaledVector(new T.Vector3(1.12, 0.78, -1.42), span);
      grid.position.set(center.x, bounds.min.y - 0.08, center.z);
    } else {
      controls.target.set(0, -3.4, 0);
      camera.position.set(7.8, 1.5, 8.4);
    }
    controls.maxDistance = isBuilding ? 500 : 32;
    controls.minDistance = isBuilding ? 3 : 1;
    controls.update();
    draw();
  }
  function show(tab) {
    active = tab;
    const b = tab === 'building';
    building.visible = b;
    cavern.visible = !b;
    el('buildingPanel').hidden = !b;
    el('cavePanel').hidden = b;
    el('building').setAttribute('aria-pressed', String(b));
    el('caves').setAttribute('aria-pressed', String(!b));
    if (!b && !surface) rebuild();
    status(
      b
        ? model.parser.json.materials.length +
            ' материалов · ' +
            (model.parser.json.images?.length ?? 0) +
            ' текстуры'
        : triangles.toLocaleString('ru-RU') + ' треугольников',
    );
    reset();
  }
  el('building').onclick = () => show('building');
  el('caves').onclick = () => show('caves');
  el('mode').onchange = rebuild;
  el('radius').oninput = () => {
    el('radiusText').textContent = el('radius').value + ' см';
  };
  el('radius').onchange = rebuild;
  el('reset').onclick = reset;
  const styleNames = {
    scifi: 'Sci-Fi',
    'scifi-space': 'Космический Sci-Fi',
    modern: 'Современный',
    'alien-civilization': 'Инопланетный',
    'ancient-civilizations': 'Древние цивилизации',
    'asian-historical': 'Историческая Азия',
    cyberpunk: 'Киберпанк',
    'dark-fantasy': 'Тёмное фэнтези',
    'medieval-fantasy': 'Средневековое фэнтези',
    'post-apocalypse': 'Постапокалипсис',
    solarpunk: 'Соларпанк',
    steampunk: 'Стимпанк',
    underwater: 'Подводный',
  };
  const roles = {
    residence: 'Жилой дом',
    hall: 'Зал',
    workshop: 'Мастерская',
    warehouse: 'Склад',
    tower: 'Башня',
    temple: 'Храм',
    fortress: 'Крепость',
    market: 'Рынок',
    factory: 'Фабрика',
    barracks: 'Казарма',
    command: 'Командный центр',
  };
  const baseFeatures = window.buildingRecipe.features;
  for (const id of Object.keys(window.styleRoles)) el('style').add(new Option(styleNames[id] || id, id));
  el('style').value = 'scifi';
  el('features').value = JSON.stringify(
    baseFeatures.filter((f) => f.kind !== 'sliding-roof'),
    null,
    2,
  );
  function updateRoles() {
    const previous = el('role').value;
    el('role').replaceChildren(new Option('По функциональному рецепту', 'custom'));
    for (const id of window.styleRoles[el('style').value]) el('role').add(new Option(roles[id] || id, id));
    el('role').value = Array.from(el('role').options).some((o) => o.value === previous) ? previous : 'custom';
    el('layout').hidden = el('role').value !== 'custom';
  }
  updateRoles();
  el('style').onchange = updateRoles;
  el('role').onchange = () => {
    el('layout').hidden = el('role').value !== 'custom';
  };
  el('roof').onchange = () => {
    const flat = el('roof').value === 'flat';
    el('sliding').disabled = !flat;
    if (!flat) el('sliding').checked = false;
    el('rise').value = flat ? '0' : String(Number(el('rise').value) || 2);
  };
  el('template').onchange = () => {
    const preset = window.buildingLayouts[el('template').value] || {
      size: [24, 12, 18],
      rooms: [1, 1, 1],
      features: baseFeatures,
    };
    ['width', 'height', 'depth'].forEach((id, i) => (el(id).value = preset.size[i]));
    ['floors', 'roomsX', 'roomsZ'].forEach((id, i) => (el(id).value = preset.rooms[i]));
    el('roof').value = 'flat';
    el('rise').value = 0;
    el('sliding').disabled = false;
    el('sliding').checked = preset.features.some((f) => f.kind === 'sliding-roof');
    el('features').value = JSON.stringify(
      preset.features.filter((f) => f.kind !== 'sliding-roof'),
      null,
      2,
    );
  };
  const fields = {
    featureKind: 'kind',
    featureSide: 'side',
    featureCount: 'count',
    featureW: 'width',
    featureH: 'height',
  };
  function refreshFeatures(index = 0) {
    const rows = JSON.parse(el('features').value);
    el('featureRow').replaceChildren();
    rows.forEach((row, i) =>
      el('featureRow').add(
        new Option(
          i +
            1 +
            '. ' +
            (label[row.kind] ||
              { window: 'Окно', stairs: 'Лестница', elevator: 'Лифт' }[row.kind] ||
              row.kind),
          i,
        ),
      ),
    );
    index = Math.max(0, Math.min(index, rows.length - 1));
    el('featureRow').selectedIndex = rows.length ? index : -1;
    for (const [id, key] of Object.entries(fields)) {
      const input = el(id);
      input.disabled = !rows.length;
      input.value = rows.length ? (rows[index][key] ?? (key === 'count' ? 1 : 0)) : '';
    }
    el('dropFeature').disabled = !rows.length;
  }
  for (const [id, key] of Object.entries(fields))
    el(id).onchange = () => {
      const index = el('featureRow').selectedIndex;
      const rows = JSON.parse(el('features').value);
      rows[index][key] = key === 'side' || key === 'kind' ? el(id).value : Number(el(id).value);
      el('features').value = JSON.stringify(rows, null, 2);
      refreshFeatures(index);
    };
  el('featureRow').onchange = () => refreshFeatures(el('featureRow').selectedIndex);
  el('features').onchange = () => {
    try {
      refreshFeatures();
    } catch {
      el('buildMessage').textContent = 'Проверьте JSON элементов';
    }
  };
  el('addFeature').onclick = () => {
    const rows = JSON.parse(el('features').value);
    rows.push({ kind: 'window', side: 'left', count: 1, width: 1.5, height: 1.2, sill: 1 });
    el('features').value = JSON.stringify(rows, null, 2);
    refreshFeatures(rows.length - 1);
  };
  el('dropFeature').onclick = () => {
    const rows = JSON.parse(el('features').value),
      index = el('featureRow').selectedIndex;
    rows.splice(index, 1);
    el('features').value = JSON.stringify(rows, null, 2);
    refreshFeatures(index);
  };
  el('template').addEventListener('change', () => refreshFeatures());
  refreshFeatures();
  function requestBuilding() {
    const number = (id) => Number(el(id).value);
    const result = {
      style: el('style').value,
      seed: number('seed'),
      width: number('width'),
      height: number('height'),
      depth: number('depth'),
      textureResolution: number('quality'),
    };
    if (el('role').value !== 'custom') return { ...result, mode: 'preset', function: el('role').value };
    const features = JSON.parse(el('features').value);
    if (!Array.isArray(features)) throw Error('Параметр features должен быть массивом');
    if (el('sliding').checked) features.push({ ...baseFeatures.find((f) => f.kind === 'sliding-roof') });
    return {
      ...result,
      mode: 'features',
      floors: number('floors'),
      roomsX: number('roomsX'),
      roomsZ: number('roomsZ'),
      roofProfile: el('roof').value,
      roofRise: number('rise'),
      interiorProfile: el('interior').value,
      features,
    };
  }
  function disposeModel(root) {
    const textures = new Set(),
      materials = new Set();
    root.traverse((o) => {
      if (!o.isMesh) return;
      o.geometry.dispose();
      for (const m of [].concat(o.material)) materials.add(m);
    });
    for (const m of materials) {
      for (const v of Object.values(m)) if (v?.isTexture) textures.add(v);
      m.dispose();
    }
    for (const t of textures) {
      t.image?.close?.();
      t.dispose();
    }
  }
  el('settings').onsubmit = async (event) => {
    event.preventDefault();
    const button = el('generate');
    if (button.disabled) return;
    button.disabled = true;
    el('buildMessage').textContent = 'Генерация модели…';
    try {
      const req = requestBuilding(),
        engine = await buildingModule;
      await new Promise((resolve) => setTimeout(resolve, 35));
      const { result: out, bytes: data, recipe: updated } = generateAsset(engine, req);
      const parsed = await new GLTFLoader().parseAsync(
        data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength),
        '',
      );
      building.remove(model.scene);
      disposeModel(model.scene);
      model = parsed;
      building.add(model.scene);
      recipe = updated;
      recipe.physicalSockets ??= [];
      URL.revokeObjectURL(el('downloadModel').href);
      if (el('downloadRecipe').href.startsWith('blob:')) URL.revokeObjectURL(el('downloadRecipe').href);
      el('downloadModel').href = URL.createObjectURL(new Blob([data], { type: 'model/gltf-binary' }));
      el('downloadModel').download = req.style + '-' + req.seed + '.glb';
      el('downloadRecipe').href = URL.createObjectURL(
        new Blob([JSON.stringify(updated, null, 2)], { type: 'application/json' }),
      );
      el('downloadRecipe').download = req.style + '-' + req.seed + '.recipe.json';
      updatePlan();
      selectSocket();
      show('building');
      el('buildMessage').textContent = 'Готово · ' + out.triangles.toLocaleString('ru-RU') + ' треугольников';
    } catch (error) {
      el('buildMessage').textContent = 'Исправьте параметры: ' + error.message;
    } finally {
      button.disabled = false;
    }
  };

  show('building');
}
start().catch((error) => {
  status('Ошибка запуска: ' + error.message);
  console.error(error);
});
