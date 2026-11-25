# Selected unit portrait (#127)

The existing UI model view landed in aec89ef43; reuse the project-authored GamePortrait and shared UI texture binding. Acceptance: switch unit types/owners, fog, deselection, custom and stock HUDs, Metal/GL orientation, animated presentation, teardown. Tests and visual checks deferred.
a60732990d169231bd4644a909d0954acd8f3e8b
GamePortrait supports bust framing and clearing. UiLayer retains the established dynamic texture owner, marks offscreen orientation and reports image consumers. Existing UiModelView shares the orientation fix. Client HUD selection wiring follows.
b41cc2e87c7358087bddf142b0b9e7bddf1d0cc2
ClientUi reuses the selection visibility gate, binds #portrait before document load, renders the existing GamePortrait before the scene pass and releases it at shutdown. Stock card is opt-in; custom RmlUi may use the texture directly. Authoring example in docs/ui-model-view.md. Metal/GL, animation and selection checks pending.
