// PHYSIS Stage 23: opt-in authored-field ECS reconciliation at authoritative
// Simulation command/tick boundaries. No editor/genre/Box3D/NEREID/Terra types.
// Existing installFromMap and save manifests are unchanged.
#pragma once
#include "PhysisFieldChangePlan.h"
#include "PhysisProjectConfig.h"
#include "PhysisFieldRuntimeState.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::physis {

// Reusable typed fields are encoded field-wise with the same persistence IO
// as normal PHYSIS ECS components; never reinterpret C++ struct memory.
inline void io(Archive& a, AuthoredField& field) {
    a(field.id, field.kind);
    if (!a.ok()) return;
    switch (field.kind) {
        case AuthoredFieldKind::Wind: {
            WindZone value = a.writing() ? std::get<WindZone>(field.data) : WindZone{};
            io(a, value);
            if (!a.writing() && a.ok()) field.data = value;
            break;
        }
        case AuthoredFieldKind::Storm: {
            StormEvent value = a.writing() ? std::get<StormEvent>(field.data) : StormEvent{};
            io(a, value);
            if (!a.writing() && a.ok()) field.data = value;
            break;
        }
        case AuthoredFieldKind::Gravity: {
            GravityField value = a.writing() ? std::get<GravityField>(field.data) : GravityField{};
            io(a, value);
            if (!a.writing() && a.ok()) field.data = value;
            break;
        }
        default: a.fail(); break;
    }
}

inline std::string authoredFingerprint(const AuthoredField& source) {
    std::string bytes;
    Archive archive(&bytes);
    AuthoredField copy = source;
    io(archive, copy);
    if (!archive.ok()) throw std::invalid_argument("PHYSIS cannot serialize authored field");
    return bytes;
}

inline bool validRuntimeField(const AuthoredField& a) {
    // Match parseFieldAssets bounds using authoritative Fixed numbers. This
    // also protects untrusted network commands decoded without JSON parsing.
    try {
        Json j = Json::object();
        j.set("id",Json::string(a.id));
        // Reusing the strict ASCII ID validator even for network payloads.
        (void)field_asset_detail::id(j);
    } catch (...) { return false; }
    const auto in=[](Fixed x, std::int64_t lo, std::int64_t hi) {
        return x>=Fixed::fromInt(lo) && x<=Fixed::fromInt(hi);
    };
    const auto vec=[&](Vec3 v, std::int64_t lo, std::int64_t hi) {
        return in(v.x,lo,hi) && in(v.y,lo,hi) && in(v.z,lo,hi);
    };
    const auto ticks=[](std::uint64_t value) { return value<=2147483647ull; };
    switch (a.kind) {
        case AuthoredFieldKind::Wind: {
            if (!std::holds_alternative<WindZone>(a.data)) return false;
            const auto& v=std::get<WindZone>(a.data);
            return vec(v.center,-1000000,1000000) &&
                   in(v.radiusMeters,1,100000) && vec(v.velocity,-200,200) &&
                   in(v.gustAmplitude,0,100) && ticks(v.seed) &&
                   ticks(v.startTick) && ticks(v.durationTicks) &&
                   ticks(v.gustPeriodTicks);
        }
        case AuthoredFieldKind::Storm: {
            if (!std::holds_alternative<StormEvent>(a.data)) return false;
            const auto& v=std::get<StormEvent>(a.data);
            return (v.kind==StormKind::Tornado || v.kind==StormKind::Hurricane) &&
                   vec(v.center,-1000000,1000000) &&
                   vec(v.driftPerTick,-1000,1000) &&
                   in(v.eyeRadiusMeters,1,100000) &&
                   in(v.outerRadiusMeters,1,100000) &&
                   v.outerRadiusMeters>v.eyeRadiusMeters &&
                   in(v.heightMeters,1,100000) &&
                   in(v.tangentialWind,0,200) && in(v.radialInflow,0,100) &&
                   in(v.updraft,0,150) && in(v.precipitationBoost,0,1) &&
                   in(v.pressureDrop,0,150) && ticks(v.startTick) &&
                   ticks(v.durationTicks);
        }
        case AuthoredFieldKind::Gravity: {
            if (!std::holds_alternative<GravityField>(a.data)) return false;
            const auto& v=std::get<GravityField>(a.data);
            if (v.mode!=GravityMode::Uniform &&
                v.mode!=GravityMode::Attract && v.mode!=GravityMode::Repel) return false;
            return vec(v.center,-1000000,1000000) &&
                   in(v.radiusMeters,v.mode==GravityMode::Uniform?0:1,100000) &&
                   vec(v.acceleration,-200,200) &&
                   in(v.radialMagnitude,0,200) &&
                   v.priority>=0 && v.priority<=1000000;
        }
        default: return false;
    }
}

