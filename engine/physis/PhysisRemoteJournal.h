// PHYSIS Stage 26: bounded, checksummed, write-ahead receipt journal.
// One authoritative server process owns the path. No editor/genre dependency.
// The World checkpoint and this sidecar are NOT one cross-file transaction:
// when replay cannot prove outcome, report Unknown/Conflict, never fake Applied.
#pragma once
#include "PhysisRemoteServer.h"
#ifndef _WIN32
#include "PhysisJournalPosixIO.h"
#endif
#include "engine/core/Hash.h"
#include "engine/net/Crypto.h"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aiverse::physis::remote {

class FileJournal {
public:
    static constexpr std::uint32_t kJournalMagic=0x4a485950u;
    static constexpr std::uint16_t kJournalVersion=1;
    static constexpr std::size_t kMaxDiskBytes=16u*1024u*1024u;
#ifndef _WIN32
    static_assert(kMaxDiskBytes==posix_journal::kMaxJournalBytes,
                  "PHYSIS journal serialization and POSIX I/O bounds must match");
#endif
    explicit FileJournal(std::filesystem::path file):file_(std::move(file)){}
    const std::filesystem::path& path() const {return file_;}

    static bool encodeEntries(const std::map<ServerGateway::Key,ServerGateway::Entry>& entries,
                              net::Bytes& output,std::string* error=nullptr) {
        const auto fail=[&](const char* reason){
            if(error)*error=reason;return false;
        };
        if(entries.size()>ServerGateway::kMaxEntries)return fail("PHYSIS journal too many records");
        ByteWriter w;
        w.u32(kJournalMagic);w.u16(kJournalVersion);
        w.u32(std::uint32_t(entries.size()));
        for(const auto& [key,entry]:entries) {
            if(key.principal.empty()||key.principal.size()>256 ||
               !key.instance||!key.request||
               entry.originalFrame.empty() ||
               entry.originalFrame.size()>kMaxFrame ||
               entry.receipt.detail.size()>kMaxText ||
               !validResult(entry.receipt.result) ||
               entry.desired.size()>2048)
                return fail("PHYSIS journal entry exceeds limits");
            Packet p;
            FieldEditBatch batch;
            if(!decode(entry.originalFrame,p) || p.kind!=Message::Submit ||
               p.clientInstance!=key.instance || p.requestId!=key.request ||
               !decodeFieldEditCommand(p.command,batch) ||
               batch.expectedRevision!=entry.expectedRevision)
                return fail("PHYSIS journal entry does not match original edit");
            if(batch.desired.size()!=entry.desired.size())
                return fail("PHYSIS journal fingerprint set has wrong size");
            for(const auto& field:batch.desired) {
                const auto it=entry.desired.find(field.id);
                if(it==entry.desired.end() ||
                   it->second.first!=field.kind ||
                   it->second.second!=authoredFingerprint(field))
                    return fail("PHYSIS journal fingerprint set mismatches request");
            }
            w.str(key.principal);
            w.u64(key.instance);w.u64(key.request);
            w.u32(std::uint32_t(entry.originalFrame.size()));
            w.buf.insert(w.buf.end(),entry.originalFrame.begin(),entry.originalFrame.end());
            w.u8(std::uint8_t(entry.receipt.result));
            w.u32(entry.receipt.scheduledTick);
            w.u64(entry.receipt.revision);
            w.str(entry.receipt.detail);
            w.u64(entry.expectedRevision);
            w.u32(entry.observedTick);
            w.u64(entry.lastTouchedMs);
            w.u32(std::uint32_t(entry.desired.size()));
            for(const auto& [id,typed]:entry.desired) {
                w.str(id);w.u8(std::uint8_t(typed.first));
                w.str(typed.second);
            }
            if(w.buf.size()>kMaxDiskBytes-8)return fail("PHYSIS journal file too large");
        }
        Hasher hash;
        hash.bytes(w.buf.data(),w.buf.size());
        w.u64(hash.value());
        output=std::move(w.buf);
        if(error)error->clear();
        return true;
    }

