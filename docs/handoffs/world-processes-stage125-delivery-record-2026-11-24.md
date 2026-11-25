# Stage 125 — oriented character motion

Base b783a5ef. In progress. Generalize the existing Box3D character mover by transforming query capsules, planes, rays, platform velocities and pushes into one owner-selected orthonormal frame; retain identity legacy path. Add explicit oriented proxy transform and PHYSIS sampling adapter with canonical body-local translation, gravity-opposed up, heading fallback and weak-gravity no-ground mode. No second collision solver, no GameCore kinematic movement changes or new ECS state. Source fixtures; tests deferred, no main merge.

Status: source f1f178fcafc847fe5e9b5d2f1c6a189cc7e01427; fixtures NOT RUN; actual host activation and platform verification open.
