# Cutout mip coverage

Ordinary uncompressed model base-color images used exclusively by alpha-test
materials now retain texture coverage in smaller mip levels. This targets thin
needles/leaves disappearing in the distance while geometry LOD remains present.
The first/source level and CPU picking alpha stay byte-identical. RGB reduces in
linear light with alpha weighting; one transparent edge texel receives color
bleed for bilinear sampling. Smaller levels derive from unscaled alpha, avoiding
cumulative dilation. Histogram selection approximates original coverage at the
material cutoff/base-alpha factor; tiny mips have unavoidable discrete limits.
Per-vertex/draw alpha and custom graph logic remain shader-owned.

Common owner: engine/render/AlphaCoverageMips.h and Model.cpp uploadImage path.
Native RHI allocates explicit RGBA8 mips and uploads each subresource. No shader,
GPU uniform layout or genre dependency was added. A backend unable to upload
these levels falls back to ordinary generated mips and reports
Image.alphaCoverageMipmaps=false. The shared texture cache includes the derived
byte threshold in GPU identity, so different cutout policies do not borrow the
same storage. Image CPU flags append to the C++ structure: rebuild native clients;
no glTF/document/save version change.

Nearest/pixel-art, authored compressed mip chains, virtual fallback images and
images also used as opaque/blended/data/graph textures keep their existing path.
Bounds: <=4096pixels per dimension and <=4Msource pixels. Image.preserveAlphaCoverage=false
opts out BEFORE upload. Mutable images must opt out or explicitly regenerate their
whole chain on edits. Native Horizon atlas does so because its partial patches
rely on backend automatic mip generation; patching rejects manual coverage chains.
AIVERSE_ALPHA_COVERAGE_MIPS=0 disables derivation for load-time A/B; default on.
Changing source cutoff/content requires normal asset reload, not in-place mutation
of a shared immutable texture. RHI TextureDesc defaults remain unchanged.

Actual acceptance on KINGSTON: CPU alpha_coverage_mips, mock model_batching_policy
including cache separation/fallback/nearest/shared policies/base-alpha factor,
real metal_texture_gpu fragment alpha-test and existing terraweave_horizon all pass.
A separate native pine A/B uses identical4140triangles; primaryAlphaCoverageImages
reports0/1 when disabled/enabled. This fixes filtering, not crown morphology,
smooth branch welding, snow coats, gameplay ecology or universal picking.
