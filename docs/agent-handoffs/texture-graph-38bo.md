# Stage 38BO — GGX environment prefilter

Branch: `feature/aiv20261124_texture_graph_gpu_slots`. Parent: `9d611d5a9f4216025b49674a849395c130344a83`. No main merge.

- Replace cosine-power specular approximation in shared EnvironmentIbl convolution with Hammersley GGX NDF importance sampling, V=N=R split-sum assumption, NdotL weighting and normalization. Cosine-weighted diffuse irradiance/pi retained. Precompute tangent-space kernels per band (256 specular, 128 diffuse); no extra GPU resources or shader binding slots.
- Preserve existing 130x330 RGBA16F lat-long atlas, wrap/pole gutters, perceptual roughness 0/1/3/2/3/1 bands and diffuse band 4; existing Studio environment upload consumes the new result. Metal/GLSL model lookup clamps band interpolation.
- Authored analytical Hammersley, horizon rejection and roughness-one hemisphere moment checks in hdr_texture test, alongside existing constant/seam/directional tests. No compilation, runtime, hardware, GUI or timing measurements performed. Still a finite four-band approximation without PDF-based source mip sampling; analytic BRDF fit remains until LUT implementation. No new environment EXR integration in this stage.
- Next: integrated BRDF LUT, then remaining HDR/EXR and compressed/custom-resource gaps and consolidated QA.
