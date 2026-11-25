# Stage 124 — planetary MORPH model stream binding

Base 52f7c729. In progress. Reuse existing per-instance ModelVertex stream and renderer override. Add strict native frame contract for rigid unit-scale WorldTransform, exact completed-tick nodes, common GravityBinding owner/units, canonical body and translation-only render frame. Reject nonidentity mesh-local bind transforms/skin and competing pose on this explicit path rather than silently mixing spaces. Fail closed on stale stream and invalidate on rollback. Source fixture for core frame/units/owner/tick. Existing legacy stream unchanged except safe publication invalidation. No automatic model asset binding, rigid collider deformation or main merge; tests deferred.

Status: source ef4a1a46272154b68c525906fa298f7c2cf44abf; previous implementation 425178535ddeca3d044ec6f8aaf99f8f2b03b00c. Fixture NOT RUN; host/asset and cross-backend acceptance open.
