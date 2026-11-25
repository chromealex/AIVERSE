# Persistent view anchors (#130)

Verify two identical effects on separate sockets, conditional visibility, movement, generation reuse and match teardown in combined acceptance.
50c32da5b0e8cf201b48491b5396a2060281ed4e
GameViewDraw publishes stable declaration slots. GameFx owns emitter tracking and teardown. ClientFx reconciles visible anchors once per primary frame. Combined runtime tests pending.
