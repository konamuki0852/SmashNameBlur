// 色の変換（処理詳細設計書 付録 B。OBS の YUV_to_RGB と同じ式と、その逆）
#pragma once

#include <array>
#include <cstdint>

#include "core/frame.hpp"

namespace omask::core {

// BT.709・限定範囲の行列と範囲（OBS が 709 の限定範囲のコマに付けるもの。テストと、行列が無いときの代わりに使う）
constexpr std::array<float, 16> kBt709LimitedMatrix{1.164384f,  0.000000f, 1.792741f, -0.972945f, 1.164384f, -0.213249f,
						    -0.532909f, 0.301483f, 1.164384f, 2.112402f,  0.000000f, -1.133402f,
						    0.000000f,  0.000000f, 0.000000f, 1.000000f};
constexpr std::array<float, 3> kBt709LimitedMin{16.0f / 255.0f, 16.0f / 255.0f, 16.0f / 255.0f};
constexpr std::array<float, 3> kBt709LimitedMax{235.0f / 255.0f, 240.0f / 255.0f, 240.0f / 255.0f};

// y・u・v（0〜1）を RGB（0〜1 に丸めない）にする。範囲が限定なら先に各成分を範囲で丸める
std::array<float, 3> yuv_to_rgb(const FrameView &frame, float y, float u, float v);

// RGB（0〜1）を、そのコマの YUV の 8 bit の値にする（塗りつぶし・確認用の枠の色。PRC-06 手順 3）
std::array<uint8_t, 3> rgb_to_yuv_bytes(const FrameView &frame, float r, float g, float b);

} // namespace omask::core
