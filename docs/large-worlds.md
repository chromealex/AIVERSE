# Large worlds: chunks and a memory budget

`AIVERSE::world` provides a shared loader, `world::ChunkStreamer`, for the game host. It does not depend on FPS, the GPU,
threads or the file system. The host defines the chunk catalog, observers, pinned chunks and the functions that load and
unload resources. The existing FPS client uses it through `ChunkScene` for the earlier box scenes. For terrain, models and
collision there is an asynchronous `world.async` path, described below.

## Capabilities

- A sparse catalog of up to 100,000 chunks; neighbouring chunks are found through an index, without walking the whole
  catalog when an observer moves.
- Several observers with an independent radius of 0..4 cells on each axis. Overlapping areas are loaded once.
- Priority: pinned chunks, the observer's cell, then the nearest rings. Equal priorities are resolved by coordinates,
  independently of observer order.
- A hard limit on the sum of **resource reservations** and the number of loaded chunks. The reservation is declared by
  the resource adapter: it must include all resources owned by the chunk and must not be exceeded while loading. It is not
  a measurement of the process RSS.
- A limit on load attempts per update. Failures are retried after a set number of updates and do not block the head of
  the queue every frame.
- Unneeded chunks are released before new ones are loaded. On teleportation and with damaged files, old worlds do not pile
  up in memory.
- `StreamStatus`: required/loaded chunks, memory reservation, pending loads, chunks that did not fit the budget, errors,
  the number of loads and unloads per update.

Pinning is a higher priority, not a bypass of the memory limit. If pinned chunks do not fit, `ready()` stays false. An
unknown pinned chunk also prevents readiness. The host must check readiness before admitting a player into an area whose
resources it requires. Removing all observers and pins releases all resources even with a zero load budget.

## Configuring the existing scene loader

The `world` block in a map:

```json
{
  "world": {
    "cellSize": 256,
    "coordinates": "local",
    "maxResidentBytes": 67108864,
    "maxResidentChunks": 512,
    "retryUpdates": 30,
    "chunks": [
      {"cell": [0, 0, 0], "file": "chunks/start.json", "maxBoxes": 200},
      {"cell": [10000, 0, 0], "file": "chunks/distant.json", "maxBoxes": 200}
    ]
  }
}
```

A chunk file:

```json
{"boxes": [{"min": [0, 0, 0], "max": [8, 3, 8]}]}
```

With `coordinates: "local"`, coordinates are measured from `cell * cellSize`. The offset is computed in `Fixed`; the
conversion to float happens after the camera origin is subtracted, so small geometry keeps its precision far from zero.
Valid cell coordinates: -100000..100000, size: 16..4096. This is the scene representation range; existing game modules
may have their own, narrower limits on the world and collision.

Without `coordinates`, the old absolute-coordinate format is kept. Without budgets, 64 MiB, 512 chunks and a retry after
30 updates are used. `maxBoxes` defaults to 10000 (valid 1..10000). A chunk's reservation equals
`maxBoxes * sizeof(world::Bounds)`: give a real upper bound so as not to reserve excess memory. A file exceeding this limit
is not published. The reservation covers the Bounds array; the catalog, containers and temporary JSON parsing are not
included. One file is limited to 4 MiB after reading. The Reader itself must limit the incoming stream if protection
against allocation while reading untrusted files is needed.

`configure()` first validates the new catalog and only then replaces the scene. A bad manifest leaves the previous scene
available.

## Other kinds of resources

```cpp
world::ChunkStreamer stream({64 * 1024 * 1024, 512, 30});
stream.add({0, 0, 0}, 1024 * 1024); // upper bound of this chunk's resources
world::StreamObserver observer{{0, 0, 0}, 2};
auto status = stream.update({&observer, 1}, {}, 2,
    [&](world::Cell cell) { return resources.loadAtomically(cell); },
    [&](world::Cell cell) { resources.release(cell); });
```

Loading must either publish the whole chunk within its reservation or return `false`, releasing everything allocated.
Unloading releases all of the chunk's resources. The functions must not throw exceptions or re-enter the loader. It is
synchronous: the attempt limit bounds the number of loads but does not guarantee frame time. The number of observers and
pins is controlled by the caller. When the loader is replaced/destroyed, the host releases its resources (for example,
with an update with empty interest).

This synchronous API is kept for compatibility and small box scenes. The asynchronous adapters below use the same
residency scheduler.

## Check

```sh
cmake --build <build> --target aiverse_world_tests aiverse_fps_tests
ctest --test-dir <build> -R '^(world_streaming|fps)$' --output-on-failure
```

The tests check several observers, pinning, budget overflow, teleportation, load errors and recovery, negative/extreme
cells, local coordinates at a distance of 409,600,000 units, atomic manifest replacement and a pass over 20,001 chunks with
a bounded number of resident resources.