    static bool decodeEntries(const net::Bytes& buffer,
                              std::map<ServerGateway::Key,ServerGateway::Entry>& result,
                              std::string* error=nullptr) {
        const auto fail=[&](const char* reason){
            if(error)*error=reason;return false;
        };
        if(buffer.size()<18 || buffer.size()>kMaxDiskBytes)
            return fail("PHYSIS journal length invalid");
        Hasher hash;
        hash.bytes(buffer.data(),buffer.size()-8);
        ByteReader checksum(buffer.data()+buffer.size()-8,8);
        if(checksum.u64()!=hash.value())
            return fail("PHYSIS journal checksum mismatch");
        ByteReader r(buffer.data(),buffer.size()-8);
        if(r.u32()!=kJournalMagic || r.u16()!=kJournalVersion)
            return fail("PHYSIS journal format mismatch");
        const auto count=r.u32();
        if(!r.ok() || count>ServerGateway::kMaxEntries)
            return fail("PHYSIS journal count invalid");
        std::map<ServerGateway::Key,ServerGateway::Entry> temporary;
        const auto readString=[&](std::string& out,std::size_t max) {
            const auto length=r.u32();
            if(!r.ok()||length>max)return false;
            out.clear();out.reserve(length);
            for(std::uint32_t i=0;i<length&&r.ok();++i)
                out.push_back(char(r.u8()));
            return r.ok();
        };
        for(std::uint32_t i=0;i<count;++i) {
            ServerGateway::Key key;
            ServerGateway::Entry entry;
            if(!readString(key.principal,256) || key.principal.empty())
                return fail("PHYSIS journal invalid principal");
            key.instance=r.u64();key.request=r.u64();
            const auto length=r.u32();
            if(!r.ok() || !key.instance || !key.request ||
               length==0 || length>kMaxFrame)
                return fail("PHYSIS journal invalid request identity");
            entry.originalFrame.reserve(length);
            for(std::uint32_t j=0;j<length&&r.ok();++j)
                entry.originalFrame.push_back(r.u8());
            entry.receipt.result=Result(r.u8());
            entry.receipt.scheduledTick=r.u32();
            entry.receipt.revision=r.u64();
            if(!readString(entry.receipt.detail,kMaxText))
                return fail("PHYSIS journal invalid receipt text");
            entry.expectedRevision=r.u64();
            entry.observedTick=r.u32();
            entry.lastTouchedMs=r.u64();
            const auto desiredCount=r.u32();
            if(!r.ok()||desiredCount>2048 ||
               !validResult(entry.receipt.result))
                return fail("PHYSIS journal invalid desired field count/status");
            for(std::uint32_t j=0;j<desiredCount;++j) {
                std::string id,fingerprint;
                if(!readString(id,64))return fail("PHYSIS journal invalid field ID");
                const auto kind=AuthoredFieldKind(r.u8());
                if(!readString(fingerprint,1024) || !r.ok() ||
                   (kind!=AuthoredFieldKind::Wind &&
                    kind!=AuthoredFieldKind::Storm &&
                    kind!=AuthoredFieldKind::Gravity))
                    return fail("PHYSIS journal invalid field fingerprint/type");
                if(!entry.desired.emplace(std::move(id),
                    std::make_pair(kind,std::move(fingerprint))).second)
                    return fail("PHYSIS journal duplicate field ID");
            }
            Packet original;
            FieldEditBatch batch;
            if(!decode(entry.originalFrame,original) ||
               original.kind!=Message::Submit ||
               original.clientInstance!=key.instance ||
               original.requestId!=key.request ||
               !decodeFieldEditCommand(original.command,batch) ||
               batch.expectedRevision!=entry.expectedRevision ||
               batch.desired.size()!=entry.desired.size())
                return fail("PHYSIS journal original request invalid");
            for(const auto& asset:batch.desired) {
                const auto it=entry.desired.find(asset.id);
                if(it==entry.desired.end() || it->second.first!=asset.kind ||
                   it->second.second!=authoredFingerprint(asset))
                    return fail("PHYSIS journal desired source fingerprint mismatch");
            }
            if(!temporary.emplace(std::move(key),std::move(entry)).second)
                return fail("PHYSIS journal duplicate request identity");
        }
        if(!r.ok() || !r.atEnd())
            return fail("PHYSIS journal contains trailing/corrupt data");
        result=std::move(temporary);
        if(error)error->clear();
        return true;
    }

    bool load(ServerGateway& gateway,std::string* error=nullptr) const {
        std::error_code ec;
        const bool exists=std::filesystem::exists(file_,ec);
        if(ec) {if(error)*error=ec.message();return false;}
        if(!exists) {
            if(error)error->clear();
            return true;
        }
        const auto length=std::filesystem::file_size(file_,ec);
        if(ec || length>kMaxDiskBytes || length<18) {
            if(error)*error="PHYSIS journal file size invalid";
            return false;
        }
        std::ifstream input(file_,std::ios::binary);
        if(!input) {
            if(error)*error="PHYSIS journal file cannot be opened";
            return false;
        }
        net::Bytes bytes(std::size_t(length),0);
        if(!input.read(reinterpret_cast<char*>(bytes.data()),
                       std::streamsize(length))) {
            if(error)*error="PHYSIS journal file truncated";
            return false;
        }
        std::map<ServerGateway::Key,ServerGateway::Entry> decoded;
        if(!decodeEntries(bytes,decoded,error))return false;
        return gateway.restoreJournal(std::move(decoded),error);
    }

