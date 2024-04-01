/*
 * Project Capsaicin: world_space_restir.cpp
 * Created: 2024/3/28
 * This program uses MulanPSL2. See LICENSE for more.
 */
#include "world_space_restir.h"

#include "capsaicin_internal.h"
#include "migi_fwd.h"

namespace Capsaicin {

WorldSpaceReSTIR::WorldSpaceReSTIR(const GfxContext & gfx) :
    gfx_(gfx), reservoir_indirect_sample_buffer_index_(0)
{}

WorldSpaceReSTIR::~WorldSpaceReSTIR()
{
    for (GfxBuffer reservoir_hash_buffer : reservoir_hash_buffers_)
        gfxDestroyBuffer(gfx_, reservoir_hash_buffer);
    for (GfxBuffer reservoir_hash_count_buffer : reservoir_hash_count_buffers_)
        gfxDestroyBuffer(gfx_, reservoir_hash_count_buffer);
    for (GfxBuffer reservoir_hash_index_buffer : reservoir_hash_index_buffers_)
        gfxDestroyBuffer(gfx_, reservoir_hash_index_buffer);
    for (GfxBuffer reservoir_hash_value_buffer : reservoir_hash_value_buffers_)
        gfxDestroyBuffer(gfx_, reservoir_hash_value_buffer);
    gfxDestroyBuffer(gfx_, reservoir_hash_list_buffer_);
    gfxDestroyBuffer(gfx_, reservoir_hash_list_count_buffer_);

    gfxDestroyBuffer(gfx_, reservoir_indirect_sample_buffer_);
    for (GfxBuffer reservoir_indirect_sample_normal_buffer : reservoir_indirect_sample_normal_buffers_)
        gfxDestroyBuffer(gfx_, reservoir_indirect_sample_normal_buffer);
    gfxDestroyBuffer(gfx_, reservoir_indirect_sample_material_buffer_);
    for (GfxBuffer reservoir_indirect_sample_reservoir_buffer : reservoir_indirect_sample_reservoir_buffers_)
        gfxDestroyBuffer(gfx_, reservoir_indirect_sample_reservoir_buffer);
}

void WorldSpaceReSTIR::ensureMemoryIsAllocated(const MIGIRenderOptions &options)
{
    uint32_t const buffer_width  = options.width;
    uint32_t const buffer_height = options.height;

    uint32_t const max_ray_count = options.restir.max_query_ray_count;

    if (reservoir_hash_buffers_->getCount() != kConstant_NumEntries)
    {
        for (GfxBuffer reservoir_hash_buffer : reservoir_hash_buffers_)
            gfxDestroyBuffer(gfx_, reservoir_hash_buffer);
        for (GfxBuffer reservoir_hash_count_buffer : reservoir_hash_count_buffers_)
            gfxDestroyBuffer(gfx_, reservoir_hash_count_buffer);
        for (GfxBuffer reservoir_hash_index_buffer : reservoir_hash_index_buffers_)
            gfxDestroyBuffer(gfx_, reservoir_hash_index_buffer);
        for (GfxBuffer reservoir_hash_value_buffer : reservoir_hash_value_buffers_)
            gfxDestroyBuffer(gfx_, reservoir_hash_value_buffer);

        for (uint32_t i = 0; i < ARRAYSIZE(reservoir_hash_buffers_); ++i)
        {
            char buffer[64];
            GFX_SNPRINTF(buffer, sizeof(buffer), "Capsaicin_Reservoir_HashBuffer%u", i);

            reservoir_hash_buffers_[i] =
                gfxCreateBuffer<uint32_t>(gfx_, WorldSpaceReSTIR::kConstant_NumEntries);
            reservoir_hash_buffers_[i].setName(buffer);
        }

        for (uint32_t i = 0; i < ARRAYSIZE(reservoir_hash_count_buffers_); ++i)
        {
            char buffer[64];
            GFX_SNPRINTF(buffer, sizeof(buffer), "Capsaicin_Reservoir_HashCountBuffer%u", i);

            reservoir_hash_count_buffers_[i] =
                gfxCreateBuffer<uint32_t>(gfx_, WorldSpaceReSTIR::kConstant_NumEntries);
            reservoir_hash_count_buffers_[i].setName(buffer);
        }

        for (uint32_t i = 0; i < ARRAYSIZE(reservoir_hash_index_buffers_); ++i)
        {
            char buffer[64];
            GFX_SNPRINTF(buffer, sizeof(buffer), "Capsaicin_Reservoir_HashIndexBuffer%u", i);

            reservoir_hash_index_buffers_[i] =
                gfxCreateBuffer<uint32_t>(gfx_, WorldSpaceReSTIR::kConstant_NumEntries);
            reservoir_hash_index_buffers_[i].setName(buffer);
        }

        for (uint32_t i = 0; i < ARRAYSIZE(reservoir_hash_value_buffers_); ++i)
        {
            char buffer[64];
            GFX_SNPRINTF(buffer, sizeof(buffer), "Capsaicin_Reservoir_HashValueBuffer%u", i);

            reservoir_hash_value_buffers_[i] =
                gfxCreateBuffer<uint32_t>(gfx_, WorldSpaceReSTIR::kConstant_NumEntries);
            reservoir_hash_value_buffers_[i].setName(buffer);
        }
    }

    if (reservoir_hash_list_buffer_.getCount() < max_ray_count)
    {
        gfxDestroyBuffer(gfx_, reservoir_hash_list_buffer_);
        gfxDestroyBuffer(gfx_, reservoir_hash_list_count_buffer_);

        reservoir_hash_list_buffer_ = gfxCreateBuffer<uint4>(gfx_, max_ray_count);
        reservoir_hash_list_buffer_.setName("Capsaicin_Reservoir_HashListBuffer");

        reservoir_hash_list_count_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, 1);
        reservoir_hash_list_count_buffer_.setName("Capsaicin_Reservoir_HashListCountBuffer");
    }

