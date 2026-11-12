#pragma once
// physical-interactions umbrella: deterministic kinematic interaction policies. Determinism/network model:
// these policies are the authoritative state (Fixed math, integer ticks, ascending-ID order, owner collision
// queries through engine/world); run them on the authority (or in lockstep on every peer) from validated
// Commands, save/hash their state records (kinds 80..85, 86..87 reserved) and replay the same inputs for
// rollback. A Box3D world (engine/physics) is not deterministic across builds/CPUs: use it only for
// presentation (debris from DebrisRequest, cosmetic ragdolls) and never read it back into these states.
#include "Carry.h"
#include "Push.h"
#include "Ropes.h"
#include "Doors.h"
#include "Platforms.h"
#include "Breakables.h"
namespace aiverse::lib::genre {
// Immutable definitions loaded by readInteractionDefinitions (InteractionData.h).
struct InteractionDefinitions {Carryables carryables;Pushables pushables;RopeDefs ropes;DoorDefs doors;PlatformDefs platforms;BreakableDefs breakables;};
inline bool valid(const InteractionDefinitions& d){return valid(d.carryables)&&valid(d.pushables)&&valid(d.ropes)&&valid(d.doors)&&valid(d.platforms)&&valid(d.breakables);}
}
