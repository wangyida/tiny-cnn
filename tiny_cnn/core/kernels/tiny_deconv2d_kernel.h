/*
    Copyright (c) 2016, Taiga Nomi, Edgar Riba
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once

#include "tiny_cnn/core/params/deconv_params.h"

namespace tiny_cnn {
namespace core {
namespace kernels {

inline void tiny_deconv2d_kernel(const deconv_params& params,
                                 const vec_t&       in,
                                 const vec_t&       W,
                                 const vec_t&       bias,
                                 vec_t&             a,
                                 const bool layer_parallelize) {
    for_i(layer_parallelize, params.out.depth_, [&](int o) {
        for (cnn_size_t inc = 0; inc < params.in.depth_; inc++) {
            if (!params.tbl.is_connected(o, inc)) continue;

            cnn_size_t idx = 0;
            idx = params.in.depth_ * o + inc;
            idx = params.weight.get_index(0, 0, idx);
            const float_t *pw = &W[idx];

            idx = params.in.get_index(0, 0, inc);
            const float_t *pi = &in[idx];

            idx = params.out.get_index(0, 0, o);
            float_t *pa = &a[idx];

            for (cnn_size_t y = 0; y < params.in.height_; y++) {
                for (cnn_size_t x = 0; x < params.in.width_; x++) {
                    const float_t * ppw = pw;
                    const float_t * ppi = pi + y * params.in.width_ + x;
                    // should be optimized for small kernel(3x3,5x5)
                    for (cnn_size_t wy = 0; wy < params.weight.height_; wy++) {
                        for (cnn_size_t wx = 0; wx < params.weight.width_; wx++) {
                            pa[(y+wy) * params.h_stride *
                                params.out.width_ + (x+wx) *
                                params.w_stride] += ppw[wy *
                                params.weight.width_ + wx] * (*ppi);
                        }
                    }
                }
            }
        }

        if (params.has_bias) {
            float_t * pa  = &a[params.out.get_index(0, 0, o)];
            float_t * paa = pa + params.out.width_ * params.out.height_;
            std::for_each(pa, paa, [&](float_t& f) { f += bias[o]; });
        }
    });
}

}  // namespace kernels
}  // namespace core
}  // namespace tiny_cnn
