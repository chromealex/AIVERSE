# Stage 82 — native capture and project lifetime

Parent `09f0c81a2b0d30618116806cdca4e3f1d9d59d8c`. Branch `feature/spectrafx-html5-data-interfaces-20261123`.

Native capture cancellation marks shared callback state cancelled before dropping host ownership on shutdown, project open, actual native surface detach, render failure, scheduling exception, Stop and successful graph load. Actual detached FX scene is destroyed on the render owner; ordinary viewport clipping/hiding remains non-destructive. StudioScene status now routes through the transfer lease expiry path and checks project identity before returning any status/camera/stage response. Browser status polling is serialized and discarded after project change or effect cleanup; capture polling tracks project identity and resets capture ID. Added native test for project switch via status.

Source-only; no native or web tests executed. Direct command promises and ordinary HTML-only viewport unmount still need review, as do GPU callback cancellation during encoding and full Mac/Windows acceptance. Main untouched.
