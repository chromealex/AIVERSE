#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::runtime {
// NativeState-compatible arrays for an existing versioned binary codec.
template<class NativeState>
bool storeBytes(NativeState& state,const std::string& key,const std::string& bytes){
    if(bytes.empty()||bytes.size()>16*1024*1024)return false;
    std::vector<int64_t> values;values.reserve(bytes.size()+1);values.push_back(1);
    for(unsigned char byte:bytes)values.push_back(byte);state.arrays[key]=std::move(values);return true;
}
template<class NativeState>
std::optional<std::string> loadBytes(const NativeState& state,const std::string& key){
    auto it=state.arrays.find(key);if(it==state.arrays.end()||it->second.size()<2||it->second.size()>16*1024*1024+1||it->second[0]!=1)return {};
    std::string bytes;bytes.reserve(it->second.size()-1);
    for(size_t i=1;i<it->second.size();++i){auto byte=it->second[i];if(byte<0||byte>255)return {};bytes.push_back(char(byte));}return bytes;
}
}
