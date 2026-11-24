# Caverns Stage55e — header isolation and racing fixture compile fixes

Date: 2026-11-24. Branch `integration/caverns-studio-stage54c-20261124`. Starting HEAD `aa6e1a56e2c7097551de3cc520a3fe1b928b4167`.

Mac build reached 52%; region_ecs_bridge linked, then native micro physics compile and race_region_respawn_gate failed. Log: test-reports/20261124-134032-10040/build.log.
- Close aiverse::terraweave::caverns in TerraWeaveCavernsShardCatalog.h (missing closing brace at EOF). This leaked the namespace into later navigation includes and caused all visible nested namespace errors. Do not create namespace aliases or duplicate navigation types.
- Existing shard GPU host fixture includes the independent shared GraphSearch header after host header and checks global ::aiverse::nav::GraphEdge; compile-time regression for the include-order leak.
- Include VehiclePersistence.h explicitly in race_region_respawn_gate_test.cpp, which uses its existing vehicleHash API.
- Remove unused parameter name from pumpGpuBatch while preserving the public signature; continue using frozen gpuBatchSpec_ for all staged shards.
- No tests disabled or diagnostics suppressed. Source committed; Mac full build and CTest NOT RUN here; diagnostic error limit means further compilation failures may remain. Native GPU/GI/playable traversal acceptance pending. Main unchanged.
