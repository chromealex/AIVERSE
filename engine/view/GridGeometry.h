// Generic View geometry indices for any regular rectangular quad grid.
// Reusable by flat terrain, spherical terrain faces, heightfields and props.
// Does not import game, map, resource, collision or GPU API types.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::view {
// Append six indices per quad, splitting by diagonal a-d. The exact
// ordering (a,d,b / a,c,d) matches existing terrain and spherical face
// tessellation, including their later host-specific winding correction.
// Does not clear any already appended indices.
inline void appendQuadGridIndices(std::vector<uint32_t>& out,
                                  int columns,int rows) {
    if(columns<2 || rows<2)return;
    const uint64_t vertices=uint64_t(columns)*uint64_t(rows);
    if(vertices>uint64_t(std::numeric_limits<uint32_t>::max()))
        throw std::length_error("View quad grid exceeds 32-bit vertex index limit");
    const auto count=std::size_t(columns-1)*std::size_t(rows-1);
    if(count>(std::numeric_limits<std::size_t>::max()-out.size())/6)
        throw std::length_error("View quad grid index list too large");
    out.reserve(out.size()+count*6);
    for(int j=0;j+1<rows;++j) {
        for(int i=0;i+1<columns;++i) {
            // Widen multiplication before narrowing: a valid uint32 grid
            // may have more than INT_MAX cells.
            const uint32_t a=uint32_t(uint64_t(j)*uint64_t(columns)+
                                      uint64_t(i));
            const uint32_t b=a+1;
            const uint32_t c=a+uint32_t(columns);
            const uint32_t d=c+1;
            out.insert(out.end(),{a,d,b,a,c,d});
        }
    }
}
} // namespace aiverse::view
