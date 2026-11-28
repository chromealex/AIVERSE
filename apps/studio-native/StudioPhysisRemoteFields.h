#pragma once
#include "apps/editor/PhysisRemoteEdit.h"

namespace aiverse::studio {

inline const char* physisRemoteStatus(physis::remote::ClientStatus status) {
    using S=physis::remote::ClientStatus;
    switch (status) {
        case S::Idle:return "Ready";
        case S::Offline:return "Offline";
        case S::AwaitingReceipt:return "Awaiting receipt";
        case S::Queued:return "Queued on server";
        case S::Applied:return "Applied by server";
        case S::Rejected:return "Rejected";
        case S::Conflict:return "Revision conflict";
        case S::Denied:return "Permission denied";
        case S::Uncertain:return "Receipt unknown";
    }
    return "Unknown";
}

inline Json studioPhysisRemoteFields(editor::physis_remote::EditorLink& link,const Json& request,
                                    uint64_t mapRevision,const Json* map) {
    const auto action=request.getStr("action");
    const auto* client=link.client();
    const auto token=[&](){
        return client?std::to_string(client->instance())+"/"+std::to_string(link.slot())+"/"+
            std::to_string(link.commandType())+"/"+std::to_string(client->nextRequestId()):std::string();
    };
    const auto revision=[&](){return client && client->hasRemoteRevision()?std::to_string(client->remoteRevision()):std::string();};
    const auto requestId=[&](){return client && client->requestId()?std::to_string(client->requestId()):std::string();};
    if(action!="status") {
        if(!link.connected() || !client)throw std::runtime_error("No authenticated PHYSIS server connection.");
        if(request.getStr("connectionVersion")!=token())throw std::runtime_error("The remote session changed. Refresh its status.");
        if(action=="refresh") {
            if(client->unresolved() || client->probingRevision())throw std::runtime_error("A request is already pending.");
            link.refreshRevision();
        } else if(action=="retry") {
            if(!client->requestId() || client->status()!=physis::remote::ClientStatus::Uncertain || request.getStr("requestId")!=requestId())
                throw std::runtime_error("The outstanding request changed. Refresh its status.");
            link.retryOutstanding();
        } else if(action=="apply") {
            if(!map || client->unresolved() || !client->hasRemoteRevision() || request.getStr("remoteRevision")!=revision() ||
                request.getStr("mapRevision")!=std::to_string(mapRevision))
                throw std::runtime_error("Review the current server and map revisions before submitting.");
            std::string error;if(!link.submit(*map,&error))throw std::runtime_error(error);
        } else throw std::runtime_error("Unknown PHYSIS remote field action.");
    }
    Json out=Json::object();
    const bool connected=link.connected();
    out.set("status",Json::string(link.authenticating()?"Authenticating":connected?physisRemoteStatus(client->status()):"Offline"));
    out.set("message",Json::string(link.message().empty()&&!connected?"The project host has no authenticated PHYSIS connection.":std::string(link.message())));
    out.set("available",Json::fromBool(connected && !client->unresolved() && client->hasRemoteRevision() && client->status()!=physis::remote::ClientStatus::Uncertain));
    out.set("canRefresh",Json::fromBool(connected && !client->unresolved() && !client->probingRevision()));
    out.set("canRetry",Json::fromBool(connected && client->requestId() && client->status()==physis::remote::ClientStatus::Uncertain));
    out.set("connectionVersion",Json::string(token()));
    out.set("mapRevision",Json::string(std::to_string(mapRevision)));
    out.set("remoteRevision",Json::string(revision()));
    out.set("requestId",Json::string(requestId()));
    if(client && !client->receipt().detail.empty())out.set("receipt",Json::string(client->receipt().detail));
    return out;
}

} // namespace aiverse::studio
