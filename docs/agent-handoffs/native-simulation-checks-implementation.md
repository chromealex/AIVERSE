# Native-module diagnostics · implementation

Commit 59a5c97eec5560fd57117604d85e2584ac9a125f registers reportMissingNativeModules on the six simulation-based MCP checks. NativeBuildOps wraps their existing operations. Static balance_stats keeps its rules-only path. The native API, script/scenario/balance/workspace registration paths and CMake regression are listed in docs/features/native-simulation-checks.json.

The regression test failed as expected against the baseline main-mac-4d11ba62 MCP binary at tools/list (reportMissingNativeModules absent). Its default match failure assertion had already passed. The baseline registry audit passed all 18 families. Initial feature planning used 942b03ba9; the planning commit is 88d321aad. The new Mac MCP executable is building on KINGSTON. Next: record branch execution through the standalone transport and run mandatory merged-tree acceptance.
