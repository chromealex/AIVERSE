#include "Rating.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace aiverse::meta {
namespace {
void valid(bool ok) {
    if (!ok)
        throw std::invalid_argument("invalid rating input or convergence");
}
constexpr double scale = 173.7178;
double g(double phi) {
    return 1 / std::sqrt(1 + 3 * phi * phi / (std::numbers::pi * std::numbers::pi));
}
double expected(double mu, double other, double phi) {
    return std::clamp(1 / (1 + std::exp(std::clamp(-g(phi) * (mu - other), -50.0, 50.0))), 1e-15, 1 - 1e-15);
}
} // namespace
double elo(double r, double opponent, double score, double k) {
    valid(std::isfinite(r) && std::isfinite(opponent) && std::isfinite(score) && std::isfinite(k) && score >= 0 &&
          score <= 1 && k > 0 && k <= 10000);
    return r + k * (score - 1 / (1 + std::pow(10, std::clamp((opponent - r) / 400, -50.0, 50.0))));
}
Rating glicko2(Rating p, std::span<const RatedGame> games, double tau) {
    valid(std::isfinite(p.value) && std::isfinite(p.deviation) && std::isfinite(p.volatility) && std::isfinite(tau) &&
          p.deviation > 0 && p.deviation <= 1e6 && p.volatility > 0 && p.volatility <= 10 && tau >= 0.01 && tau <= 2 &&
          games.size() <= 10000);
    double mu = (p.value - 1500) / scale, phi = p.deviation / scale;
    if (games.empty()) {
        p.deviation = scale * std::sqrt(phi * phi + p.volatility * p.volatility);
        return p;
    }
    double inv = 0, sum = 0;
    for (auto &match : games) {
        valid(std::isfinite(match.opponent) && std::isfinite(match.deviation) && std::isfinite(match.score) &&
              match.deviation > 0 && match.deviation <= 1e6 && match.score >= 0 && match.score <= 1);
        double pg = match.deviation / scale, gg = g(pg), e = expected(mu, (match.opponent - 1500) / scale, pg);
        inv += gg * gg * e * (1 - e);
        sum += gg * (match.score - e);
    }
    valid(inv > 0 && std::isfinite(inv));
    double variance = 1 / inv, delta = variance * sum, a = std::log(p.volatility * p.volatility);
    auto f = [&](double x) {
        double ex = std::exp(x), den = phi * phi + variance + ex;
        return ex * (delta * delta - phi * phi - variance - ex) / (2 * den * den) - (x - a) / (tau * tau);
    };
    double A = a, B = 0;
    if (delta * delta > phi * phi + variance)
        B = std::log(delta * delta - phi * phi - variance);
    else {
        int k = 1;
        while (k < 10000 && f(a - k * tau) < 0)
            ++k;
        valid(k < 10000);
        B = a - k * tau;
    }
    double fA = f(A), fB = f(B);
    int iterations = 0;
    while (std::abs(B - A) > 1e-6 && iterations++ < 1000) {
        valid(std::isfinite(fA) && std::isfinite(fB) && fB != fA);
        double C = A + (A - B) * fA / (fB - fA), fC = f(C);
        if (fC * fB <= 0) {
            A = B;
            fA = fB;
        } else
            fA /= 2;
        B = C;
        fB = fC;
    }
    valid(iterations < 1000);
    double sigma = std::exp(A / 2), prior = std::sqrt(phi * phi + sigma * sigma),
           next = 1 / std::sqrt(1 / (prior * prior) + 1 / variance);
    Rating result{scale * (mu + next * next * sum) + 1500, scale * next, sigma};
    valid(std::isfinite(result.value) && std::isfinite(result.deviation) && std::isfinite(result.volatility));
    return result;
}
} // namespace aiverse::meta
