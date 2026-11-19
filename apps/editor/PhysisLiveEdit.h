// PHYSIS Stage 24: opt-in local editor->Simulation field edit gateway/status.
// This header has NO ImGui and does not mutate World from presentation.
// A project module must explicitly advertise local editor authority.
#pragma once
#include "engine/physis/PhysisFieldRuntime.h"
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::editor::physis_live {

// Implement alongside a project's GameModule to expose exactly one permitted
// editor command route. This is NOT network authentication. Network ingress
// must additionally authenticate permission from its trusted session identity.
struct FieldEditHost {
    virtual ~FieldEditHost() = default;
    virtual bool allowLocalFieldEdits(const Simulation& simulation) const = 0;
    virtual std::uint16_t fieldEditCommandType() const = 0;
};

enum class EditStatus : std::uint8_t {
    Unavailable, Idle, Queued, Applied, Conflict, Rejected
};

inline const char* statusName(EditStatus value) {
    switch(value) {
        case EditStatus::Unavailable: return "Недоступно";
        case EditStatus::Idle: return "Готово к отправке";
        case EditStatus::Queued: return "В очереди";
        case EditStatus::Applied: return "Применено (состояние подтверждено)";
        case EditStatus::Conflict: return "Конфликт ревизии";
        case EditStatus::Rejected: return "Отклонено";
    }
    return "Неизвестно";
}

inline bool canSubmit(const Simulation* sim, std::string* reason = nullptr) {
    const auto fail=[&](const char* why) {
        if(reason) *reason=why;
        return false;
    };
    if(!sim) return fail("Запустите локальную Simulation с PHYSIS.");
    if(!sim->world().resourceConst<physis::State>())
        return fail("PHYSIS не установлена в текущем игровом World.");
    if(!sim->world().resourceConst<physis::FieldRuntimeState>())
        return fail("GameModule должен использовать installEditableFromMap.");
    if(sim->hasCommandSink())
        return fail("Сетевая Simulation: необходим авторизованный серверный канал.");
    const auto* host=dynamic_cast<const FieldEditHost*>(&sim->module());
    if(!host || !host->allowLocalFieldEdits(*sim) ||
        host->fieldEditCommandType()==0)
        return fail("GameModule не разрешает PHYSIS Live Editing.");
    if(reason) reason->clear();
    return true;
}

struct FieldEditSession {
    EditStatus status=EditStatus::Unavailable;
    const Simulation* source=nullptr;
    std::uint64_t expectedRevision=0;
    std::uint32_t scheduledTick=0;
    std::uint32_t submittedTick=0;
    std::map<std::string, std::pair<physis::AuthoredFieldKind,std::string>> desired;
    std::string message;

    bool matchesDesired(const physis::FieldRuntimeState& runtime) const {
        if(runtime.byId.size()!=desired.size()) return false;
        for(const auto& [id, planned]:desired) {
            const auto it=runtime.byId.find(id);
            if(it==runtime.byId.end() || it->second.kind!=planned.first ||
               it->second.fingerprint!=planned.second) return false;
        }
        return true;
    }

    void observe(const Simulation* sim) {
        if(!sim || (source && source!=sim)) {
            source=sim;
            desired.clear();
            status=EditStatus::Unavailable;
            message="Активная Simulation отсутствует или была заменена.";
            return;
        }
        source=sim;
        // An already confirmed local edit may disappear after a rollback.
        // Never keep presenting an obsolete "Applied" after a restore.
        if(status==EditStatus::Applied) {
            const auto* runtime=sim->world().resourceConst<physis::FieldRuntimeState>();
            if(!runtime || sim->tick()<submittedTick ||
               runtime->revision<=expectedRevision ||
               !matchesDesired(*runtime)) {
                status=EditStatus::Conflict;
                message="PHYSIS применённые поля изменились или были откатаны.";
                return;
            }
        }
        std::string reason;
        if(status!=EditStatus::Queued) {
            if(!canSubmit(sim,&reason)) {
                status=EditStatus::Unavailable;
                message=reason;
            } else if(status==EditStatus::Unavailable) {
                status=EditStatus::Idle;
                message.clear();
            }
            return;
        }
        const auto* runtime=sim->world().resourceConst<physis::FieldRuntimeState>();
        if(!runtime) {
            status=EditStatus::Rejected;
            message="PHYSIS runtime registry исчез из активной Simulation.";
            return;
        }
        if(sim->tick()<submittedTick || runtime->revision<expectedRevision) {
            status=EditStatus::Rejected;
            message="Simulation была откатана; требуется повторная отправка.";
            return;
        }
        if(runtime->revision>expectedRevision) {
            // A monotonic revision by itself does NOT prove that OUR command
            // succeeded: another editor command may have won the same revision.
            const bool equal=runtime->revision==expectedRevision+1 &&
                             matchesDesired(*runtime);
            status=equal?EditStatus::Applied:EditStatus::Conflict;
            message=equal ?
                "Ревизия и содержимое полей совпали с отправленным запросом." :
                "Ревизия сменилась, но состояние не соответствует запросу.";
        } else if(sim->tick()>scheduledTick) {
            status=EditStatus::Rejected;
            message="Тик прошёл без обновления ревизии; проверьте обработчик GameModule.";
        }
    }

    bool submit(Simulation& sim,const Json& authoredMap,
                std::string* error=nullptr) {
        if(status==EditStatus::Queued) {
            if(error) *error="PHYSIS: предыдущая команда ещё выполняется";
            return false;
        }
        std::string why;
        if(!canSubmit(&sim,&why)) {
            status=EditStatus::Unavailable;
            message=why;
            if(error) *error=why;
            return false;
        }
        try {
            const auto options=physis::optionsFromMap(authoredMap);
            if(!options) {
                why="PHYSIS отсутствует или отключена в редактируемой карте.";
                throw std::invalid_argument(why);
            }
            const auto fields=physis::parseFieldAssets(
                authoredMap["physis"],*options);
            const auto* runtime=sim.world().resourceConst<physis::FieldRuntimeState>();
            const auto* state=sim.world().resourceConst<physis::State>();
            if(!runtime || !state) throw std::logic_error("PHYSIS runtime не инициализирован.");
            const auto& old=state->settings;
            const auto& updated=options->climate;
            if(old.originX!=updated.originX || old.originZ!=updated.originZ ||
               old.width!=updated.width || old.height!=updated.height ||
               old.cellSizeMeters!=updated.cellSizeMeters || old.seed!=updated.seed ||
               old.baseTemperature!=updated.baseTemperature ||
               old.baseHumidity!=updated.baseHumidity)
                throw std::invalid_argument("Live Editing поддерживает только поля, не изменение сетки/климата.");
            // Take every intended fingerprint from the EXACT command payload.
            physis::FieldEditBatch batch{runtime->revision,fields};
            Command command;
            const auto* host=dynamic_cast<const FieldEditHost*>(&sim.module());
            if(!host || !physis::encodeFieldEditCommand(batch,host->fieldEditCommandType(),
                                                         command,&why))
                throw std::invalid_argument(why.empty()?"Некорректная команда PHYSIS":why);
            std::map<std::string,
                     std::pair<physis::AuthoredFieldKind,std::string>> expected;
            for(const auto& field:fields)
                expected.emplace(field.id,std::make_pair(
                    field.kind,physis::authoredFingerprint(field)));
            const auto beforeTick=sim.tick();
            const auto tick=sim.submit(std::move(command));
            source=&sim;
            submittedTick=beforeTick;
            scheduledTick=tick;
            expectedRevision=batch.expectedRevision;
            desired=std::move(expected);
            status=EditStatus::Queued;
            message="Команда поставлена в очередь Simulation; применение ещё не подтверждено.";
            if(error) error->clear();
            return true;
        } catch(const std::exception& ex) {
            message=ex.what();
            status=EditStatus::Rejected;
            if(error) *error=message;
            return false;
        }
    }
};

} // namespace aiverse::editor::physis_live