inline bool validateRuntimeFields(const std::vector<AuthoredField>& fields,
                                  std::string* error=nullptr) {
    const auto fail=[&](const char* why) {
        if(error) *error=why;
        return false;
    };
    if (fields.size()>2048) return fail("PHYSIS field edit exceeds 2048 sources");
    std::string previous;
    bool first=true;
    for (const auto& field:fields) {
        if (!validRuntimeField(field)) return fail("PHYSIS field edit has invalid data");
        if (!first && !(previous<field.id))
            return fail("PHYSIS field edit IDs must be unique and sorted");
        previous=field.id;first=false;
    }
    return true;
}

// All commands replace the entire desired authored field set. The revision
// check rejects stale edit batches and prevents dropping concurrent changes.
// A GameModule must authorize the system/player=0 command and dispatch it
// from applyCommand; Simulation records/replays that command at its tick.
struct FieldEditBatch {
    std::uint64_t expectedRevision = 0;
    std::vector<AuthoredField> desired;
};
inline constexpr std::uint32_t kFieldEditProtocol = 1;
// Simulation::readCommand accepts at most 4096 int64 args. One word is
// reserved for payload byte length. Never emit an unroundtrippable command.
inline constexpr std::size_t kFieldEditMaxWords = 4096;
inline constexpr std::size_t kFieldEditMaxBytes = (kFieldEditMaxWords-1)*8;

inline bool encodeFieldEditCommand(const FieldEditBatch& request,
                                   std::uint16_t hostCommandType,
                                   Command& output, std::string* error=nullptr) {
    if (!validateRuntimeFields(request.desired,error)) return false;
    std::string binary;
    Archive archive(&binary);
    std::uint32_t version=kFieldEditProtocol;
    std::uint32_t count=std::uint32_t(request.desired.size());
    std::uint64_t revision=request.expectedRevision;
    archive(version,revision,count);
    for (const auto& item:request.desired) {
        auto copy=item;
        io(archive,copy);
    }
    if (!archive.ok() || binary.size()>kFieldEditMaxBytes) {
        if(error) *error="PHYSIS edit command exceeds binary payload limit";
        return false;
    }
    Command command;
    command.type=hostCommandType;
    command.player=0; // authoritative system slot, not a client permission
    command.args.reserve(1+(binary.size()+7)/8);
    command.args.push_back(std::int64_t(binary.size()));
    for (std::size_t offset=0;offset<binary.size();offset+=8) {
        std::uint64_t word=0;
        for (unsigned j=0;j<8 && offset+j<binary.size();++j)
            word |= std::uint64_t(std::uint8_t(binary[offset+j])) << (j*8);
        command.args.push_back(std::bit_cast<std::int64_t>(word));
    }
    output=std::move(command);
    return true;
}
inline bool decodeFieldEditCommand(const Command& command,
                                   FieldEditBatch& output,
                                   std::string* error=nullptr) {
    const auto fail=[&](const char* why) {
        if(error) *error=why;return false;
    };
    if (command.args.empty() || command.args[0]<=0 ||
        std::uint64_t(command.args[0])>kFieldEditMaxBytes)
        return fail("PHYSIS invalid edit command size");
    const std::size_t size=std::size_t(command.args[0]);
    if (command.args.size()!=1+(size+7)/8)
        return fail("PHYSIS invalid edit command word count");
    std::string bytes(size,'\0');
    for (std::size_t i=0;i<size;++i) {
        const auto word=std::bit_cast<std::uint64_t>(command.args[1+i/8]);
        bytes[i]=char((word>>((i%8)*8)) & 0xffu);
    }
    Archive archive(bytes);
    std::uint32_t version=0,count=0;
    FieldEditBatch proposed;
    archive(version,proposed.expectedRevision,count);
    if (!archive.ok() || version!=kFieldEditProtocol || count>2048)
        return fail("PHYSIS invalid edit protocol or count");
    proposed.desired.reserve(count);
    for (std::uint32_t i=0;i<count && archive.ok();++i) {
        AuthoredField item;
        io(archive,item);
        if(archive.ok()) proposed.desired.push_back(std::move(item));
    }
    if (!archive.ok() || archive.remaining()!=0 ||
        !validateRuntimeFields(proposed.desired,error))
        return fail("PHYSIS edit command payload validation failed");
    output=std::move(proposed);
    return true;
}

