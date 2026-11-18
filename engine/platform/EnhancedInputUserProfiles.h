// Per-local-player Enhanced Input profiles. Separate project, user and OS input.
// Host/presentation only. Not part of deterministic simulation/network snapshots.
#pragma once

#include "EnhancedInputProfileIO.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <utility>

namespace aiverse::platform::enhanced_config {

class UserProfiles {
public:
    using UserId=std::uint32_t;

    void setScope(std::filesystem::path root,std::string projectId) {
        if(root_==root && projectId_==projectId)return;
        root_=std::move(root);
        projectId_=std::move(projectId);
        profiles_.clear();
    }
    std::filesystem::path path(UserId user) const {
        // Stable FNV-1a hash of the project root, independent of std::hash and
        // without exposing raw project paths inside filenames.
        std::uint64_t hash=14695981039346656037ULL;
        for(unsigned char ch:projectId_){hash^=ch;hash*=1099511628211ULL;}
        std::string name="enhanced-input-"+std::to_string(hash);
        if(user!=0)name+="-user-"+std::to_string(user);
        return root_/(name+".json"); // user 0 keeps the existing FPS path
    }
    const Profile* profile(UserId user) const {
        const auto it=profiles_.find(user);
        return it==profiles_.end()?nullptr:&it->second;
    }

    // A missing profile is valid; bad data leaves both runtime and cache untouched.
    bool load(UserId user,lib::gameplay::inputflow::InputFlow& input,
              std::string* error=nullptr) {
        std::error_code ec;
        const auto file=path(user);
        const bool exists=std::filesystem::exists(file,ec);
        if(ec)return detail::inputFileError(error,ec.message());
        if(!exists){profiles_[user]={};return true;}
        Profile loaded;
        lib::gameplay::inputflow::InputFlow staged=input;
        if(!loadProfileFile(file,loaded,error) || !applyProfile(staged,loaded,error))return false;
        input=std::move(staged);
        profiles_[user]=std::move(loaded);
        return true;
    }

    bool rebind(UserId user,lib::gameplay::inputflow::InputFlow& input,
                const std::string& context,std::uint64_t id,
                lib::gameplay::inputflow::Control control,
                lib::gameplay::inputflow::RebindPolicy policy=
                    lib::gameplay::inputflow::RebindPolicy::Swap,
                std::string* error=nullptr) {
        using System=lib::gameplay::inputflow::InputFlow;
        using Policy=lib::gameplay::inputflow::RebindPolicy;
        const auto* original=input.context(context);
        if(!original)return detail::inputFileError(error,"unknown context");
        System staged=input;
        if(!staged.rebind(context,id,control,policy))
            return detail::inputFileError(error,
                policy==Policy::Reject?"binding conflict":"unknown or ambiguous binding");
        Profile next;
        if(const Profile* previous=profile(user))next=*previous;
        const auto* changed=staged.context(context);
        for(const auto& mapping:changed->mappings) {
            const auto before=std::find_if(original->mappings.begin(),original->mappings.end(),
                [&](const auto& prior){return prior.id==mapping.id;});
            if(before==original->mappings.end() || before->control==mapping.control)continue;
            auto entry=std::find_if(next.rebindings.begin(),next.rebindings.end(),
                [&](const auto& prior){return prior.context==context&&prior.mappingId==mapping.id;});
            if(entry!=next.rebindings.end())entry->control=mapping.control;
            else next.rebindings.push_back({context,mapping.id,mapping.control});
        }
        Profile validated;
        System verified=input;
        if(!readProfile(writeProfile(next),validated,error) ||
           !applyProfile(verified,validated,error) ||
           !saveProfileFile(path(user),validated,error))return false;
        input=std::move(verified);
        profiles_[user]=std::move(validated);
        return true;
    }

    bool reset(UserId user,lib::gameplay::inputflow::InputFlow& input,
               const lib::gameplay::inputflow::InputFlow& projectDefaults,
               std::string* error=nullptr) {
        Profile empty;
        if(!saveProfileFile(path(user),empty,error))return false;
        input=projectDefaults;
        profiles_[user]={};
        return true;
    }
private:
    std::filesystem::path root_;
    std::string projectId_;
    std::map<UserId,Profile> profiles_;
};

} // namespace aiverse::platform::enhanced_config
