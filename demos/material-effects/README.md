# Material effects

Three ordinary game scenes share the same model and native FX owners:

- Shadows: left casts a shadow, right disables model.castShadow. Both remain visible.
- Materials: left alpha, centre source-alpha-weighted additive, right unweighted additive. The same source model and alpha track are used.
- Particles: left linear HDR addition, right display-space addition. Texture, colour and emission match.

Choose the scene in the map menu (or use ?map=0, ?map=1, ?map=2 in the Web build). Shadows depend on the selected renderer quality. These examples use ordinary view.fx bindings and fx/*.json; there is no demonstration-only runtime path.

The source shadow fixture has retained native Metal A/B evidence. This combined demo still needs native and browser acceptance before publication; a data file alone does not establish backend parity.
