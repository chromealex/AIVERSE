# Object volume CSG native stage

Stage commit 892320df792f951063c57c613a15a56273216cc1 on codex/modeling-volume-csg adds tools/modeling/VolumeCsg.cpp, Geometry.cpp dispatch, Modeling.h declarations, Document.cpp catalogue, CMake target inputs, and examples/modeling/volume-csg.json. Root owner is Modeling. Shared Caverns mesh extraction and lib_noise remain the surface and noise implementations.

Focused Mac x86_64 native workspace test reopened the source across processes and exported 7,662 triangles at bore 0.3 and 7,736 at 0.5. Undo restored the initial GLB bytes. Next stage adds Studio, SPVT input and a packaged regression.
