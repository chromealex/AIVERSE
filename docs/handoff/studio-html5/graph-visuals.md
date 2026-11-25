# Studio graph visuals

Scope: Shader Graph and SpectraFX topology editors. Verify node titles, connection endpoints, selected signal paths, keyboard selection, and scrolling at narrow widths. Preserve existing native graph formats, persistence and edit operations. Capture populated graphs and run web, Studio contract and integrated Mac checks.

Implementation 0e35087c4e8af523104790433aa68fc92e56636c. Modified the two existing graph renderers, shared graph surface styles, shader layout adapter and StudioShaderGraphDocument value type. The native compiler, graph file formats, asset operations and undo authority remain with their existing owners.

The production bundle compiled. All 196 Studio web tests and all 50 Python Studio contracts passed. Playwright loaded the repository's sg_brass and runtime_math_connected_v2 samples in the real panel components with fixture bridge data at 1100 and 240 CSS pixels. Checks covered keyboard selection, highlighted edges, shader numeric values, pointer drag and discard, SpectraFX dashed value links and zoom. The shader layout regression checks sixteen distinct inputs on a single destination.

Native Studio captures, the integrated Mac quick and publication are the remaining delivery checks. No headless simulation, new platform API, new MCP operation or schema migration is involved. Windows native QA was not run.

Native visual review led to 5326427882985b4fbffa73baa6e4dc4bdddacee3 (grouped zoom controls and taller topology surface) and dbfb3dcd74eb3bfbeaa1066bed6e9a1f31d6de5b (word breaks for long type names). The detached native SpectraFX window compiled runtime_math_connected_v2 with one emitter and displayed its solid execution and dashed value wires. The shader screenshot shows sg_brass values and its RGB swatch. Both frames are in docs/devlog/media/2026-11-25-studio-graph-visuals/. Final production compilation and all 196 web tests passed. Editor tooling is illustrated with native frames under the publishing contract.
