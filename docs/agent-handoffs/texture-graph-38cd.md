# Stage 38CD — GPU BC and native compressed HDR import

Parent: `5d21be129866313e418de92d058812851f209ffd`. Branch: `feature/aiv20261124_texture_graph_gpu_slots`.

- Direct GPU block encoding BC1/3/4/5 from graph packed RGBA8, bounded compressed-only readback and CPU decoded-error validation. Base mip only; existing CPU path supports full mip chains. Studio explicit GPU BC action and actual compression backend diagnostic.
- Native BC5 for graph-authored normal DDS with renderer U2 flag, hash, and positive-Z reconstruction; no generic custom graph BC5 sampling. BC6H DDS/KTX2 import tagged linear HDR, native-only and no silent LDR fallback. No BC6H encoder yet.
- GPU and CPU test fixtures authored, not executed. Main not merged.
