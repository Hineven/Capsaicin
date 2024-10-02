/*
 * Created: 2024/10/2
 * Author:  hineven
 * See LICENSE for licensing.
 */

#include "evaluate.h"
#include "capsaicin_internal.h"

namespace Capsaicin
{
Evaluate::Evaluate(): RenderTechnique("Evaluate") {}

Evaluate::~Evaluate()
{
    terminate();
}

AOVList Evaluate::getAOVs() const noexcept
{
    AOVList aovs;
    aovs.push_back({"Debug", AOV::Write});
    aovs.push_back({"Color", AOV::Read});
    return aovs;
}

bool Evaluate::init(const CapsaicinInternal &capsaicin) noexcept
{
    pixel_mse_buffer_ = gfxCreateBuffer<float>(gfx_, capsaicin.getWidth() * capsaicin.getHeight());
    pixel_mse_buffer_.setName("PixelMSEBuffer");

    pixel_mape_buffer_ = gfxCreateBuffer<float>(gfx_, capsaicin.getWidth() * capsaicin.getHeight());
    pixel_mape_buffer_.setName("PixelMAPEBuffer");

    mse_buffer_ = gfxCreateBuffer<float>(gfx_, 1);
    mse_buffer_.setName("MSEBuffer");

    mape_buffer_ = gfxCreateBuffer<float>(gfx_, 1);
    mape_buffer_.setName("MAPEBuffer");

    for (uint32_t i = 0; i < ARRAYSIZE(readback_buffers_); ++i)
    {
        char buffer[64];
        GFX_SNPRINTF(buffer, sizeof(buffer), "EvaluateReadbackBuffer%u", i);

        readback_buffers_[i] = gfxCreateBuffer<float>(gfx_, 1, nullptr, kGfxCpuAccess_Read);
        readback_buffers_[i].setName(buffer);
    }

    program_ = gfxCreateProgram(
        gfx_, "render_techniques/evaluate/evaluate", capsaicin.getShaderPath());
    compute_kernel_ = gfxCreateComputeKernel(gfx_, program_, "Main");
    return true;
}
}