inline bool fieldEntityHasKind(const World& world, Entity entity,
                               AuthoredFieldKind kind) {
    if(!world.alive(entity)) return false;
    switch(kind) {
        case AuthoredFieldKind::Wind: return world.has<WindZone>(entity);
        case AuthoredFieldKind::Storm: return world.has<StormEvent>(entity);
        case AuthoredFieldKind::Gravity: return world.has<GravityField>(entity);
        default: return false;
    }
}
inline void writeFieldComponent(World& world,Entity entity,
                                const AuthoredField& asset) {
    switch(asset.kind) {
        case AuthoredFieldKind::Wind:
            world.add<WindZone>(entity,std::get<WindZone>(asset.data));break;
        case AuthoredFieldKind::Storm:
            world.add<StormEvent>(entity,std::get<StormEvent>(asset.data));break;
        case AuthoredFieldKind::Gravity:
            world.add<GravityField>(entity,std::get<GravityField>(asset.data));break;
        default: throw std::invalid_argument("PHYSIS unknown field kind");
    }
}

// This is the tick-boundary operation. Module::applyCommand calls it BEFORE
// Simulation runs PHYSIS systems; all changes are staged in an independent
// World copy and committed together, so validation/allocation failures cannot
// partially modify authoritative state.
inline bool reconcileFieldEdit(Simulation& sim,const FieldEditBatch& request,
                               std::string* error=nullptr) {
    const auto fail=[&](const char* why) {
        if(error)*error=why;return false;
    };
    const World& committed=sim.world();
    const auto* current=committed.resourceConst<FieldRuntimeState>();
    if (!committed.resourceConst<State>() || !current)
        return fail("PHYSIS live field editing not installed");
    if (current->revision!=request.expectedRevision)
        return fail("PHYSIS stale field edit revision");
    if (current->revision==std::numeric_limits<std::uint64_t>::max())
        return fail("PHYSIS field edit revision overflow");
    if (!validateRuntimeFields(request.desired,error)) return false;
    if(current->byId.size()>2048)
        return fail("PHYSIS live registry exceeds bound");

    std::vector<FieldRevision> before,after;
    before.reserve(current->byId.size());
    after.reserve(request.desired.size());
    for (const auto& [id,entry]:current->byId) {
        if (!fieldEntityHasKind(committed,entry.entity,entry.kind))
            return fail("PHYSIS field registry has stale Entity binding");
        before.push_back({id,entry.kind,entry.fingerprint});
    }
    for (const auto& asset:request.desired)
        after.push_back({asset.id,asset.kind,authoredFingerprint(asset)});
    const auto plan=planFieldChanges(std::move(before),std::move(after));

    // An identical full-state command still consumes its expected revision,
    // preventing a second stale command with the same revision from applying,
    // but should not deep-copy all climate cells merely to advance it.
    if(plan.empty()) {
        auto* registry=sim.world().resource<FieldRuntimeState>();
        if (!registry || registry->revision!=request.expectedRevision)
            return fail("PHYSIS runtime revision changed");
        ++registry->revision;
        return true;
    }

    World staged=committed;
    FieldRuntimeState registry=*current;
    for(const auto& change:plan) {
        if (change.operation==FieldChangeKind::Remove) {
            const auto it=registry.byId.find(change.id);
            if(it==registry.byId.end()) return fail("PHYSIS remove source missing");
            staged.destroy(it->second.entity);
            registry.byId.erase(it);
        } else {
            const auto desired=std::lower_bound(request.desired.begin(),
                           request.desired.end(),change.id,
                           [](const AuthoredField& a,const std::string& id){return a.id<id;});
            if(desired==request.desired.end() || desired->id!=change.id)
                return fail("PHYSIS edit plan references missing source");
            if (change.operation==FieldChangeKind::Update) {
                const auto it=registry.byId.find(change.id);
                if(it==registry.byId.end() ||
                   !fieldEntityHasKind(staged,it->second.entity,change.kind))
                    return fail("PHYSIS update Entity missing");
                writeFieldComponent(staged,it->second.entity,*desired);
                it->second.fingerprint=authoredFingerprint(*desired);
            } else {
                const Entity entity=staged.create();
                writeFieldComponent(staged,entity,*desired);
                registry.byId.emplace(change.id,FieldRuntimeEntry{
                    entity,desired->kind,authoredFingerprint(*desired)});
            }
        }
    }
    ++registry.revision; // a consumed no-op command still advances sequence
    staged.setResource<FieldRuntimeState>(std::move(registry));
    sim.world()=std::move(staged);
    return true;
}

