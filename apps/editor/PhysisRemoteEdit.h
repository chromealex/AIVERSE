// PHYSIS Stage 25: optional authenticated remote-connection bridge for editor.
// A project/network host MUST attach an already authenticated ::aiverse::net::Conn.
// Explicit global network qualification is REQUIRED here: aiverse::editor
// also declares its own net namespace, which would shadow aiverse::net.
// This file intentionally does NOT create anonymous sockets or credentials.
#pragma once
#include "engine/physis/PhysisRemoteClient.h"
#ifndef __EMSCRIPTEN__
#include "engine/physis/PhysisAdminEndpoint.h"
#endif
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace aiverse::editor::physis_remote {

class EditorLink {
public:
    bool attached() const {return bool(connection_) && bool(client_);}
    bool authenticating() const {
#ifndef __EMSCRIPTEN__
        return bool(pendingAdmin_);
#else
        return false;
#endif
    }
#ifndef __EMSCRIPTEN__
    // Host-owned secret is supplied by trusted configuration/secure storage.
    // The native WebSocket client does NOT support WSS yet. This helper only
    // connects to loopback, using challenge-response authentication.
    bool connectLocalAdmin(const std::string& loopbackUrl,
                           const std::string& principal,::aiverse::net::Key key,
                           std::uint64_t clientInstance,
                           std::uint8_t playerSlot,
                           std::uint16_t commandType,
                           std::string* error=nullptr) {
        if(!clientInstance || !playerSlot || !commandType ||
           (client_ && client_->unresolved() &&
            (client_->instance()!=clientInstance ||
             playerSlot!=editorPlayerSlot_ ||
             commandType!=commandType_))) {
            if(error)*error="PHYSIS invalid admin connection identity";
            ::aiverse::net::wipe(key.data(),key.size());
            return false;
        }
        auto dialer=std::make_unique<physis::remote::admin::LoopbackAdminConnector>();
        if(!dialer->connect(loopbackUrl,principal,key,error)) {
            ::aiverse::net::wipe(key.data(),key.size());
            return false;
        }
        ::aiverse::net::wipe(key.data(),key.size());
        pendingInstance_=clientInstance;
        pendingSlot_=playerSlot;
        pendingCommandType_=commandType;
        pendingAdmin_=std::move(dialer);
        message_="PHYSIS admin connection authenticating";
        return true;
    }
#endif
    bool connected() const {
        return attached() && connection_->state()==::aiverse::net::ConnState::Connected;
    }
    const physis::remote::RemoteClient* client() const {return client_.get();}
    std::string_view message() const {return message_;}
    std::uint8_t slot() const {return editorPlayerSlot_;}
    std::uint16_t commandType() const {return commandType_;}

    // Only call after a real host connection handshake/identity check.
    bool attachAuthenticated(std::unique_ptr<::aiverse::net::Conn> ready,
                             std::uint64_t clientInstance,
                             std::uint8_t editorPlayerSlot,
                             std::uint16_t reservedCommandType,
                             std::string* error=nullptr) {
        if(!ready || !clientInstance || !editorPlayerSlot || !reservedCommandType) {
            if(error)*error="PHYSIS remote route needs authenticated connection and IDs";
            return false;
        }
        if(client_ && client_->unresolved() &&
           (client_->instance()!=clientInstance ||
            editorPlayerSlot_!=editorPlayerSlot ||
            commandType_!=reservedCommandType)) {
            if(error)*error="PHYSIS cannot replace the identity of an outstanding edit";
            return false;
        }
        if(!client_ || client_->instance()!=clientInstance)
            client_=std::make_unique<physis::remote::RemoteClient>(clientInstance);
        if(connection_)connection_->close();
        connection_=std::move(ready);
        editorPlayerSlot_=editorPlayerSlot;
        commandType_=reservedCommandType;
        client_->disconnected();
        if(!client_->waiting())
            (void)client_->requestRevision();
        message_="PHYSIS editor attached to host-authenticated remote transport";
        if(error)error->clear();
        return true;
    }

    // Disconnect retains pending request so it can be queried after a new
    // transport attaches with the SAME authenticated client instance ID.
    void disconnect() {
#ifndef __EMSCRIPTEN__
        pendingAdmin_.reset();
#endif
        if(connection_)connection_->close();
        connection_.reset();
        if(client_)client_->disconnected();
        message_="PHYSIS network disconnected; pending request retained";
    }
    void poll(std::uint64_t nowMs) {
#ifndef __EMSCRIPTEN__
        if(pendingAdmin_) {
            pendingAdmin_->poll(nowMs);
            if(pendingAdmin_->ready()) {
                auto conn=pendingAdmin_->takeAuthenticated();
                pendingAdmin_.reset();
                std::string error;
                if(!attachAuthenticated(std::move(conn),pendingInstance_,
                        pendingSlot_,pendingCommandType_,&error))
                    message_=error;
            } else if(pendingAdmin_->rejected()) {
                message_="PHYSIS administrative authentication rejected";
                pendingAdmin_.reset();
            }
        }
#endif
        if(!attached())return;
        client_->poll(*connection_,nowMs);
        if(client_->status()==physis::remote::ClientStatus::Applied ||
           client_->status()==physis::remote::ClientStatus::Conflict)
            message_=client_->receipt().detail;
    }
    bool submit(const Json& authoredMap,std::string* error=nullptr) {
        const auto fail=[&](const char* reason) {
            message_=reason;
            if(error)*error=message_;
            return false;
        };
        if(!connected())return fail("PHYSIS remote transport not connected");
        if(client_->waiting())return fail("PHYSIS previous edit is unresolved");
        if(!client_->hasRemoteRevision())
            return fail("PHYSIS remote revision unknown; refresh it first");
        try {
            const auto options=physis::optionsFromMap(authoredMap);
            if(!options)return fail("PHYSIS authored map is missing or disabled");
            auto fields=physis::parseFieldAssets(authoredMap["physis"],*options);
            physis::FieldEditBatch batch{client_->remoteRevision(),std::move(fields)};
            if(!client_->begin(batch,commandType_,editorPlayerSlot_,error))
                return fail("PHYSIS remote command could not be encoded");
            message_="PHYSIS remote edit pending; server receipt not yet received";
            if(error)error->clear();
            return true;
        } catch(const std::exception& ex) {
            message_=ex.what();
            if(error)*error=message_;
            return false;
        }
    }
    void refreshRevision() {
        if(client_ && !client_->unresolved())(void)client_->requestRevision();
    }
    void retryOutstanding() {
        if(client_)client_->retry();
    }
    ::aiverse::net::Bytes saveOutstanding() const {
        return client_ ? client_->exportOutstanding() : ::aiverse::net::Bytes{};
    }
    bool restoreOutstanding(const ::aiverse::net::Bytes& encoded,std::string* error=nullptr) {
        return client_ && client_->restoreOutstanding(encoded,error);
    }
private:
    std::unique_ptr<::aiverse::net::Conn> connection_;
    std::unique_ptr<physis::remote::RemoteClient> client_;
    std::uint8_t editorPlayerSlot_=0;
    std::uint16_t commandType_=0;
    std::string message_;
#ifndef __EMSCRIPTEN__
    std::unique_ptr<physis::remote::admin::LoopbackAdminConnector> pendingAdmin_;
    std::uint64_t pendingInstance_=0;
    std::uint8_t pendingSlot_=0;
    std::uint16_t pendingCommandType_=0;
#endif
};

// Editor/network project host attaches a trusted link here after its own
// secure handshake; the generic PHYSIS window does not auto-connect.
inline EditorLink& link() {
    static EditorLink state;
    return state;
}
} // namespace aiverse::editor::physis_remote
