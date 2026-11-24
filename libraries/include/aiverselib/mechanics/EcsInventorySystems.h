// Transactional, deterministic ECS inventory for any game type.
// Reuses the existing native inventory algorithms (stacks, weight, transfer)
// instead of re-implementing inventory for RTS, FPS or RPG.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/mechanics/EcsMechanicSystems.h"
#include "engine/gameplay/Inventory.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::mechanics {

struct InventoryComponent {
    gameplay::InventoryConfig config{};
    std::vector<gameplay::ItemStack> items;
};
struct InventoryCatalog {
    std::map<int64_t,gameplay::ItemType> byType; // immutable after setup
};
enum class InventoryOperation:uint8_t { Add, Remove, Transfer };
struct InventoryAction {
    uint64_t id=0;
    InventoryOperation operation=InventoryOperation::Add;
    Entity source=kNullEntity;
    Entity destination=kNullEntity; // only Transfer uses destination
    gameplay::ItemStack item{};
};
struct InventoryCommandQueue {
    uint64_t nextId=1; // canonical sequencer, included in authoritative state
    std::vector<InventoryAction> pending;
};
struct InventoryActionResult {
    uint64_t id=0;
    InventoryOperation operation=InventoryOperation::Add;
    Entity source=kNullEntity,destination=kNullEntity;
    gameplay::Decision decision{};
};
struct InventoryResultQueue {
    std::vector<InventoryActionResult> events; // results from the last barrier
};

// Submit from a validated Command, AI or interaction stage. The queue and its
// monotonically increasing ID live inside World and survive rollback.
inline uint64_t enqueueInventoryAction(World& world,InventoryAction request) {
    auto* queue=world.resource<InventoryCommandQueue>();
    if(!queue || !queue->nextId || queue->nextId==UINT64_MAX)return 0;
    request.id=queue->nextId++;
    queue->pending.push_back(std::move(request));
    return queue->nextId-1;
}

// Runs after command authorization, before consumers (equipment, crafting,
// shop/loot completion). Transactions are applied once in enqueue order.
// Invalid targets, stacks and capacity failures must leave both bags intact.
inline void addInventoryTransactionSystem(Simulation& sim,std::string name) {
    sim.addSystem(std::move(name),[](Simulation& current) {
        World& world=current.world();
        auto* requests=world.resource<InventoryCommandQueue>();
        auto* results=world.resource<InventoryResultQueue>();
        if(!requests||!results)return;
        results->events.clear();
        if(requests->pending.empty())return;
        std::vector<InventoryAction> batch;
        batch.swap(requests->pending);
        const auto* catalog=static_cast<const World&>(world).resourceConst<InventoryCatalog>();
        auto* inventories=world.prepareExistingWrite<InventoryComponent>();
        auto lookup=[catalog](int64_t type)->std::optional<gameplay::ItemType> {
            if(!catalog)return std::nullopt;
            const auto it=catalog->byType.find(type);
            return it==catalog->byType.end()?std::nullopt:
                   std::optional<gameplay::ItemType>{it->second};
        };
        for(const InventoryAction& request:batch) {
            gameplay::Decision decision{gameplay::Reason::Invalid};
            InventoryComponent* from=(inventories&&world.alive(request.source))?
                inventories->tryGet(request.source.index):nullptr;
            InventoryComponent* to=(inventories&&world.alive(request.destination))?
                inventories->tryGet(request.destination.index):nullptr;
            if(from&&catalog) {
                switch(request.operation) {
                case InventoryOperation::Add:
                    decision=gameplay::addItem(from->items,from->config,
                                               request.item,lookup);
                    break;
                case InventoryOperation::Remove:
                    decision=gameplay::removeItem(from->items,request.item);
                    break;
                case InventoryOperation::Transfer:
                    if(to && !(request.source==request.destination))
                        decision=gameplay::transferItem(from->items,to->items,
                                  to->config,request.item,lookup);
                    else decision={gameplay::Reason::Target};
                    break;
                }
            } else if(!from)decision={gameplay::Reason::Target};
            results->events.push_back(InventoryActionResult{
                request.id,request.operation,request.source,
                request.destination,decision});
        }
    });
}

} // namespace aiverse::lib::mechanics
