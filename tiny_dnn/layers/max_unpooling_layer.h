/*
    Copyright (c) 2015, Taiga Nomi
    All rights reserved.

    Use of this source code is governed by a BSD-style license that can be found
    in the LICENSE file.
*/
#pragma once
#include "tiny_dnn/util/util.h"

#ifdef DNN_USE_IMAGE_API
#include "tiny_dnn/util/image.h"
#endif

namespace tiny_dnn {

/**
 * applies max-pooing operaton to the spatial data
 **/
class max_unpooling_layer : public layer {
 public:
  using layer::parallelize_;

  /**
   * @param in_width     [in] width of input image
   * @param in_height    [in] height of input image
   * @param in_channels  [in] the number of input image channels(depth)
   * @param unpooling_size [in] factor by which to upscale
   **/
  max_unpooling_layer(serial_size_t in_width,
                      serial_size_t in_height,
                      serial_size_t in_channels,
                      serial_size_t unpooling_size)
    : max_unpooling_layer(in_width,
                          in_height,
                          in_channels,
                          unpooling_size,
                          (in_height == 1 ? 1 : unpooling_size)) {}

  max_unpooling_layer(const shape3d &in_size,
                      serial_size_t unpooling_size,
                      serial_size_t stride)
    : max_unpooling_layer(in_size.width_,
                          in_size.height_,
                          in_size.depth_,
                          unpooling_size,
                          (in_size.height_ == 1 ? 1 : unpooling_size)) {}

  /**
   * @param in_width     [in] width of input image
   * @param in_height    [in] height of input image
   * @param in_channels  [in] the number of input image channels(depth)
   * @param unpooling_size [in] factor by which to upscale
   * @param stride       [in] interval at which to apply the filters to the
   *input
  **/
  max_unpooling_layer(serial_size_t in_width,
                      serial_size_t in_height,
                      serial_size_t in_channels,
                      serial_size_t unpooling_size,
                      serial_size_t stride)
    : layer({vector_type::data}, {vector_type::data}),
      unpool_size_(unpooling_size),
      stride_(stride),
      in_(in_width, in_height, in_channels),
      out_(unpool_out_dim(in_width, unpooling_size, stride),
           unpool_out_dim(in_height, unpooling_size, stride),
           in_channels) {
    worker_storage_.in2outmax_.resize(out_.size());
    init_connection();
  }

  serial_size_t fan_in_size() const override { return 1; }

  serial_size_t fan_out_size() const override { return in2out_[0].size(); }

  void forward_propagation(const std::vector<tensor_t *> &in_data,
                           std::vector<tensor_t *> &out_data) override {
    const tensor_t &in = *in_data[0];
    tensor_t &out      = *out_data[0];

    for (size_t sample = 0; sample < in_data[0]->size(); sample++) {
      const vec_t &in_vec = in[sample];
      vec_t &out_vec      = out[sample];

      std::vector<serial_size_t> &max_idx = worker_storage_.in2outmax_;

      for_(parallelize_, 0, in2out_.size(), [&](const blocked_range &r) {
        for (int i = r.begin(); i < r.end(); i++) {
          const auto &in_index = out2in_[i];
          out_vec[i]           = (static_cast<int>(max_idx[in_index]) == i)
                         ? in_vec[in_index]
                         : float_t{0};
        }
      });
    }
  }

  void back_propagation(const std::vector<tensor_t *> &in_data,
                        const std::vector<tensor_t *> &out_data,
                        std::vector<tensor_t *> &out_grad,
                        std::vector<tensor_t *> &in_grad) override {
    tensor_t &prev_delta = *in_grad[0];
    tensor_t &curr_delta = *out_grad[0];

    for (serial_size_t sample = 0; sample < in_data[0]->size(); sample++) {
      vec_t &prev_delta_vec = prev_delta[sample];
      vec_t &curr_delta_vec = curr_delta[sample];

      std::vector<serial_size_t> &max_idx = worker_storage_.in2outmax_;

      for_(parallelize_, 0, in2out_.size(), [&](const blocked_range &r) {
        for (int i = r.begin(); i != r.end(); i++) {
          serial_size_t outi = out2in_[i];
          prev_delta_vec[i]  = (static_cast<int>(max_idx[outi]) == i)
                                ? curr_delta_vec[outi]
                                : float_t{0};
        }
      });
    }
  }

  std::vector<index3d<serial_size_t>> in_shape() const override {
    return {in_};
  }
  std::vector<index3d<serial_size_t>> out_shape() const override {
    return {out_};
  }
  std::string layer_type() const override { return "max-unpool"; }
  size_t unpool_size() const { return unpool_size_; }

  friend struct serialization_buddy;

 private:
  serial_size_t unpool_size_;
  serial_size_t stride_;
  std::vector<serial_size_t> out2in_;               // mapping out => in (N:1)
  std::vector<std::vector<serial_size_t>> in2out_;  // mapping in => out (1:N)

  struct worker_specific_storage {
    std::vector<serial_size_t>
      in2outmax_;  // mapping max_index(out) => in (1:1)
  };

  worker_specific_storage worker_storage_;

  index3d<serial_size_t> in_;
  index3d<serial_size_t> out_;

  static serial_size_t unpool_out_dim(serial_size_t in_size,
                                      serial_size_t unpooling_size,
                                      serial_size_t stride) {
    return static_cast<serial_size_t>(static_cast<int64_t>(in_size) * stride +
                                      unpooling_size - 1);
  }

  void connect_kernel(serial_size_t unpooling_size,
                      serial_size_t inx,
                      serial_size_t iny,
                      serial_size_t c) {
    serial_size_t dxmax = static_cast<serial_size_t>(
      std::min(unpooling_size, inx * stride_ - out_.width_));
    serial_size_t dymax = static_cast<serial_size_t>(
      std::min(unpooling_size, iny * stride_ - out_.height_));

    for (serial_size_t dy = 0; dy < dymax; dy++) {
      for (serial_size_t dx = 0; dx < dxmax; dx++) {
        serial_size_t out_index =
          out_.get_index(static_cast<serial_size_t>(inx * stride_ + dx),
                         static_cast<serial_size_t>(iny * stride_ + dy), c);
        serial_size_t in_index = in_.get_index(inx, iny, c);

        if (in_index >= in2out_.size()) throw nn_error("index overflow");
        if (out_index >= out2in_.size()) throw nn_error("index overflow");
        out2in_[out_index] = in_index;
        in2out_[in_index].push_back(out_index);
      }
    }
  }

  void init_connection() {
    in2out_.resize(in_.size());
    out2in_.resize(out_.size());

    worker_storage_.in2outmax_.resize(in_.size());

    for (serial_size_t c = 0; c < in_.depth_; ++c)
      for (serial_size_t y = 0; y < in_.height_; ++y)
        for (serial_size_t x = 0; x < in_.width_; ++x)
          connect_kernel(static_cast<serial_size_t>(unpool_size_), x, y, c);
  }
};

}  // namespace tiny_dnn
