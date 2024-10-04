/*
 * Created: 2024/10/2
 * Author:  hineven
 * See LICENSE for licensing.
 */

#ifndef CAPSAICIN_EVALUATE_H
#define CAPSAICIN_EVALUATE_H

#include "render_technique.h"

    namespace Capsaicin
{
    class Evaluate : public RenderTechnique
    {
    public:
        Evaluate();
        ~Evaluate();

        AOVList getAOVs() const noexcept override;

        bool init(CapsaicinInternal const &capsaicin) noexcept override;

        void render(CapsaicinInternal &capsaicin) noexcept override;

        void terminate() noexcept override;

        void renderGUI(CapsaicinInternal &capsaicin) const noexcept override;

        DebugViewList getDebugViews() const noexcept;

    protected:

        GfxBuffer pixel_mse_buffer_;
        GfxBuffer pixel_mape_buffer_;
        GfxBuffer mse_buffer_;
        GfxBuffer mape_buffer_;
        GfxTexture reference_;

        GfxBuffer readback_buffers_[kGfxConstant_BackBufferCount];
        bool      readback_pending_[kGfxConstant_BackBufferCount];

        GfxProgram program_;
        GfxKernel  evaluate_kernel_;
        GfxKernel  visualize_kernel_;

        int readback_index_{};
        float mse_{};
        float mape_{};

        mutable int mode_ {};
        mutable float shift_ {};
        mutable float angle_ {};
    };
}

#endif // CAPSAICIN_EVALUATE_H
