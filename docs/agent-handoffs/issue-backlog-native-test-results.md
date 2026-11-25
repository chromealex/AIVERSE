# Native backlog checks

Source HEAD 2e64eb1b5, macOS x86_64. Ran ../issue-backlog-completion-mac/aiverse_tests from the issue worktree with each filter below. All seven commands exited 0. The combined build was already running while this branch was being developed, so this records the named registered tests only.

| Filter | Checks passed |
| --- | ---: |
| progression_critical_hit | 15 |
| projectile_program_fitted_weapon_arc | 189 |
| fx_node_binding | 10 |
| overlay_conditions_on_sim_state | 26 |
| resource_hud_labels | 5 |
| fx_budget_config | 12 |
| city_mirror_predicts | 12 |

For #153 the progression test checks final critical damage and hit flags, the deceased entity identity, a capped actual payout of 3 gold and 5 wood, and absence of a second payout. The general overlay and city tests exercise their existing paths. Popup text formatting and critical-specific visual style still need an end-to-end check. Related scope: #131 HUD formats, #146 node anchors, #147 fitted arcs, #152 budgets. The final integration quick gate remains outstanding.

Mac quick on the feature snapshot reported two monolithic regressions, reproduced by name with the same binary: projectile_program_loads_real_v4_projectile_save failed to restore the real v4 fixture and arsenal_fists_hit_scan_in_reach_only missed its second punch in a ten-tick input window. skill_library_isolation also timed out at 601 seconds after isolating twelve policy selections. lens_metal_frame was skipped. The quick gate failed.

4eb3f17bbbd1fe80840334cfbf507a075cb63194 versions the weapon cooldown hash in the game module pool header. Save.cpp, GameCore.cpp, Arsenal.cpp and Simulation.cpp validate the original hash for an old archive, with separate support for native ECS migration. The Arsenal fixture allows the lockstep input delay and full weapon period in tests/tests_arsenal.inc. These corrections passed git diff --check; compilation and rerunning both tests on this revision are pending.
