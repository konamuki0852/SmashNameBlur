// コマの画素の見え方（所有しない）と、形式ごとの成分の並び（処理詳細設計書 付録 A）。OBS に依存しない。
#pragma once

#include <array>
#include <cstdint>

namespace omask::core {

// 対応するコマの形式（付録 A）。それ以外は kUnsupported
enum class PixelFormat { kNv12, kI420, kI422, kI444, kYuy2, kYvyu, kUyvy, kRgba, kBgra, kBgrx, kUnsupported };

// コマの画素（OBS の obs_source_frame に相当）。平面の先頭と 1 行のバイト数、色の行列と範囲を持つ
struct FrameView {
	PixelFormat format = PixelFormat::kUnsupported;
	int width = 0;
	int height = 0;
	std::array<uint8_t *, 4> data{};
	std::array<int, 4> linesize{};
	bool full_range = false;
	std::array<float, 16> color_matrix{}; // YUV → RGB の 4x4（行優先。OBS の color_matrix）
	std::array<float, 3> color_range_min{0.0f, 0.0f, 0.0f};
	std::array<float, 3> color_range_max{1.0f, 1.0f, 1.0f};
};

// 1 つの成分（Y・U・V または R・G・B・A）の置き場所
struct Component {
	int plane = 0;  // 平面の番号
	int sub_x = 1;  // 横の間引き（1 か 2）
	int sub_y = 1;  // 縦の間引き（1 か 2）
	int step = 1;   // 同じ成分の隣の標本までのバイト数
	int offset = 0; // 標本の中での位置（バイト）
};

// 形式ごとの成分の並び。YUV は Y・U・V、RGB は R・G・B・A（X）の順
struct ComponentLayout {
	int count = 0;
	std::array<Component, 4> components{};
};

bool is_supported(PixelFormat format);
bool is_yuv(PixelFormat format);
// 色差が間引かれた形式（範囲を偶数にそろえる必要がある。PRC-06 手順 1）
bool is_chroma_subsampled(PixelFormat format);
ComponentLayout component_layout(PixelFormat format);

// 平面の数と、平面ごとの行の数（画素を写すときに使う）
int plane_count(PixelFormat format);
int plane_rows(const FrameView &frame, int plane);

// 上下を逆にした見え方にする（各平面の先頭を最後の行にし、1 行のバイト数を負にする）。
// OBS のコマは flip が立っていると下の行から並んでいるので、これで上の行からの並びとして扱える
void flip_vertically(FrameView &frame);

// 成分の標本を読む・書く（範囲の確認はしない）
inline uint8_t *sample_ptr(const FrameView &frame, const Component &c, int x, int y)
{
	return frame.data[c.plane] + static_cast<int64_t>(y) * frame.linesize[c.plane] + x * c.step + c.offset;
}

// 成分の大きさ（標本の数）
inline int component_width(const FrameView &frame, const Component &c)
{
	return (frame.width + c.sub_x - 1) / c.sub_x;
}
inline int component_height(const FrameView &frame, const Component &c)
{
	return (frame.height + c.sub_y - 1) / c.sub_y;
}

} // namespace omask::core
