// 中核で使う基本の型（データ設計書 ENT-06・07）。OBS に依存しない。
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace omask::core {

// 画面の種類（ENT-05・06）。確率の並び（other・vs・battle・result）と同じ順に番号を振る
enum class ScreenKind { kOther = 0, kVs = 1, kBattle = 2, kResult = 3, kUncertain = 4 };

// 解析の状態（ENT-06 analysis_status）。kNotAnalyzed は解析用のバッファが足りず解析しなかったコマ
enum class AnalysisStatus { kOk, kLate, kFailed, kNotAnalyzed };

// 隠し方の決まり（ENT-01 policy）
enum class Policy { kLeakFirst, kFewerMosaic };

// 隠す範囲の種類（ENT-07 kind）
enum class RegionKind { kFixed, kTag };

// 位置（解析座標 640x360）
struct PointF {
	float x = 0.0f;
	float y = 0.0f;
};

// 頭上タグの候補（ENT-06 tag_candidates）。座標は解析座標
struct TagCandidate {
	float x = 0.0f;
	float y = 0.0f;
	float score = 0.0f;
};

// リザルトの相手のカードの黒い帯（解析座標。PRC-14）
struct ResultBand {
	int top = 0;  // 上端の行
	int rows = 0; // 続いた行の数（太さ）
};

inline bool operator==(const ResultBand &a, const ResultBand &b)
{
	return a.top == b.top && a.rows == b.rows;
}

// コマの解析結果（ENT-06）
struct AnalysisResult {
	int64_t timestamp_ns = 0;
	AnalysisStatus status = AnalysisStatus::kOk;
	ScreenKind screen_kind = ScreenKind::kUncertain;
	bool has_screen_probs = false;       // screennet の確率があるか（使えなかったときは偽）
	std::array<float, 4> screen_probs{}; // other・vs・battle・result
	float brightness = 0.0f;             // 入力テンソルの全要素の平均（0〜1）
	bool screen_by_brightness = false;   // 白いコマの決まりで vs にした
	std::vector<TagCandidate> tags;
	std::vector<ResultBand> result_bands; // リザルトの相手のカードの黒い帯（上から順。PRC-14）
};

// 矩形。左上を含み右下を含まない（画素）
struct RectI {
	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;

	int width() const { return x1 - x0; }
	int height() const { return y1 - y0; }
	bool empty() const { return x1 <= x0 || y1 <= y0; }
	bool operator==(const RectI &other) const
	{
		return x0 == other.x0 && y0 == other.y0 && x1 == other.x1 && y1 == other.y1;
	}
	bool operator!=(const RectI &other) const { return !(*this == other); }
};

// 隠す範囲（ENT-07）。座標は出力するコマの画素
struct MaskRegion {
	RectI rect;
	RegionKind kind = RegionKind::kFixed;
};

} // namespace omask::core
