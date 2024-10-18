/*
 * Created: 2024/10/2
 * Author:  hineven
 * See LICENSE for licensing.
 */

#include "evaluate.h"
#include "capsaicin_internal.h"

// Supress C4244
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 6262)

//#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#pragma warning(pop)


namespace Capsaicin
{
Evaluate::Evaluate()
    : RenderTechnique("Evaluate")
{}

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
    reference_ =
        gfxCreateTexture2D(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), DXGI_FORMAT_R8G8B8A8_UNORM);
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

        readback_buffers_[i] = gfxCreateBuffer<float>(gfx_, 2, nullptr, kGfxCpuAccess_Read);
        readback_buffers_[i].setName(buffer);
    }

    program_ = gfxCreateProgram(gfx_, "render_techniques/evaluate/evaluate", capsaicin.getShaderPath());
    evaluate_kernel_  = gfxCreateComputeKernel(gfx_, program_, "Evaluate");
    visualize_kernel_ = gfxCreateComputeKernel(gfx_, program_, "Visualize");

    // Upload reference texture
    int  in_img_width, in_img_height, in_img_channels;
    auto img_uc = stbi_load("reference.jpeg", &in_img_width, &in_img_height, &in_img_channels, 0);
    if (!img_uc || in_img_width != (int)capsaicin.getWidth() || in_img_height != (int)capsaicin.getHeight())
    {
        puts("Failed to load reference image");
        return false;
    }
    if(in_img_channels != 4) {
        // expand to 4 channels
        auto img_uc_new = new unsigned char[in_img_width * in_img_height * 4];
        for (int i = 0; i < in_img_width * in_img_height; ++i)
        {
            img_uc_new[i * 4 + 0] = img_uc[i * in_img_channels + 0];
            img_uc_new[i * 4 + 1] = (in_img_channels >= 2) ? img_uc[i * in_img_channels + 1] : 0;
            img_uc_new[i * 4 + 2] = (in_img_channels >= 3) ? img_uc[i * in_img_channels + 2] : 0;
            img_uc_new[i * 4 + 3] = 255;
        }
        stbi_image_free(img_uc);
        img_uc = img_uc_new;
    }
    auto img = new float[in_img_width * in_img_height * 4];
    for (int i = 0; i < in_img_width * in_img_height * 4; ++i)
    {
        img[i] = 1.f;//(float)img_uc[i] / 255.0f;
    }
    auto staging_buffer = gfxCreateBuffer(gfx_, in_img_width * in_img_height * 4, img_uc);
    gfxCommandCopyBufferToTexture(gfx_, reference_, staging_buffer);
    gfxDestroyBuffer(gfx_, staging_buffer);
    if(in_img_channels == 4) stbi_image_free(img_uc);
    else delete [] img_uc;
    delete [] img;
    return true;
}

void Evaluate::terminate() noexcept
{
    for (uint32_t i = 0; i < ARRAYSIZE(readback_buffers_); ++i)
    {
        gfxDestroyBuffer(gfx_, readback_buffers_[i]);
    }
    gfxDestroyKernel(gfx_, visualize_kernel_);
    gfxDestroyKernel(gfx_, evaluate_kernel_);
    gfxDestroyProgram(gfx_, program_);
    gfxDestroyBuffer(gfx_, mape_buffer_);
    gfxDestroyBuffer(gfx_, mse_buffer_);
    gfxDestroyBuffer(gfx_, pixel_mape_buffer_);
    gfxDestroyBuffer(gfx_, pixel_mse_buffer_);
}

void Evaluate::render(CapsaicinInternal &capsaicin) noexcept
{
    auto color_aov = capsaicin.getAOVBuffer("Color");
    auto debug_aov = capsaicin.getAOVBuffer("Debug");
    gfxProgramSetParameter(gfx_, program_, "g_RWColor", color_aov);
    gfxProgramSetParameter(gfx_, program_, "g_Reference", reference_);
    gfxProgramSetParameter(gfx_, program_, "g_RWDebug", debug_aov);
    gfxProgramSetParameter(gfx_, program_, "g_Mode", mode_);
    gfxProgramSetParameter(gfx_, program_, "g_RWPixelSEBuffer", pixel_mse_buffer_);
    gfxProgramSetParameter(gfx_, program_, "g_RWPixelPEBuffer", pixel_mape_buffer_);
    gfxProgramSetParameter(gfx_, program_, "g_Shift", shift_);
    gfxProgramSetParameter(gfx_, program_, "g_Angle", angle_);
    gfxProgramSetParameter(gfx_, program_, "g_Exposure", exposure_);

    gfxCommandBindKernel(gfx_, evaluate_kernel_);
    auto threads          = gfxKernelGetNumThreads(gfx_, evaluate_kernel_);
    auto divideAndRoundUp = [](uint32_t x, uint32_t y) -> uint32_t {
        return (x + y - 1) / y;
    };
    gfxCommandDispatch(gfx_, divideAndRoundUp(capsaicin.getWidth(), threads[0]),
        divideAndRoundUp(capsaicin.getHeight(), threads[1]), 1);
    gfxCommandReduceSum(gfx_, GfxDataType::kGfxDataType_Float, mse_buffer_, pixel_mse_buffer_);
    gfxCommandReduceSum(gfx_, GfxDataType::kGfxDataType_Float, mape_buffer_, pixel_mape_buffer_);
    if (capsaicin.getCurrentDebugView() == "Evaluate")
    {
        gfxCommandBindKernel(gfx_, visualize_kernel_);
        gfxCommandDispatch(gfx_, capsaicin.getWidth(), capsaicin.getHeight(), 1);
    }
    {
        int cur_idx = readback_index_ % ARRAYSIZE(readback_buffers_);
        gfxCommandCopyBuffer(gfx_, readback_buffers_[cur_idx], 0, mse_buffer_, 0, sizeof(float));
        gfxCommandCopyBuffer(gfx_, readback_buffers_[cur_idx], sizeof(float), mape_buffer_, 0, sizeof(float));
        readback_pending_[cur_idx] = true;
    }
    {
        int prev_idx = (readback_index_ + 1) % ARRAYSIZE(readback_buffers_);
        if (readback_pending_[prev_idx])
        {
            auto buf = gfxBufferGetData(gfx_, readback_buffers_[prev_idx]);
            memcpy(&mse_, buf, sizeof(float));
            memcpy(&mape_, (std::byte *)buf + sizeof(float), sizeof(float));
            mse_                        = mse_ / (capsaicin.getWidth() * capsaicin.getHeight());
            mape_                       = mape_ / (capsaicin.getWidth() * capsaicin.getHeight());
            readback_pending_[prev_idx] = false;
        }
    }
    readback_index_++;
}

void Evaluate::renderGUI([[maybe_unused]] CapsaicinInternal &capsaicin) const noexcept
{
    if (ImGui::CollapsingHeader("Evaluate", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("MSE: %f", mse_);
        ImGui::Text("MAPE: %f", mape_);
        ImGui::SliderInt("Mode", &mode_, 0, 2);
        ImGui::SliderFloat("Shift", &shift_, -1.0f, 1.0f);
        ImGui::SliderFloat("Angle", &angle_, 0.0f, 3.1415f * 2.f);
        ImGui::SliderFloat("Error Exposure", &exposure_, 1.f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    }
}

DebugViewList Evaluate::getDebugViews() const noexcept
{
    DebugViewList views;
    views.push_back({"Evaluate"});
    return views;
}
}