# Shared client controls

F6 selects the scout; F6 again focuses it. M enters Move targeting. F7 uses the public view.select API to select the armed tower. Escape opens pause; Quit requests host termination. The native acceptance runner exercises the same client and stock HUD on Metal.

Portraits deliberately differ from world models: the scout uses an orange animated diamond on its owner colour, and the tower a green static diamond on an authored dark red background. Switching selection reuses the shared portrait host.

The minimap samples the authored gradient across world bounds [4,2,20,14], draws a rectangular camera frame and hides tower markers through view.minimap=false. The image is a deterministic pixel fixture eligible for the ordinary texture atlas.
