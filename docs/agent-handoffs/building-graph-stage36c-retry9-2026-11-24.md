# Stage 36C — retry 9: PHYSIS map storage guards

User incremental build excerpt at 89% reports two Clang -Wtautological-negation-compare warnings in apps/editor/PhysisAuthoring.h:127 and :591. Full build outcome, exact tested SHA and CTest results are not supplied.

Implementation 8055cf72c3d2fee21dfcac89714fce44c74b68e9: MapDoc::Data stores the map authoring payload in extra for both world-space and other maps (MapDoc.cpp load/toJson contract). Replace both impossible worldSpace && !worldSpace checks with !doc.data.extra.isObject() and a genre-neutral error; simplify identical PHYSIS read branches. Json::set would otherwise replace malformed non-object storage. Keep validation before undo mutation. Add transactional rejection fixtures for both worldSpace settings to tests/physis_authoring_test.cpp and tests/physis_field_authoring_test.cpp, including transform with a valid frozen baseline and invalid current storage. Update integration manifest. No runtime PHYSIS, persistence or genre-specific implementation change.

GitHub source inspection and commit only; new C++ fixtures, Mac build/CTest, editor and MCP runtime verification NOT RUN. Next: fast-forward integration branch and rerun the existing KINGSTON-backed quick build; collect complete diagnostics and CTest report. No main merge.
