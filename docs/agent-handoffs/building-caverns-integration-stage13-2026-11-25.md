# Optional Caverns rim publication in the native terrain driver

Code commit `47a2709eae394c751dfebe206c111b392f6bc4da` changes `engine/world/TerraWeaveNativeDriver.h`. Its Box3D-backed rim publication entry point is available to consumers with the optional Box3D headers. The ordinary native terrain path compiles with the base target's include directories.

The unfiltered build portion of the Caverns Mac gate stopped at 77% on `f8e433eb2`. The base native micro test indirectly included box3d/box3d.h without the physics dependency. The runner stopped before CTest. Evidence is in `/Volumes/KINGSTON/AIVERSE-builds/caverns-stage59/0f4394d81d798f28bfdf6b5e43991057a9e7636b/test-reports/20261124-211521-94719/build.log`.

On the fixed commit both native micro targets built and both corresponding CTests passed in 3.28 s. The gate cache used Debug on macOS with physics enabled, with the base fixture compiled without the Box3D include path. The next checks are the remaining native Caverns tests and Building acceptance.

Promotion requires reconciling the scope of the staged feature ancestry. Of 1,217 staged diff paths compared to the main merge base, 584 fall outside the union of the four named PR diffs. Examples include MotionWeaveTelemetry.h and TextureGraph.h. Path counts alone do not establish which changes are needed as dependencies.
