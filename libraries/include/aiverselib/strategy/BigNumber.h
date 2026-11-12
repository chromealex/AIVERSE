#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::lib::strategy {
// Exact nonnegative integers, with a fixed allocation ceiling. No floating-point rounding.
class BigNumber {
public:
    static constexpr uint32_t base = 1000000000;
    static constexpr size_t maxLimbs = 456;
    BigNumber(uint64_t n=0) { while(n) { limbs_.push_back(uint32_t(n%base)); n/=base; } }
    static std::optional<BigNumber> parse(std::string_view s) {
        if(s.empty()||s.size()>maxLimbs*9) return {};
        for(char c:s) if(c<'0'||c>'9') return {};
        BigNumber n;
        for(size_t end=s.size();end>0;) {
            size_t begin=end>9?end-9:0;uint32_t part=0;
            for(size_t i=begin;i<end;++i) part=part*10+uint32_t(s[i]-'0');
            n.limbs_.push_back(part);end=begin;
        }
        n.trim();return n;
    }
    std::string decimal() const {
        if(limbs_.empty()) return "0";
        std::string out=std::to_string(limbs_.back());
        for(size_t i=limbs_.size()-1;i>0;--i) {
            auto part=std::to_string(limbs_[i-1]);out.append(9-part.size(),'0');out+=part;
        }
        return out;
    }
    bool zero() const { return limbs_.empty(); }
    int compare(const BigNumber& other) const {
        if(limbs_.size()!=other.limbs_.size()) return limbs_.size()<other.limbs_.size()?-1:1;
        for(size_t i=limbs_.size();i>0;--i) if(limbs_[i-1]!=other.limbs_[i-1]) return limbs_[i-1]<other.limbs_[i-1]?-1:1;
        return 0;
    }
    bool operator==(const BigNumber&) const = default;
    std::optional<BigNumber> plus(const BigNumber& b) const {
        BigNumber out;uint64_t carry=0;size_t n=std::max(limbs_.size(),b.limbs_.size());
        for(size_t i=0;i<n||carry;++i) {
            if(i==maxLimbs) return {};
            uint64_t sum=carry+(i<limbs_.size()?limbs_[i]:0)+(i<b.limbs_.size()?b.limbs_[i]:0);
            out.limbs_.push_back(uint32_t(sum%base));carry=sum/base;
        }
        return out;
    }
    std::optional<BigNumber> minus(const BigNumber& b) const {
        if(compare(b)<0) return {};
        BigNumber out=*this;int64_t borrow=0;
        for(size_t i=0;i<out.limbs_.size();++i) {
            int64_t v=int64_t(limbs_[i])-(i<b.limbs_.size()?b.limbs_[i]:0)-borrow;
            borrow=v<0;out.limbs_[i]=uint32_t(v+(borrow?base:0));
        }
        out.trim();return out;
    }
    std::optional<BigNumber> times(const BigNumber& b) const {
        if(zero()||b.zero()) return BigNumber{};
        if(limbs_.size()+b.limbs_.size()>maxLimbs+1) return {};
        BigNumber out;out.limbs_.assign(limbs_.size()+b.limbs_.size(),0);
        for(size_t i=0;i<limbs_.size();++i) {
            uint64_t carry=0;
            for(size_t j=0;j<b.limbs_.size()||carry;++j) {
                uint64_t v=out.limbs_[i+j]+carry+(j<b.limbs_.size()?uint64_t(limbs_[i])*b.limbs_[j]:0);
                out.limbs_[i+j]=uint32_t(v%base);carry=v/base;
            }
        }
        out.trim();if(out.limbs_.size()>maxLimbs) return {};return out;
    }
    std::optional<BigNumber> dividedBy(uint32_t divisor,uint32_t* remainder=nullptr) const {
        if(!divisor) return {};
        BigNumber out=*this;uint64_t rest=0;
        for(size_t i=limbs_.size();i>0;--i) {uint64_t v=rest*base+limbs_[i-1];out.limbs_[i-1]=uint32_t(v/divisor);rest=v%divisor;}
        out.trim();if(remainder) *remainder=uint32_t(rest);return out;
    }
private:
    void trim(){while(!limbs_.empty()&&!limbs_.back()) limbs_.pop_back();}
    std::vector<uint32_t> limbs_;
};
}
