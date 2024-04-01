/*
 * Project Capsaicin: world_space_restir.h
 * Created: 2024/3/28
 * This program uses MulanPSL2. See LICENSE for more.
 */

#ifndef CAPSAICIN_WORLD_SPACE_RESTIR_H
#define CAPSAICIN_WORLD_SPACE_RESTIR_H

#include "capsaicin.h"
#include "migi_fwd.h"

namespace Capsaicin {

// Used for sampling the direct lighting at primary (i.e., direct lighting; disabled by default) and
// secondary path vertices.
struct WorldSpaceReSTIR
{
    enum Constants
    {
        kConstant_NumCells          = 0x40000u,
        kConstant_NumEntriesPerCell = 0x10u,
        kConstant_NumEntries        = kConstant_NumCells * kConstant_NumEntriesPerCell
    };

    WorldSpaceReSTIR(const GfxContext & gfx_);
    ~WorldSpaceReSTIR();

    void ensureMemoryIsAllocated(const MIGIRenderOptions &options);

    GfxBuffer reservoir_hash_buffers_[2];
    GfxBuffer reservoir_hash_count_buffers_[2];
    GfxBuffer reservoir_hash_index_buffers_[2];
    GfxBuffer reservoir_hash_value_buffers_[2];
    GfxBuffer reservoir_hash_list_buffer_;
    GfxBuffer reservoir_hash_list_count_buffer_;
    GfxBuffer reservoir_indirect_sample_buffer_;
    GfxBuffer reservoir_indirect_sample_normal_buffers_[2];
    GfxBuffer reservoir_indirect_sample_material_buffer_;
    GfxBuffer reservoir_indirect_sample_reservoir_buffers_[2];
    uint32_t  reservoir_indirect_sample_buffer_index_;

    const GfxContext & gfx_;
};

}

#endif // CAPSAICIN_WORLD_SPACE_RESTIR_H
