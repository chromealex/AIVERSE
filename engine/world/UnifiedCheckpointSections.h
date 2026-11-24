#pragma once
#include "UnifiedCheckpointFormat.h"
#include "UnifiedWorldReplay.h"
#include "RegionCheckpointArchive.h"
namespace aiverse::world {
class UnifiedCheckpointSections {
public:
    using Format=UnifiedCheckpointFormat;
    using State=UnifiedWorldReplay::State;
    using Writer=std::function<bool(const State&,std::vector<uint8_t>&)>;
    using Reader=std::function<bool(State&,uint32_t,std::span<const uint8_t>)>;
    struct Adapter {uint32_t version=1;Writer write;Reader read;};
    using Adapters=std::array<Adapter,3>;
    static bool capture(const State& state,const std::array<uint8_t,16>& world,
        const Adapters& adapters,size_t budget,std::vector<uint8_t>& out){
        Format::Snapshot snapshot;
        snapshot.world=world;snapshot.sequence=state.sequence;snapshot.revision=state.ledger.lastRevision();
        Format::Section ledger{Format::Kind::Ledger,1,{}};
        if(!RegionCheckpointArchive::encode(state.ledger,ledger.bytes)||ledger.bytes.size()>budget)return false;
        size_t used=ledger.bytes.size();
        snapshot.sections.push_back(std::move(ledger));
        for(size_t i=0;i<adapters.size();++i){
            const auto& a=adapters[i];
            Format::Section section{static_cast<Format::Kind>(i+2),a.version,{}};
            if(!a.version||!a.write||!a.write(state,section.bytes)||section.bytes.size()>budget-used)return false;
            used+=section.bytes.size();snapshot.sections.push_back(std::move(section));
        }
        if(!state.restart.empty()){
            NativeRestartState::Data metadata;
            if(state.restart.size()>budget-used||!NativeRestartState::decode(state.restart,metadata)||
               !NativeRestartState::referencesValid(metadata,state.world,state.ledger))return false;
            snapshot.sections.push_back({Format::Kind::Restart,1,
                std::vector<uint8_t>(state.restart.begin(),state.restart.end())});
        }
        return Format::encode(snapshot,budget,out);
    }
    // The factory prepares a private World with required pools/resources.
    // Readers run in entity, component, resource order and may not publish side effects.
    using Factory=std::function<std::unique_ptr<State>()>;
    static bool restore(std::span<const uint8_t> bytes,size_t budget,
        const std::array<uint8_t,16>& expectedWorld,const Adapters& adapters,
        const Factory& factory,std::unique_ptr<State>& out,
        const UnifiedWorldReplay::Validator& validate={}){
        Format::Snapshot snapshot;
        if(!factory||!Format::decode(bytes,budget,snapshot)||snapshot.world!=expectedWorld)return false;
        auto staged=factory();if(!staged)return false;
        const auto& ledger=snapshot.sections[0];
        if(ledger.version!=1||!RegionCheckpointArchive::decode(ledger.bytes,staged->ledger)||
           staged->ledger.lastRevision()!=snapshot.revision)return false;
        staged->sequence=snapshot.sequence;
        for(size_t i=0;i<adapters.size();++i){
            const auto& a=adapters[i];const auto& section=snapshot.sections[i+1];
            if(!a.read||!a.read(*staged,section.version,section.bytes))return false;
        }
        if(staged->sequence!=snapshot.sequence||staged->ledger.lastRevision()!=snapshot.revision)return false;
        staged->restart.clear();
        if(snapshot.sections.size()==5){
            const auto& section=snapshot.sections[4];
            if(section.version!=1||section.bytes.empty()||section.bytes.size()>NativeRestartState::MaxBytes)return false;
            staged->restart.assign(section.bytes.begin(),section.bytes.end());
            NativeRestartState::Data metadata;
            if(!NativeRestartState::decode(staged->restart,metadata)||
               !NativeRestartState::referencesValid(metadata,staged->world,staged->ledger))return false;
        }
        PersistentEntityReferences refs;
        if(!refs.rebuild(staged->world,staged->ledger)||(validate&&!validate(*staged)))return false;
        out.swap(staged);return true;
    }
};
}
