// PHYSIS Stage 25: reliable remote editor request client.
// Uses existing net::Conn; connection identity/TLS credentials come from host.
// One outstanding edit at a time to preserve revision order and avoid races.
#pragma once
#include "PhysisRemoteProtocol.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace aiverse::physis::remote {

enum class ClientStatus : std::uint8_t {
    Idle, Offline, AwaitingReceipt, Queued, Applied,
    Rejected, Conflict, Denied, Uncertain
};

class RemoteClient {
public:
    // Host must supply a nonzero randomly generated client instance ID that
    // survives reconnect. Do NOT derive this token from std::hash or a name.
    explicit RemoteClient(std::uint64_t clientInstance)
        : instance_(clientInstance) {}
    std::uint64_t instance() const {return instance_;}
    ClientStatus status() const {return status_;}
    const Receipt& receipt() const {return receipt_;}
    std::uint64_t requestId() const {return active_?active_->requestId:0;}
    std::uint64_t nextRequestId() const {return nextId_;}
    bool hasRemoteRevision() const {return knownRemoteRevision_;}
    std::uint64_t remoteRevision() const {return remoteRevision_;}
    bool probingRevision() const {return probeId_!=0;}
    // Query a fresh, never-used request ID. The server's Unknown response
    // contains its authoritative CURRENT revision. This is discovery, not
    // a command, and requires the authenticated host's read permission.
    bool requestRevision() {
        if(!instance_ || !nextId_ || probeId_ || unresolved())return false;
        probeId_=nextId_++;
        probeAttempts_=0;
        probeNextMs_=0;
        knownRemoteRevision_=false;
        return true;
    }
    bool unresolved() const {
        return active_.has_value() && !terminal(receipt_.result);
    }
    bool waiting() const {
        return unresolved() && status_!=ClientStatus::Uncertain;
    }

    bool begin(const FieldEditBatch& edit,std::uint16_t hostCommandType,
               std::uint8_t authorizedClientSlot,
               std::string* error=nullptr) {
        if(!instance_ || nextId_==0 ||
           status_==ClientStatus::Uncertain || refreshApplied_ ||
           (active_ && !terminal(receipt_.result)) ||
           (active_ && receipt_.result==Result::Applied && !connected_))
            return protocolError(error,"PHYSIS edit already outstanding or instance invalid");
        if(authorizedClientSlot==0 || hostCommandType==0)
            return protocolError(error,"PHYSIS remote edit needs nonzero player and command type");
        Command command;
        if(!encodeFieldEditCommand(edit,hostCommandType,command,error))return false;
        command.player=authorizedClientSlot;
        command.tick=0;command.seq=0;
        Packet packet;
        packet.kind=Message::Submit;
        packet.clientInstance=instance_;
        packet.requestId=nextId_++;
        packet.command=std::move(command);
        net::Bytes encoded;
        if(!encode(packet,encoded,error))return false;
        probeId_=0;
        active_=std::move(packet);
        receipt_=Receipt{};
        status_=ClientStatus::Offline;
        suspendedForJournal_=false;
        suspendedForReconcile_=false;
        refreshApplied_=false;
        refreshQuerySent_=false;
        sent_=false;
        query_=false;
        attempts_=0;
        nextSendMs_=0;
        if(error)error->clear();
        return true;
    }

    // Persist this exact request envelope from application-owned secure
    // storage; restoring it after process restart preserves the same IDs.
    net::Bytes exportOutstanding() const {
        net::Bytes raw;
        if(active_ && !terminal(receipt_.result))
            (void)encode(*active_,raw);
        return raw;
    }
    bool restoreOutstanding(const net::Bytes& raw,std::string* error=nullptr) {
        Packet restored;
        if(!decode(raw,restored,error))return false;
        if(restored.kind!=Message::Submit ||
           restored.clientInstance!=instance_ ||
           restored.requestId==std::numeric_limits<std::uint64_t>::max() ||
           restored.command.player==0)
            return protocolError(error,"PHYSIS invalid restored request identity");
        FieldEditBatch decoded;
        if(!decodeFieldEditCommand(restored.command,decoded,error))return false;
        active_=std::move(restored);
        probeId_=0;
        nextId_=std::max(nextId_,active_->requestId+1);
        receipt_=Receipt{};
        status_=ClientStatus::Offline;
        suspendedForJournal_=false;
        suspendedForReconcile_=false;
        refreshApplied_=false;
        refreshQuerySent_=false;
        sent_=true; // query first: an earlier process might have submitted
        query_=true;
        connected_=false;
        attempts_=0;
        nextSendMs_=0;
        if(error)error->clear();
        return true;
    }

