# Texture RHI Stage 3 — Metal class scope correction

Branch: integration/texture-rhi-main-stage3-20261120
Stage HEAD before merge: bbda0febbbc9cca26c735046c4cffe9270944c20
Merged main snapshot: f9aed9a705982e2c6b77b29869e309995ce8b163

## Root cause
The three-way merge resolution omitted the closing brace of
MetalDevice::completedFrameSerial() immediately before gpuSubmissionFence().
This caused a cascade of MetalDevice member and pure-virtual errors.

## Changed file
engine/gfx/metal/GfxMetal.mm — restore one missing function brace.

## API
No changes to public interfaces, GPU logic or texture formats.

## Verification
The exact archived file was inspected and the brace structure checked.
Native Mac compilation after this correction: NOT YET RUN.

## Next
Rebuild Metal backend and full engine. Resolve subsequent compile
errors if any. Then run full CTest and native Metal GPU acceptance.
Record results and merge SHA before commit/push.
