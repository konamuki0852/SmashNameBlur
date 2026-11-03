// 推論の約束（方式・構成設計書 §5.3: core に約束を置き、infer が実装する）。core は ONNX Runtime を知らない。
#pragma once

#include <array>
#include <vector>

namespace omask::core {

class InferenceBackend {
public:
	virtual ~InferenceBackend() = default;

	virtual bool screen_available() const = 0; // screennet を使えるか
	virtual bool tag_available() const = 0;    // tagnet を使えるか

	// input は前処理済みの [1, 3, 360, 640]。失敗したら偽（実行先の切り替えは実装の側で行う）
	virtual bool run_screen(const float *input, std::array<float, 4> &logits) = 0;
	// output は [3, 45, 80]（中心らしさのロジット・横のずれ・縦のずれ）
	virtual bool run_tags(const float *input, std::vector<float> &output) = 0;
};

} // namespace omask::core
