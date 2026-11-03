// OBS のコマ（obs_source_frame）を中核の見え方（FrameView）にする（処理詳細設計書 PRC-01、付録 A）
#pragma once

#include <obs.h>

#include "core/frame.hpp"

namespace omask::plugin {

core::PixelFormat to_pixel_format(enum video_format format);

// 画素を上から順に並べた見え方（flip が立っていれば上下を逆にした見え方）
core::FrameView to_frame_view(const obs_source_frame &frame);

// flip を当てない、並んでいるままの見え方（画素を写すとき）
core::FrameView to_raw_view(const obs_source_frame &frame);

// ログ・状態の文に出す形式の名前（例 NV12）
const char *format_name(enum video_format format);

} // namespace omask::plugin
