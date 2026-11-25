# Triplanar model material on Metal

Tested code 541870d27 on macOS. The normal and optional-colour-only recipes are ordinary material Shader Graph documents. Studio Material Library uses the asset GUID picker, persists through material_set and resolves referenced image assets for the normal static-model viewport. The shader uses world metres and world-space axis orientation. Moving objects move through the projected pattern. Shader Graph itself owns the compiled GLSL/MSL, validation, sample limits and texture dependencies.

Shader Graph and model_triplanar_frame both passed (docs/evidence/model-triplanar-metal.json). The real GPU readback used a rotated, nonuniformly scaled, imported UV-free OBJ and a fresh Modeling Boolean subtract exported and imported again as GLB. All UVs were zeroed. Three targets compare the normal map, colour-only, and a different repeat distance. Material application used AppearanceAuthoringService. The Metal device created 16 pipelines with zero failures.

Disabling normal detail changed 13,989 pixels on the OBJ and 40,550 on the CSG. Changing repeat length affected 11,799 and 30,537 pixels respectively. On the back wall, side wall and floor of the new cut, disabling normal detail changed 2,657, 512 and 728 pixels. Repeat changes on those same faces affected 2,117, 543 and 518 pixels. The colour-only variant uses three samples; colour plus normal map uses six. The three PNGs in docs/evidence/model-triplanar-{projected,colour,repeat}.png are actual render-target captures.

Studio frontend typecheck and production build passed. Native Studio assignment/reopen and MCP registration are still being verified. Final integration, device perf and public demo acceptance are pending.
