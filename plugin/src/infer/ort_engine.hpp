// 推論エンジン（処理詳細設計書 PRC-08）。ONNX Runtime（DirectML 版）を自分のフォルダから明示的に読み込み、
// GPU（DirectML）→ CPU の順に準備する。モデルはメタデータ（ENT-04・05 の omask.*）と入出力の名前・形を確かめる。
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/inference.hpp"
#include "core/status.hpp"

namespace omask::infer {

struct EngineOptions {
	std::wstring runtime_dir;    // onnxruntime.dll の置き場（プラグインの DLL と同じフォルダ）
	std::wstring tagnet_path;    // data/models/tagnet.onnx
	std::wstring screennet_path; // data/models/screennet.onnx
	bool prefer_gpu = true;
	int cpu_threads = 4;      // CPU の並列数（PRC-08）
	int warmup_runs = 3;      // 試しの推論の回数
	uint32_t api_version = 0; // 0 ならビルドに使った版。テストで版の違いを作るときだけ変える
};

// 準備の結果（ログと状態に使う。名前・絶対パスは含めない）
struct EngineReport {
	bool ready = false; // tagnet を使える（使えなければ「検出できない」）
	bool gpu = false;   // DirectML で動いている
	bool screen_ready = false;
	core::UnavailableReason reason = core::UnavailableReason::kNone;
	std::string ort_version;
	std::string tagnet_version;
	std::string screennet_version;
	std::string detail; // 失敗の詳細（ログだけに出す）
};

class OrtEngine final : public core::InferenceBackend {
public:
	OrtEngine();
	~OrtEngine() override;
	OrtEngine(const OrtEngine &) = delete;
	OrtEngine &operator=(const OrtEngine &) = delete;

	EngineReport prepare(const EngineOptions &options);

	bool screen_available() const override;
	bool tag_available() const override;
	bool run_screen(const float *input, std::array<float, 4> &logits) override;
	bool run_tags(const float *input, std::vector<float> &output) override;

	bool using_gpu() const;
	// GPU から CPU に切り替えたことを 1 回だけ返す（状態とログに使う）
	bool take_fallback_event();
	// 実行中に CPU でも失敗して使えなくなった
	bool failed() const;
	// テスト用: 次の GPU での推論を失敗させる（TC-18）
	void inject_gpu_failure_for_test();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace omask::infer
