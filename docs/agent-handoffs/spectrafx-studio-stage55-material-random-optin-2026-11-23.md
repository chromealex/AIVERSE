# Stage 55 — explicit GPU material random opt-in

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits dbe83b2ebc38032128d717f78f23e212ce159608, 77218bc06d10b7302067c303c12c036637b7965e, 024294c9ae4be4192f99a8f87880d4d373d3eb2f, bab47ab6a3df145c1de03c738e733d0ed6a20452, ecc401656f7b11287cc12c615ad18f1cce8ff63c, e77c4db5986bf6cdb76ab83efa44e847879541c4.

GraphRuntimeOptions and ComputeEmitterDesc add gpuBirthRandom and gpuBirthSeed. SpectraGpuMaterial compiler accepts random lane only with opt-in. GraphRuntime eligibility and ComputeParticles initialization/pipeline pass the option. Skill updated. Tests: none, no compilation or GPU verification. CPU fallback has different random semantics. Next VM random field with explicit semantics, Studio/MCP seed plumbing, checkpoint audit and deferred QA.
