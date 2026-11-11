#pragma once
#include <span>
namespace aiverse::meta {
// Authoritative meta only: libm doubles are deliberately not simulation/lockstep-safe.
struct Rating { double value=1500, deviation=350, volatility=0.06; };
struct RatedGame { double opponent=1500, deviation=350, score=0; };
double elo(double rating,double opponent,double score,double k=32);
Rating glicko2(Rating player,std::span<const RatedGame> games,double tau=0.5);
}
