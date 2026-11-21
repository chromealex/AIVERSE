# Shared native game host

`apps/client/HostedClient.h` exposes `aiverse::client::HostedClient` through the
`aiverse_hosted_client` CMake target. The legacy editor name
`aiverse::editor::ClientHost` remains a source-compatible alias. Both editor
shells must use this runtime instead of implementing another simulation or
game client. This is the existing GameClient host extracted from the legacy
editor, not a new gameplay implementation.

The application owns its `gfx::Device`, frame begin/end and native surface.
Pass the device owner's `RenderDeviceLifetime::Lease` to `start`. One live
device must have one generation. `frame` installs the hosted platform size,
content root, safe area and target textures while invoking GameClient, then
restores the previous platform and overlay context. The existing in-game
overlay renderer remains internal to GameClient; no editor controls belong
there.

`place` supplies the viewport origin, scale and keyboard focus. `event` maps
pointer positions, captures matching presses/releases and optionally maps
left-button input to touch. Call `suspendInput` when the surface is hidden or
loses focus, so held input cannot leak into another editor panel.

`stop` retires the client behind an actual GPU submission fence. Call
`collectGpuRetirements` between submissions. Before destroying the device,
stop the client and prove GPU idle before `drainAfterGpuIdle`; on terminal
device loss use `abandonOnDeviceLoss`. Never replace this with guessed frame
ages or destroy the device while the host still references it.

The extraction does not provide HTML5 Play controls, native FPS play, pause,
single-step, runtime inspection or a device-simulator panel. Those remain
explicit migration work in `docs/studio-html5-parity.md`. A successful library
build or stopped-host test is not evidence of rendered gameplay parity.