    bool save(const std::map<ServerGateway::Key,ServerGateway::Entry>& entries,
              std::string* error=nullptr) const {
        net::Bytes bytes;
        if(!encodeEntries(entries,bytes,error))return false;
        // Single-server path ownership is mandatory: never reuse the journal
        // across two independent server processes or trust a user path.
        const auto parent=file_.parent_path();
        if(parent.empty() || !std::filesystem::is_directory(parent)) {
            if(error)*error="PHYSIS journal parent directory does not exist";
            return false;
        }
        // Secure, unpredictable temp basename. The containing directory
        // MUST be owned by the single authorized server process.
        const auto nonce=net::randomKey();
        const auto temporary=std::filesystem::path(
            file_.string()+".next-"+net::toHexString(nonce.data(),16));
#ifndef _WIN32
        const int fd=::open(temporary.c_str(),
                            O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0) {
            if(error)*error="PHYSIS journal safe temp creation failed";
            return false;
        }
        std::size_t offset=0;
        bool good=true;
        while(offset<bytes.size()) {
            const auto written=::write(fd,bytes.data()+offset,bytes.size()-offset);
            if(written<0 && errno==EINTR)continue;
            if(written<=0) {good=false;break;}
            offset+=std::size_t(written);
        }
        if(good && ::fsync(fd)!=0)good=false;
        if(::close(fd)!=0)good=false;
        if(!good) {
            std::error_code ignored;
            std::filesystem::remove(temporary,ignored);
            if(error)*error="PHYSIS journal write or fsync failed";
            return false;
        }
        std::error_code ec;
        std::filesystem::rename(temporary,file_,ec);
        if(ec) {
            std::error_code ignored;
            std::filesystem::remove(temporary,ignored);
            if(error)*error="PHYSIS journal atomic rename failed: "+ec.message();
            return false;
        }
        // A successful rename is not crash-durable until the containing
        // directory is synced. Do not silently accept open() failure.
        const int dir=::open(parent.c_str(),O_RDONLY);
        if(dir<0) {
            if(error)*error="PHYSIS journal directory open for fsync failed";
            return false;
        }
        const bool synced=(::fsync(dir)==0);
        const bool closed=(::close(dir)==0);
        if(!synced || !closed) {
            if(error)*error="PHYSIS journal directory fsync/close failed";
            return false;
        }
#else
        HANDLE fd=CreateFileW(temporary.wstring().c_str(),GENERIC_WRITE,
                              0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(fd==INVALID_HANDLE_VALUE) {
            if(error)*error="PHYSIS Windows journal temp create failed";
            return false;
        }
        std::size_t offset=0;
        bool good=true;
        while(offset<bytes.size()) {
            DWORD written=0;
            const DWORD wanted=DWORD(std::min<std::size_t>(
                bytes.size()-offset,1024u*1024u));
            if(!WriteFile(fd,bytes.data()+offset,wanted,&written,nullptr) ||
               written==0) {good=false;break;}
            offset+=written;
        }
        if(good && !FlushFileBuffers(fd))good=false;
        if(!CloseHandle(fd))good=false;
        if(!good ||
           !MoveFileExW(temporary.wstring().c_str(),file_.wstring().c_str(),
                        MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            std::error_code ignored;
            std::filesystem::remove(temporary,ignored);
            if(error)*error="PHYSIS Windows journal flush/replace failed";
            return false;
        }
#endif
        if(error)error->clear();
        return true;
    }

#ifndef _WIN32
    // The canonical admin endpoint pins its already verified journal parent.
    // Keep receipt serialization separate from the POSIX I/O primitives so
    // the filesystem contract can be compiled and tested without ECS.
    bool loadPinned(int parentFd,ServerGateway& gateway,
                    std::string* error=nullptr) const {
        net::Bytes bytes;
        bool missing=false;
        if(!posix_journal::read(parentFd,file_.filename().string(),
                                18,kMaxDiskBytes,bytes,missing,error))
            return false;
        if(missing)return true;
        std::map<ServerGateway::Key,ServerGateway::Entry> decoded;
        if(!decodeEntries(bytes,decoded,error))return false;
        return gateway.restoreJournal(std::move(decoded),error);
    }

    bool savePinned(int parentFd,
                    const std::map<ServerGateway::Key,ServerGateway::Entry>& entries,
                    std::string* error=nullptr) const {
        net::Bytes bytes;
        if(!encodeEntries(entries,bytes,error))return false;
        const auto nonce=net::randomKey();
        return posix_journal::write(parentFd,file_.filename().string(),
            bytes,net::toHexString(nonce.data(),16),error);
    }
#endif

    // Load FIRST, then set the writer, then allow privileged connections.
    // A corrupt journal rejects startup rather than silently losing dedupe.
    bool attach(ServerGateway& gateway,std::string* error=nullptr) const {
        if(!load(gateway,error))return false;
        const auto path=file_;
        gateway.setJournalWriter([path](
            const std::map<ServerGateway::Key,ServerGateway::Entry>& entries,
            std::string* why) {
            return FileJournal(path).save(entries,why);
        });
        return true;
    }

private:
    std::filesystem::path file_;
};

} // namespace aiverse::physis::remote