    void disconnected() {
        connected_=false;
        // A journal-durability failure is not a transient network timeout.
        // Keep the explicit-retry requirement across connection loss; a
        // reconnect must not silently re-arm a previously suspended Submit.
        if(active_ && !terminal(receipt_.result) &&
           status_!=ClientStatus::Uncertain)
            status_=ClientStatus::Offline;
    }
    // Explicit user action after an indeterminate network timeout.
    // Original request ID/payload are preserved. If an already-Applied
    // request's verification timed out, only Query it; never resend Submit.
    void retry() {
        if(!active_)return;
        if(terminal(receipt_.result)) {
            if(receipt_.result!=Result::Applied ||
               status_!=ClientStatus::Uncertain)return;
            suspendedForReconcile_=false;
            refreshApplied_=true;
            refreshQuerySent_=false;
            attempts_=0;
            nextSendMs_=0;
            query_=true;
            status_=connected_ ? ClientStatus::AwaitingReceipt
                               : ClientStatus::Offline;
            return;
        }
        // Only a deliberate user action may lift journal-failure suspension.
        suspendedForJournal_=false;
        suspendedForReconcile_=false;
        attempts_=0;
        nextSendMs_=0;
        query_=sent_;
        status_=connected_ ? ClientStatus::AwaitingReceipt : ClientStatus::Offline;
    }

