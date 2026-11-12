aiverse_native_module(ID library_demo VERSION 4 ENTRY createLibraryDemo SOURCES LibraryDemo.cpp AppConsumer.cpp ToolkitConsumer.cpp SystemsConsumer.cpp BotConsumer.cpp GenreConsumer.cpp)
aiverse_native_use_libraries(aiverse_native_library_demo
    graphs behavior logistics configuration tutorial
    ui-binding screens themes app-lifecycle play-scenarios genre-kits change-planner
    bounded-memory hot-reload work-budget state-diff attribute-modifiers status-effects
    tag-conditions object-interactions progression-curves reservations action-sequences
    loot-tables ai-perception decision-explanations bots-rts bots-fps navmesh
    character-locomotion melee-combat weapon-policies match-rules narrative-graphs)
# Combat actions (hitboxes, dodge, parry, poise, lock-on) compose melee-combat.
aiverse_native_use_libraries(aiverse_native_library_demo combat-actions)
# Stealth senses (sight, hearing, trails, suspicion, alerts, search, disguises, hiding).
aiverse_native_use_libraries(aiverse_native_library_demo stealth-senses)