// Only call from a trusted GameModule::applyCommand for its explicitly chosen
// PHYSIS command type. The host must authenticate/authorize player 0 and block
// spoofed system commands before accepting them from network clients.
inline bool applyFieldEditCommand(Simulation& sim,const Command& command,
                                  std::uint16_t hostCommandType,
                                  std::string* error=nullptr) {
    if(command.type!=hostCommandType || command.player!=0 ||
       command.tick!=sim.tick() || !command.units.empty() ||
       command.target!=kNullEntity || command.flags!=0)
    {
        if(error) *error="PHYSIS unauthorized or mistimed field edit command";
        return false;
    }
    FieldEditBatch request;
    if(!decodeFieldEditCommand(command,request,error)) return false;
    return reconcileFieldEdit(sim,request,error);
}

// Explicit opt-in startup. Registers all three source component schemas so
// future add/type-swap edits remain persistable. Existing installFromMap stays
// source-compatible and does NOT register this new resource. Save formats
// differ by design; use the same opt-in policy when loading.

// Stage 24 authenticated ingress adapter. It is intentionally NOT part of
// the normal player InputFrame relay. The transport supplies a principal
// derived from its *authenticated connection*, not from Command.player or
// any untrusted packet field. The server callback implements its ACL.
// Only then is a new system-slot Command constructed for deterministic replay.
template<class Principal, class Authorizer>
inline bool admitAuthorizedFieldEdit(Simulation& authoritative,
                                     const Command& untrusted,
                                     std::uint16_t hostType,
                                     const Principal& connectionPrincipal,
                                     Authorizer&& authorize,
                                     std::uint32_t& scheduledTick,
                                     std::string* error=nullptr) {
    const auto fail=[&](const char* reason) {
        if(error) *error=reason;
        return false;
    };
    if(hostType==0 || untrusted.type!=hostType ||
       untrusted.player==0 || untrusted.flags!=0 ||
       !untrusted.units.empty() || untrusted.target!=kNullEntity)
        return fail("PHYSIS untrusted field command has forbidden metadata");
    // The callback must inspect a server-issued principal, not a client-
    // asserted role. Caller must authenticate before using this function.
    if(!authorize(connectionPrincipal,untrusted))
        return fail("PHYSIS field editing denied by server permissions");
    FieldEditBatch request;
    if(!decodeFieldEditCommand(untrusted,request,error)) return false;
    const auto* state=authoritative.world().resourceConst<FieldRuntimeState>();
    if(!state || state->revision!=request.expectedRevision)
        return fail("PHYSIS remote edit has stale revision");
    Command trusted;
    if(!encodeFieldEditCommand(request,hostType,trusted,error)) return false;
    trusted.player=0;
    trusted.tick=0;
    trusted.seq=0;
    scheduledTick=authoritative.submit(std::move(trusted));
    if(scheduledTick==0)
        return fail("PHYSIS authoritative command channel rejected request");
    if(error) error->clear();
    return true; // accepted for submission, NOT yet applied
}

