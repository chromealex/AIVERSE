# Contact-hardening sun shadows

Build the opt-in `aiverse_hdr_scene` and (macOS) `aiverse_hdr_gl` targets with
`AIVERSE_BUILD_HDR_TESTS=ON`. Both runners use `ContactShadowScene.h` when
`AIVERSE_CONTACT_SHADOW_TEST=1`; their normal HDR fixtures remain unchanged.

Run each backend twice with `AIVERSE_HDR_CAPTURE=/absolute/path.ppm`:

- `AIVERSE_CONTACT_SHADOW_TEST=1`: PCSS, enabled by default at medium/high.
- Add `AIVERSE_SHADOW_FIXED=1`: fixed-radius reference using the same camera,
  geometry, map size, bias, light and PCF filter.

Repeat with `AIVERSE_SHADOWS=medium` and `low`. Low skips blocker search and
must match its fixed-radius reference. Inspect the wall's shadow edge: sharp
at the floor contact, progressively wider toward its far end. The detached
small block must retain a soft shadow. Check that the floor has no self-shadow
acne and the wall shadow stays attached. Also run the normal HDR fixture with
`AIVERSE_SCREEN_LIGHTING=1` to check the raw sun map's volumetric consumer.

`python3 tests/render/check_model_lod_shaders.py` compiles and links desktop
GLSL 330 and GLES 300 model variants. Actual Android/WebGL GPU validation is
separate from that shader compiler check.

The expected capture names and image assertions are in
`verify_contact_shadows.py`; run it against the capture directory.

Implementation: nine raw blocker-depth reads on medium/high, receiver-plane
correction during blocker search, and a directional-light penumbra slope of
0.05 world units per unit separation, capped by `Lighting::shadowSoftness`.
The minimum contact kernel is half a shadow-map texel. Metal retains hardware
bilinear comparisons; GL/GLES uses four raw texel reads per bilinear comparison
so blocker depths and PCF can share one texture without another sampler slot.
`Lighting::shadowContactHardening=false` selects the reference fixed radius.
