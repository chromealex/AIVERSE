# Painted minimap background (#173)

Delivery scope: game default and map override, content image decoding at match start, CPU compositing below fog, units, overlays and the camera. Verify rectangular maps, alpha, missing images and both client startup paths in combined acceptance.

Implementation abdbbdf3c13b6f9d31f2fb5086ec72c7729f079e in apps/client/{Minimap.cpp,GameClient.h,GameClient.cpp,Lobby.cpp}. Image decoding uses the project content reader at match setup. Existing fog and overlay stages consume the composed base. Builds, image captures and match checks deferred to combined acceptance.