inline bool installEditableFromMap(Simulation& sim,const Json& map) {
    auto options=optionsFromMap(map);
    if(!options) return false;
    // Both registry paths own the same authored ECS entities. Never install
    // canonical editing over tracked fields or over a previous canonical host.
    // Reject BEFORE install() mutates the World or registers persistence.
    if(sim.world().resourceConst<State>() ||
        sim.world().resourceConst<AuthoredFieldInstallMode>() ||
       sim.world().resourceConst<RuntimeFieldRegistry>() ||
       sim.world().resourceConst<FieldRuntimeState>())
        throw std::logic_error("PHYSIS authored field registry already installed; choose exactly one runtime");
    const auto initial=parseFieldAssets(map["physis"],*options);
    // Validate the serializable authored identity BEFORE any ECS installation.
    // Serialization errors must not leave a partially spawned editable World.
    FieldRuntimeState registry;
    std::map<std::string,std::string> fingerprints;
    for(const auto& asset:initial)
        fingerprints.emplace(asset.id,authoredFingerprint(asset));
    options->enableWindZones=true;
    options->enableStorms=true;
    options->enableGravityFields=true;
    install(sim,*options);
    sim.registerPersistentResource<FieldRuntimeState>("physis.field_runtime",1);
    for(const auto& asset:initial) {
        const Entity entity=sim.world().create();
        writeFieldComponent(sim.world(),entity,asset);
        registry.byId.emplace(asset.id,FieldRuntimeEntry{
            entity,asset.kind,fingerprints.at(asset.id)});
    }
    sim.world().setResource<FieldRuntimeState>(std::move(registry));
    sim.world().setResource<AuthoredFieldInstallMode>(AuthoredFieldInstallMode{AuthoredFieldMode::Editable});
    return true;
}

// Host-only convenience: convert a fully authored map to the canonical
// command payload and submit via Simulation's existing command/CommandSink.
// This DOES NOT apply immediately, and it does not grant network authority.
// The GameModule must dispatch the chosen command type from applyCommand.
inline bool submitEditableFieldsFromMap(Simulation& sim,const Json& map,
                                        std::uint16_t hostCommandType,
                                        std::uint32_t& scheduledTick,
                                        std::string* error=nullptr) {
    const auto* registry=sim.world().resourceConst<FieldRuntimeState>();
    if(!registry) {
        if(error) *error="PHYSIS editable field registry not installed";
        return false;
    }
    try {
        const auto options=optionsFromMap(map);
        if(!options) {
            if(error) *error="PHYSIS map is disabled or absent";
            return false;
        }
        const auto* state=sim.world().resourceConst<State>();
        if(!state) {
            if(error) *error="PHYSIS simulation climate state missing";
            return false;
        }
        const auto& old=state->settings;
        const auto& next=options->climate;
        if(old.originX!=next.originX || old.originZ!=next.originZ ||
           old.width!=next.width || old.height!=next.height ||
           old.cellSizeMeters!=next.cellSizeMeters || old.seed!=next.seed ||
           old.baseTemperature!=next.baseTemperature ||
           old.baseHumidity!=next.baseHumidity) {
            if(error) *error="PHYSIS field-only edit cannot replace climate/grid settings";
            return false;
        }
        const auto desired=parseFieldAssets(map["physis"],*options);
        FieldEditBatch request{registry->revision,desired};
        Command command;
        if(!encodeFieldEditCommand(request,hostCommandType,command,error))
            return false;
        scheduledTick=sim.submit(std::move(command));
        return true;
    } catch(const std::exception& exception) {
        if(error) *error=exception.what();
        return false;
    }
}

} // namespace aiverse::physis