    if (reservoir_indirect_sample_buffer_.getCount() < max_ray_count)
    {
        gfxDestroyBuffer(gfx_, reservoir_indirect_sample_buffer_);
        for (GfxBuffer reservoir_indirect_sample_normal_buffer : reservoir_indirect_sample_normal_buffers_)
            gfxDestroyBuffer(gfx_, reservoir_indirect_sample_normal_buffer);
        gfxDestroyBuffer(gfx_, reservoir_indirect_sample_material_buffer_);
        for (GfxBuffer reservoir_indirect_sample_reservoir_buffer :
            reservoir_indirect_sample_reservoir_buffers_)
            gfxDestroyBuffer(gfx_, reservoir_indirect_sample_reservoir_buffer);

        reservoir_indirect_sample_buffer_ = gfxCreateBuffer<float4>(gfx_, max_ray_count);
        reservoir_indirect_sample_buffer_.setName("Capsaicin_Reservoir_IndirectSampleBuffer");

        for (uint32_t i = 0; i < ARRAYSIZE(reservoir_indirect_sample_normal_buffers_); ++i)
        {
            char buffer[64];
            GFX_SNPRINTF(buffer, sizeof(buffer), "Capsaicin_Reservoir_IndirectSampleNormalBuffer%u", i);

            reservoir_indirect_sample_normal_buffers_[i] = gfxCreateBuffer<uint32_t>(gfx_, max_ray_count);
            reservoir_indirect_sample_normal_buffers_[i].setName(buffer);
        }

        reservoir_indirect_sample_material_buffer_ = gfxCreateBuffer<uint32_t>(gfx_, max_ray_count);
        reservoir_indirect_sample_material_buffer_.setName(
            "Capsaicin_Reservoir_IndirectSamplerMaterialBuffer");

        for (uint32_t i = 0; i < ARRAYSIZE(reservoir_indirect_sample_reservoir_buffers_); ++i)
        {
            char buffer[64];
            GFX_SNPRINTF(buffer, sizeof(buffer), "Capsaicin_Reservoir_IndirectSampleReservoirBuffer%u", i);

            reservoir_indirect_sample_reservoir_buffers_[i] = gfxCreateBuffer<uint4>(gfx_, max_ray_count);
            reservoir_indirect_sample_reservoir_buffers_[i].setName(buffer);
        }
    }
}
}