## Asynchronous terrain, models and collision

A working example: `examples/world/streamed`. Run:

```sh
<build>/modules/fps/aiverse_fps_client --project examples/world/streamed
<build>/modules/fps/aiverse_fps_server --project examples/world/streamed --ticks 600
<build>/aiverse_headless --project examples/world/streamed --max-seconds 5 --verify-restore-at 1 --verify-restore-ticks 30
```

`examples/world/generate_streamed.py` reproduces the original assets: 16 terrain chunks of 128×128, arches with a real
opening, OBJ/MTL and the matching collision triangles. The generator also computes the SHA-256 of all files. This is a
separate FPS example; the existing RTS heightfield/MapGrid is not converted to this format automatically.

### Manifest

In the map, set `world`:

```json
{
  "async": true,
  "version": 1,
  "cellSize": 128,
  "maxResidentBytes": 41943040,
  "maxResidentChunks": 20,
  "maxStagingBytes": 8388608,
  "maxUploadBytes": 4194304,
  "maxJobs": 2,
  "retryUpdates": 30,
  "assets": {
    "chunks/0_0.json": "<64 lowercase hex characters>",
    "arch.obj": "<64 lowercase hex characters>",
    "arch.mtl": "<64 lowercase hex characters>"
  },
  "chunks": [
    {"cell": [0, 0, 0], "file": "chunks/0_0.json", "reservedBytes": 2097152}
  ]
}
```

The hashes in this snippet are placeholders; real valid hashes are in the example. `assets` lists **all** dependencies,
including the model's buffers/textures/MTL. A file that is not in the list or whose hash changed is not published. The
hashes are part of the content identity of a network session and of saves. Async maps get a runtime version tag: an old
client that only knows box scenes will not connect to a new async map. Regular maps keep their earlier identifiers and
formats.

The asynchronous format divides the world into X/Z columns: the cell's Y coordinate is zero, object height inside a
column is free. Up to 32768 chunks. For collision, the `SpatialWorld` ranges (±1000000 units) and the narrower
`fps.worldLimit` still apply. Column size 16..4096, data coordinates are local. Mesh and collision bounds on X/Z must fit
in their column; an object crossing the border must be split in advance.

### Chunk file

```json
{
  "terrain": {"resolution": 2, "heights": [0, 1, 0, 1]},
  "meshes": [{"file": "arch.obj", "position": [32, 0.25, 64]}],
  "boxes": [{"min": [5, 0, 5], "max": [8, 4, 8]}],
  "collision": {
    "vertices": [[0, 0, 0], [8, 0, 0], [0, 8, 0]],
    "indices": [0, 1, 2]
  }
}
```

The blocks are optional. `terrain.resolution` is 2..129 vertices per side; `heights` are absolute local heights in world
units, row by row in Z, then X. The diagonal of each cell is (0,0)–(1,1). The visual mesh and the collision use the same
triangles. The optional `terrain.material` sets `color: [r,g,b]`, `baseTexture` and `normalTexture`; the files are also
listed in `assets`. `terrain.uvScale` sets texture repeats per world unit; the UV phase is consistent across columns.
Neighbouring resident terrain chunks must have the same resolution and matching edge heights; otherwise collision
publishing is rejected.

`meshes` are up to 256 instances of static models supported by `loadModel` (including OBJ and glTF). Models and textures
are decoded; repeated files within a chunk share one model. `position`, a positive `scale` and `rotation` in degrees are
supported. Skins, clips and morphs require a regular entity and are rejected. A model's collision is set separately with
exact triangles in `collision`, not invented from its AABB. `boxes` are collision only; an async scene's visuals are
explicit.

Triangles are processed through the existing spatial index and a swept-AABB SAT narrow phase on `Fixed`. This applies to
the motor, rays/shots, grenades and free-space checks. Openings are not closed by the model's bounding box.

### Threads, cancellation and memory

`AsyncChunkStream` is a shared two-phase adapter on top of `ChunkStreamer`:

1. The main thread selects chunks and reserves memory.
2. One worker thread reads/verifies files, decodes models and builds terrain/collision.
3. The main thread accepts the finished results. `WorldScene` creates GPU resources, `CollisionStream` adds collision only
   between ticks.
4. A task that is no longer relevant is cancelled logically. Its staging reservation stays occupied until the work
   finishes; a late result cannot bring back an unloaded chunk.

`maxResidentBytes` covers the declared reservations of the selected chunks, including those waiting for
preparation/publishing. `maxStagingBytes` separately limits running, pending and cancelled tasks at the same time;
`maxJobs` limits the queue, there is one worker thread. The decoded payload is checked against a conservative estimate
before publishing. `maxUploadBytes` limits the sum of reservations of chunks published per frame; one chunk must fit
whole. By default, no more than two publishes and four new requests per visual scene update. The GPU API is called only on
the render thread.

