// リザルトの相手のカードの名前の帯（処理詳細設計書 PRC-14）。カードは上から入り、下側で止まり、上へスライドして
// 結果の詳細になる。名前は常にキャラ名の黒い帯のすぐ下にあるので、右のカードの列で黒い帯を探して追いかける。
#pragma once

#include <cstdint>
#include <vector>

#include "core/result_store.hpp"
#include "core/types.hpp"

namespace omask::core {

// 黒い帯の見つけ方と、隠す範囲の決め方（ENT-03）。位置は 1080p 換算、時間はミリ秒
struct ResultBandParams {
	int column_x0 = 1040; // 相手のカードの列（名前・「2P」の印が入る横の範囲）
	int column_x1 = 1500;
	float dark_max = 40.0f / 255; // RGB のどれもこれ未満なら黒
	float dark_fraction = 0.5f;   // 列のうち黒の割合がこれ以上の行を帯とみなす
	int min_rows = 3;             // 帯とみなす連続した行の数（解析座標）
	int ignore_below = 1040;      // 上端がこれより下の帯は画面の下の縁の黒（名前ではない）
	int above = 40;               // 帯の上端から上に隠す長さ
	int below = 220;              // 帯の上端から下に隠す長さ（カードが大きく映るスライド中でも名前が入る長さ）
	int margin = 8;               // 列の左右に足す余白
	int hold_ms = 200;            // 見つけた帯を隠し続ける時間
	int look_ahead_ms = 100;      // 後のコマで見つけた帯を前のコマにも当てる時間（遅延 D の先読み以下）
	// 結果詳細（カードが上に止まっている）とみなす帯: 上端がこの間にあり、この行数（解析座標）以上の太さ。
	// そのコマの帯は使わない（名前は決まった範囲が覆う。帯から下を隠すと、右の世界戦闘力まで隠れる）。
	// 入ってくる途中・スライド中の帯は細いので、結果詳細とはみなさない
	int detail_top_min = 100;
	int detail_top_max = 200;
	int detail_min_rows = 7;
};

// 前処理した入力テンソル（[3, 360, 640] の RGB、0〜1）から、黒い帯（解析座標）を上から順に返す
std::vector<ResultBand> find_result_bands(const float *tensor, const ResultBandParams &params = ResultBandParams{});

// 帯の並びが結果詳細（カードが上に止まっている）のものか
bool is_result_detail(const std::vector<ResultBand> &bands, const ResultBandParams &params = ResultBandParams{});

// リザルトの範囲が有効な出力のコマ（時刻 output_ns）について、[output_ns − 保持, output_ns + 先読み] の結果で
// 見つけた帯から、コマの座標の隠す範囲を作る（結果詳細のコマの帯は使わない。重なりはまとめない。
// 描き込みは重なっても同じ）
std::vector<MaskRegion> result_band_regions(const ResultStore &store, int64_t output_ns, const ResultBandParams &params,
					    int width, int height);

} // namespace omask::core
