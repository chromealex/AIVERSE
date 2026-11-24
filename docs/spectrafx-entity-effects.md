# SpectraFX: effects declared in data, and one CPU/GPU path

Two capabilities, both genre-independent:

1. **Entity effects in data.** Rules, maps, unit types, weapons and abilities declare which effects an entity carries
   and which effects its events play. The effects start when the entity appears, follow it and stop when it dies or
   leaves the scene. No game code: every host feeds the same presenter (`engine/fx/SpectraFxEntityEffects.h`).
2. **One asset, GPU or CPU.** A SpectraFX GPU emitter asset `fx/gpu/<name>.gpu.json` runs on the Metal compute kernel
   when it is available and on the CPU twin of the same kernel otherwise. Both read the same seeds and draw through
   the same billboard renderer, so the two paths give the same picture (`tests/spectrafx_cpu_gpu_parity.mm`).

## Data format

A `"spectraFx"` block in `rules.json` and/or the map JSON. The map is read after the rules, and its rules override the
rules' rules selector by selector.

```json
"spectraFx": {
  "entities": {
    "type:townhall": { "attach": [ { "effect": "hearth_embers", "offset": [0, 2.0, 0] } ] },
    "unit": {
      "attach": [ { "effect": "fire_smoke", "when": "damaged", "below": 0.35 } ],
      "on": { "spawned": "unit_ready", "died": { "effect": "explosion", "scale": 0.8 }, "fired": null }
    }
  }
}
```

The same rule object can also sit **inline** on a data entry as `"spectraFx": { "attach": [...], "on": {...} }`:

| Where                                    | Selector it defines        | Host                 |
|------------------------------------------|----------------------------|----------------------|
| rules `types.<id>`                       | `type:<id>`                | RTS client           |
| rules `types.<id>.weapons[i]` (`id`)     | `weapon:<id>`              | RTS client           |
| rules `types.<id>.abilities[i]` (`id`)   | `ability:<id>`             | RTS client           |
| rules `actor.weapons[i]`                   | `weapon:<i>` (index)       | FPS client           |

### Rule

- `attach`: effects that live as long as the entity. Each entry is an effect name or an object:
  - `effect` (name, required): `fx/gpu/<name>.gpu.json` if that file exists (GPU or CPU twin), else `fx/<name>.json`
    (CPU `ParticleSystem` effect);
  - `offset` `[right, up, forward]` in metres in the entity's heading frame (default `[0,0,0]`);
  - `when`: `always` (default), `moving`, `idle`, `damaged` (health under `below`, default 0.5);
  - `direction`: emission axis `up` (default), `forward`, `backward`;
  - `scale` (default 1), `kill` (true: particles vanish when the attachment stops; default false: they fade out).
- `on`: `{ "<cue>": effect | null }`. One-shot effects at the entity. `null` switches off a binding inherited from a
  more general selector. `seconds` (default 3) bounds how long a GPU one-shot owns its buffers.

### Selectors and merging

A host gives every entity a list of selectors, general first. All matching rules are merged in that order:
`attach` lists add up; for `on`, the later (more specific) selector wins.

| Host        | Entity       | Selectors (in order)                                                                  |
|-------------|--------------|---------------------------------------------------------------------------------------|
| RTS client  | unit / building | `*`, `unit` or `structure`, `type:<id>`, `player:<n>`                              |
| RTS client  | projectile   | `*`, `projectile`, `type:<id>`, `player:<n>`, `weapon:<id>` (weapon that fired it)    |
| FPS client  | character    | `*`, `actor`, `player:local` or `player:remote`, `team:<n>`, `weapon:<index>`         |
| FPS client  | vehicle      | `*`, `vehicle`                                                                        |
| FPS client  | projectile   | `*`, `projectile`, `projectile:<kind>`, `weapon:<index>`                              |

### Cues

| Cue          | When                                                                | RTS | FPS |
|--------------|---------------------------------------------------------------------|-----|-----|
| `spawned`    | entity first presented alive                                         | yes | yes |
| `died`       | health reaches 0 or a death event; attachments stop                  | yes | yes |
| `despawned`  | entity left the scene while alive (fog, removed)                     | yes | yes |
| `fired`      | it attacked / shot; also plays the `weapon:<id>` rule's `fired`      | yes | yes |
| `hit`        | it was hit (hit event, or its health dropped)                        | yes | yes |
| `ability`    | it cast an ability; also plays `ability:<id>` rule's `cast`          | yes | no  |

## GPU emitter asset and the CPU twin

`fx/gpu/<name>.gpu.json` (full field list: `engine/fx/SpectraFxGpuEmitterAsset.h`):

```json
{ "version": 1, "preference": "auto", "particles": 512,
  "appearance": { "additive": true, "radius": 0.09, "color": [1.0, 0.55, 0.18] },
  "spawn": { "rate": 70, "burst": 20, "life": 2.6, "speed": 1.2, "spread": 0.45, "seed": 11 },
  "environment": { "acceleration": [0.15, 0.7, 0], "damping": 0.985 } }
```

- `preference`: `auto` (GPU when the RHI has compute, else the CPU twin), `gpu`, `cpu` (always the CPU twin).
- `cpuFallback`: `twin` (default: the same asset on the CPU) or `effect` (legacy: play the separately authored
  `cpuEffect`). `cpuEffect` is optional; it is still used when the playback's GPU slot budget is full or a GPU emitter
  fails at runtime.
- Metal: compute writes an RGBA32F position texture the billboard vertex shader samples; regular frames read nothing
  back. CPU twin: `spectraFxEvolveParticle` (`engine/fx/SpectraFxEvolutionKernel.h`, line-for-line copy of the
  kernel) integrates the same seeds; live particles are packed (back-to-front for alpha) into a 3-texture ring.
  Keep the kernel and `SpectraFxComputeShaders.h` in lockstep.

## Hosting the presenter in a new host

```cpp
fx::SpectraFxSceneHost host;                       // or your own ParticleSystem + HybridPlayback
host.init(device, reader);
host.configureEntityEffects(rules, map, {{"weapon:", &rules["weapons"], "id"}});
// each frame:
auto& fx = host.entities();
fx.beginFrame(dt);
fx.entity(id, fx.rule({"*", "vehicle", "type:" + kind}), {position, forward}, alive, health01);
fx.cue(id, "fired");
fx.endFrame();
host.prepareGpu(dt, view);   // flushes queued GPU cues at the safe point
```

## Verification (2026-11-22, Mac, Metal)

- `spectrafx_cpu_gpu_parity`: 4096 particles, 180 frames with gravity/damping/ground bounce: identical alive sets,
  position error p99 6e-7 m, max 1.6e-6 m; GPU vs CPU-twin emitter images identical (max delta 0); perf print.
- `spectrafx_entity_effects`: table merge and presenter lifecycle; `spectrafx_retirement`: compute-less RHI picks the
  CPU twin automatically and releases every resource.
- Templates: `templates/skirmish` (RTS) and `templates/fps_arena` (FPS) declare effects only in data.
