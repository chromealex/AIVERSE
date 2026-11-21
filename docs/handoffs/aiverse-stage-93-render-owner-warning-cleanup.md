# AIVERSE Stage 93 — resource owner warning cleanup without semantic change

Date: 2026-11-20
Branch: integration/prisma-motionweave-view
Previous: docs/handoffs/prisma-stage-92-model-image-transaction-mock-signature.md
Base commit: 5df84e27642b1e9d683ff2437fa161ae5daac852

## Mac build report
Multiple -Wunused-lambda-capture warnings in ModelResourceCache.h and
RenderResourceOwnerGroup.h, plus two -Wmissing-field-initializers
warnings in RenderResourceOwnerGroup::Registration aggregates.

## Exact changes
- engine/render/ModelResourceCache.h: remove an unneeded capture of
  `max` in the pure saturating-add lambda (one occurrence).
- engine/render/RenderResourceOwnerGroup.h: remove two unnecessary
  captures of the local integral constant `max` from saturating-add
  lambdas; neither lambda mutates external state.
- Explicitly value-initialize the trailing RenderDeviceLifetime::Lease
  `epoch` in two legacy Registration aggregates. The generational
  `registerScoped(lease,...)` overload retains its real lease.
  Explicit {} equals the previous implicit default construction;
  the existing boundEpoch_ logic is unchanged.

## Testing and safety
Read current GitHub sources before editing; direct transformations were
checked against exact anchors. Native CMake/CTest/Metal/Vulkan/GLES
NOT RUN after this source change. No GPU teardown, retirement, or
ownership policies were modified. Keep the clean Mac build running
and only rebuild after it finishes. Do not touch main/source branches.