    void poll(net::Conn& conn,std::uint64_t nowMs) {
        conn.update(nowMs);
        if(conn.state()!=net::ConnState::Connected) {
            disconnected();
            return;
        }
        if(!connected_) {
            connected_=true;
            attempts_=0;
            nextSendMs_=0;
            probeAttempts_=0;
            probeNextMs_=0;
            query_=sent_; // on reconnect first ask what happened
            // Also re-query a previously applied edit after reconnect:
            // server rollback may have invalidated the earlier receipt.
            refreshApplied_ = active_ && receipt_.result==Result::Applied &&
                              !suspendedForReconcile_;
            refreshQuerySent_=false;
        }
        net::Bytes wire;
        int maxReceive=64;
        while(maxReceive-- > 0 && conn.recv(wire)) {
            Packet message;
            if(!decode(wire,message) || message.kind!=Message::Receipt ||
               message.clientInstance!=instance_)
                continue;
            if(probeId_ && message.requestId==probeId_) {
                if(message.receipt.result==Result::Unknown) {
                    if(journalRetrySuspended(message.receipt.detail)) {
                        // A broken receipt journal cannot safely establish
                        // a remote editing baseline. Even a plausible ECS
                        // revision is not proof that request history can be
                        // deduplicated durably. Require a NEW explicit
                        // revision probe after server recovery.
                        knownRemoteRevision_=false;
                        receipt_=message.receipt;
                        status_=ClientStatus::Uncertain;
                    } else {
                        knownRemoteRevision_=true;
                        remoteRevision_=message.receipt.revision;
                        if(!active_ && status_==ClientStatus::Uncertain) {
                            // A fresh successful probe clears the previous
                            // journal-failure warning shown by the editor.
                            status_=ClientStatus::Idle;
                            receipt_=Receipt{};
                        }
                    }
                } else if(message.receipt.result==Result::Denied) {
                    receipt_=message.receipt;
                    status_=ClientStatus::Denied;
                }
                probeId_=0;
                continue;
            }
            if(!active_ || message.requestId!=active_->requestId)continue;
            // A resolved edit stays resolved. Old in-flight Queued/Unknown
            // frames must never rewind a durable terminal ACK and trigger an
            // accidental Submit. The only exception is an explicit
            // post-reconnect verification of a previously Applied edit.
            if(suspendedForJournal_ || suspendedForReconcile_)continue;
            if(terminal(receipt_.result) && !refreshApplied_)continue;
            if(refreshApplied_ &&
               (!refreshQuerySent_ || message.receipt.result==Result::Queued))
                continue; // stale pre-query or pre-commit receipt
            const bool wasVerifying=refreshApplied_;
            refreshApplied_=false;
            refreshQuerySent_=false;
            receipt_=message.receipt;
            if(wasVerifying && receipt_.result==Result::Unknown) {
                status_=ClientStatus::Uncertain;
                receipt_.detail="PHYSIS previous ACK missing on server; reconcile manually";
                // This edit was ALREADY acknowledged as Applied. A later
                // delayed receipt must not resume automatic retries; the
                // original ID is retained for explicit operator recovery.
                suspendedForReconcile_=true;
                nextSendMs_=std::numeric_limits<std::uint64_t>::max();
                continue;
            }
            if(receipt_.result==Result::Queued) {
                status_=ClientStatus::Queued;
                query_=true;
                attempts_=0; // received a positive receipt, keep polling
                nextSendMs_=nowMs+250;
            } else if(receipt_.result==Result::Unknown) {
                if(journalRetrySuspended(receipt_.detail)) {
                    // The server cannot durably dedupe requests. Do not
                    // automatically resend or treat this as a lost request.
                    // The user may explicitly retry after journal recovery.
                    status_=ClientStatus::Uncertain;
                    suspendedForJournal_=true;
                    nextSendMs_=std::numeric_limits<std::uint64_t>::max();
                } else {
                    status_=ClientStatus::AwaitingReceipt;
                    query_=false; // server reports missing ID; retry same ID
                    attempts_=0;
                    nextSendMs_=nowMs;
                }
            } else if(receipt_.result==Result::Applied) {
                status_=ClientStatus::Applied;
                knownRemoteRevision_=true;
                remoteRevision_=receipt_.revision;
            } else if(receipt_.result==Result::Conflict) {
                status_=ClientStatus::Conflict;
                knownRemoteRevision_=true;
                remoteRevision_=receipt_.revision;
            }
            else if(receipt_.result==Result::Denied)
                status_=ClientStatus::Denied;
            else status_=ClientStatus::Rejected;
        }
        if(probeId_ && probeAttempts_>=12 && nowMs>=probeNextMs_) {
            probeId_=0;
            if(!active_) status_=ClientStatus::Uncertain;
        }
        if(probeId_ && nowMs>=probeNextMs_ && probeAttempts_<12) {
            Packet probe;
            probe.kind=Message::Query;
            probe.clientInstance=instance_;
            probe.requestId=probeId_;
            net::Bytes encoded;
            if(encode(probe,encoded))conn.send(encoded);
            ++probeAttempts_;
            probeNextMs_=nowMs+std::min<std::uint64_t>(
                200u*probeAttempts_,2000u);
        }
        if(!active_ || (terminal(receipt_.result) && !refreshApplied_) ||
           status_==ClientStatus::Uncertain || nowMs<nextSendMs_)
            return;
        if(attempts_>=12) {
            status_=ClientStatus::Uncertain;
            return;
        }
        Packet outgoing;
        if(query_ || refreshApplied_) {
            outgoing.kind=Message::Query;
            outgoing.clientInstance=instance_;
            outgoing.requestId=active_->requestId;
        } else outgoing=*active_;
        net::Bytes bytes;
        if(!encode(outgoing,bytes)) {
            status_=ClientStatus::Rejected;
            receipt_.result=Result::Rejected;
            receipt_.detail="PHYSIS request could not be serialized";
            return;
        }
        conn.send(bytes);
        if(refreshApplied_ && outgoing.kind==Message::Query)
            refreshQuerySent_=true;
        sent_=true;
        // A reconnect verification only queries the previously Applied
        // receipt. Preserve its last confirmed status until a *new*
        // authoritative reply changes that conclusion. Otherwise a Query
        // send itself incorrectly downgrades Applied to AwaitingReceipt.
        if(status_!=ClientStatus::Queued && !refreshApplied_)
            status_=ClientStatus::AwaitingReceipt;
        ++attempts_;
        // Backoff is only network timing, never fed into deterministic ECS.
        const std::uint64_t delay=std::min<std::uint64_t>(200u*attempts_,2000u);
        nextSendMs_=nowMs+delay;
    }

private:
    std::uint64_t instance_=0,nextId_=1;
    std::uint64_t probeId_=0,probeNextMs_=0;
    std::uint64_t remoteRevision_=0;
    bool knownRemoteRevision_=false;
    unsigned probeAttempts_=0;
    std::optional<Packet> active_;
    Receipt receipt_{};
    ClientStatus status_=ClientStatus::Idle;
    bool connected_=false,sent_=false,query_=false,refreshApplied_=false;
    bool suspendedForJournal_=false;
    bool suspendedForReconcile_=false; // missing ACK needs manual resolution
    bool refreshQuerySent_=false; // never accept a stale pre-query reply
    unsigned attempts_=0;
    std::uint64_t nextSendMs_=0;
};

} // namespace aiverse::physis::remote
