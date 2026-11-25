# Main camera shake (#156)

Acceptance covers data bindings, Lua requests, spatial attenuation, simultaneous events, pause, expiry, unmodified navigation state and match reset. Combined checks pending.
cfb41a07d4c14748098254a501124eddd912f10a
GameClient applies a transient copy to primary camera rendering. MainCameraShake uses the existing LENS noise sampler and distance attenuation. The presentation queue holds configured events through confirmation. Runtime checks pending.
b1065d6d86018f5fb86905ddb39e72f445ff108e
tests/tests.cpp includes the new camera regression. Examples are in docs/scripting.md and docs/game-format.md. Execution deferred until the backlog implementation is complete.
