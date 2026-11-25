# Watchtower orders

Select **Select scout** beside the minimap. Right-click open ground to send the scout there. The mint ring expands at the destination and the view script displays the order kind. Shift-right-click queues another order.

The patrol's guard house begins upgrading two seconds into the match. Watch the tower grow over sixteen seconds. The opposing guard house keeps its original appearance. Use **Restart upgrade** to replay the transition and **Hide order labels** to view the ring on its own.

The demo uses the shared game client, the `order` view event, and the source type's `view.showUpgradeTarget` flag. Edit the ring in Studio's Overlays panel. To change the building preview, open the guard house in Rules and toggle **Show upgrade target during construction**.

To build the browser version with the engine builder and an active Emscripten SDK, run from the repository root:

```sh
aiversebuild --project demos/order-upgrade --engine . --out "$DEMO_OUTPUT" --target web --audio off
```

Serve the resulting `web/` directory over HTTP and open `index.html?map=0&lang=en`.
