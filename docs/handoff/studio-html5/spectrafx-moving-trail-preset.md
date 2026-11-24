# Studio moving smoke trail preset

Implementation commit `aed9dda05` adds **Moving smoke trail** to the SpectraFX graph preset selector and **Leave a trail** to the VolumeDomain attachment selector. The existing new-volume and append-volume commands both use the native preset compiler. To customize an existing gas graph, change its domain attachment and size its bounds for the source's route.

The native preview capture shows the editable example with a moving nozzle. The preset itself is covered by a graph compilation test. The TypeScript check and production web build pass. Four docking-layout tests and ten Python Studio contract tests also fail at the pre-change revision. Interactive editing and reopening a saved game project remain to be exercised.
