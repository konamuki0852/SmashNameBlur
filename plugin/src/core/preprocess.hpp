// 前処理（処理詳細設計書 PRC-04 手順 1・付録 B）。コマを 640x360 の RGB（0〜1、チャネルごとの平面）にする。
#pragma once

#include <cstddef>

#include "core/frame.hpp"
#include "core/params.hpp"

namespace omask::core {

constexpr std::size_t kTensorPlaneSize = static_cast<std::size_t>(kAnalysisWidth) * kAnalysisHeight;
constexpr std::size_t kTensorSize = 3 * kTensorPlaneSize; // NCHW（N=1）。R・G・B の順の平面

// 出力の 1 画素ごとに、元のコマの対応する範囲の平均を取り（YUV は Y と U・V をそれぞれの範囲で平均し、
// OBS と同じ式で RGB に直す）、0〜1 に丸めて out（kTensorSize 個）に書く。明るさ（全要素の平均）を返す。
// 対応外の形式・大きさが 0 のときは out を 0 にして 0 を返す
float preprocess_frame(const FrameView &frame, float *out);

} // namespace omask::core