These are resource budgets, not a strict RSS limit: the catalog, allocator, temporary JSON, decoders and driver use
additional memory. A single file is limited to 32 MiB before reading from a file/pack in the standard
`platform::makeContentReader`; a custom Reader must honour the same contract. A very heavy single mesh or texture must be
split in advance: a chunk's GPU upload is indivisible, a hard frame-millisecond limit is not guaranteed.

`platform::makeContentReader` creates an independent Reader with a fixed root, its own `content.rpak` and local split
chunks. From the background thread it does not touch the mutable state of the hosted view/CWD/pack mounts. Content that
is not available locally remains a load error; automatic network delivery of DLC is not added. An APK-asset Reader for an
Android host must be passed separately. In a web build without pthreads, preparation runs cooperatively, one task per
update, not on a background thread. This fallback must not be advertised as non-blocking browser I/O.

### Authority and saving

Before a tick, FPS computes a conservative set of chunks for all actors, weapon ranges, movement, vehicles, projectiles,
explosions and respawn points. If any needed collision is not ready, `GameModule::readyToStep` forbids the tick.
`Simulation::step` does not change the tick number, RNG, events or the command queue. The server does not take input
commands until it is ready. Disk speed changes the waiting time, not the result of the tick.

The server budget must fit the union of the areas of all players and respawn points. An insufficient budget does not allow
a simulation with holes: the server keeps waiting. This is a safe stop of the whole session, not a distributed simulation
of independent regions. On the client, prediction does not move the actor into an area without collision.

Static collision is a recoverable cache of immutable, hashed content. It is not written into the save; after restoring,
the host loads the required chunks before the first tick. Dynamic actors are saved by the existing mechanism. Mutable
terrain, unloading/saving of dynamic entities and a streamed navmesh are not added.

### Async path checks

`aiverse_world_async_tests` checks real work on another thread, cancellation with late completion, keeping the staging
reservation until completion, publishing on the owner thread, SHA-256, bad seams, triangles, movement on a slope and an
identical continuation of the simulation after a cold save load. An integration test passes through an arch and a chunk
border through the real authority/client loopback, then checks stopping before the arch's column. The test's working
directory is the engine root.

```sh
cmake --build <build> --target aiverse_world_async_tests
ctest --test-dir <build> -R '^(world_async|world_streaming|fps)$' --output-on-failure
```


Checking the real GPU lifecycle: `aiverse_world_preview` goes through three camera positions, fully unloads the scene,
then loads it again. It checks the number of resident chunks and the reservation at each stage and exits with an error on
failure.

```sh
AIVERSE_SCREENSHOT=/tmp/world.ppm AIVERSE_EXIT_AFTER_FRAMES=400 <build>/modules/fps/aiverse_world_preview examples/world/streamed
```

## Статичный мир в любом жанре: общие модели и клиент RTS / top-down

Потоковая сцена не привязана к жанру. Клиент `AiverseGame` (RTS, top-down и прочие игры на модуле `rts`) читает
тот же блок из ключа карты **`streamedWorld`** — в этих картах `world` занят планетной системой; FPS-карты
по-прежнему держат блок в `world`. Клиент подгружает участки вокруг точки, на которую смотрит камера
(`viewRadius` 0..4 участка, по умолчанию 2), и рисует их тем же `Renderer3D`. Симуляция о сцене не знает:
препятствия остаются клетками карты.

Два ключа манифеста для миров, собранных из набора повторяющихся моделей (город из типовых зданий):

- `"shareModels": true` — файл модели декодируется и загружается в GPU один раз и живёт, пока он нужен хотя бы
  одному резидентному или готовящемуся участку. Общие модели не входят в `reservedBytes` участка (участок
  платит 4096 байт за ссылку): их суммарный объём ограничен числом разных файлов контента, а не бюджетом.
- `"meshOverhang": N` — на сколько единиц меш может выходить за границу своей колонки (по умолчанию 0, как
  раньше). Нужен для зданий на стыке участков; коллизий это не касается.

`tools/world/make_chunks.py <проект> [--map maps/x.json] [--cell 32] [--radius 2] [--prune-types]` переносит
статичные объекты карты (здания, деревья, пропсы — по тегам типов, с моделью без `fit` / `center`) из `entities`
в файлы участков `<карта>.chunks/<x>_<z>.json` и пишет блок с SHA-256 всех файлов. Клетки под зданиями
становятся `#` (в картах с 2D-тайлами — терреном тайлсета с `"cell": "#"`). Перенесённые деревья и пропсы
перестают быть препятствиями. Проверено на городе 384 × 384: 4120 объектов в 144 участках, в симуляции
осталось 766 объектов.

Не проверено тестами: `shareModels` и `meshOverhang` покрыты только запуском клиента под Xvfb.
