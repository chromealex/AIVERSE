#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>
namespace aiverse::lib::app {
struct Budget { std::uint64_t maximumP95=16667,maximumPeak=100000;std::uint32_t regressionPermille=100;std::size_t minimumSamples=30; };
struct Measurements { std::uint64_t median=0,p95=0,peak=0;std::size_t count=0; };
inline std::optional<Measurements> summarize(std::vector<std::uint64_t> samples) {
    if(samples.empty())return std::nullopt;
    std::sort(samples.begin(),samples.end());
    const auto n=samples.size(); const auto p95=n-1-(n/20); // nearest-rank ceil(.95*n)-1
    return Measurements{samples[(n-1)/2],samples[p95],samples.back(),n};
}
enum class BudgetResult { Pass, InsufficientSamples, AbsoluteLimit, Regression };
inline BudgetResult checkBudget(const Measurements& current,Budget b={},std::optional<Measurements> baseline={}) {
    if(current.count<b.minimumSamples)return BudgetResult::InsufficientSamples;
    if(current.p95>b.maximumP95||current.peak>b.maximumPeak)return BudgetResult::AbsoluteLimit;
    if(baseline&&current.p95>baseline->p95){
        // Compare in long double on host only; never feed timing measurements into sim outcomes.
        auto allowed=static_cast<long double>(baseline->p95)*(1.0L+static_cast<long double>(b.regressionPermille)/1000.0L);
        if(static_cast<long double>(current.p95)>allowed)return BudgetResult::Regression;
    }
    return BudgetResult::Pass;
}
}
