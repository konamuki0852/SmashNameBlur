// 1 コマの解析（PRC-04 ＋ PRC-02）。偽の推論で、どの場合に頭上タグを推論するか・失敗の扱いを確かめる
#include "doctest/doctest.h"

#include <cmath>

#include "core/analyzer.hpp"
#include "core/tag_decode.hpp"
#include "frame_builder.hpp"

using omask::core::AnalysisResult;
using omask::core::AnalysisStatus;
using omask::core::Analyzer;
using omask::core::InferenceBackend;
using omask::core::PixelFormat;
using omask::core::ScreenKind;
using omask::test::FrameBuilder;

namespace {

class FakeBackend final : public InferenceBackend {
public:
	bool screen_ok = true;
	bool tag_ok = true;
	bool screen_fails = false;
	int top = 2; // battle
	int tag_runs = 0;

	bool screen_available() const override { return screen_ok; }
	bool tag_available() const override { return tag_ok; }
	bool run_screen(const float *, std::array<float, 4> &logits) override
	{
		if (screen_fails)
			return false;
		logits = {-5.0f, -5.0f, -5.0f, -5.0f};
		logits[static_cast<size_t>(top)] = 5.0f;
		return true;
	}
	bool run_tags(const float *, std::vector<float> &output) override
	{
		++tag_runs;
		const size_t plane = static_cast<size_t>(omask::core::kTagGridWidth) * omask::core::kTagGridHeight;
		output.assign(3 * plane, 0.0f);
		for (size_t i = 0; i < plane; ++i)
			output[i] = -10.0f;
		output[20 * omask::core::kTagGridWidth + 40] = 3.0f; // (40, 20) に 1 つ
		return true;
	}
};

AnalysisResult analyze(FakeBackend *backend, int64_t t = 1000)
{
	FrameBuilder builder(PixelFormat::kNv12, 640, 360);
	Analyzer analyzer;
	return analyzer.analyze(builder.view(), t, backend);
}

} // namespace

TEST_CASE("バトルなら頭上タグを推論し、候補を取り出す")
{
	FakeBackend backend;
	const AnalysisResult r = analyze(&backend);
	CHECK(r.status == AnalysisStatus::kOk);
	CHECK(r.screen_kind == ScreenKind::kBattle);
	CHECK(backend.tag_runs == 1);
	REQUIRE(r.tags.size() == 1);
	CHECK(r.tags[0].x == doctest::Approx(40.0 * 8));
}

TEST_CASE("VS 画面・リザルト・その他では頭上タグを推論しない")
{
	for (int top : {0, 1, 3}) {
		FakeBackend backend;
		backend.top = top;
		const AnalysisResult r = analyze(&backend);
		CHECK(backend.tag_runs == 0);
		CHECK(r.tags.empty());
	}
}

TEST_CASE("screennet が使えないときは不確か・確率なしで、頭上タグは推論する")
{
	FakeBackend backend;
	backend.screen_ok = false;
	const AnalysisResult r = analyze(&backend);
	CHECK(r.screen_kind == ScreenKind::kUncertain);
	CHECK_FALSE(r.has_screen_probs);
	CHECK(backend.tag_runs == 1);
	CHECK(r.status == AnalysisStatus::kOk);
}

TEST_CASE("推論に失敗したコマは failed にする")
{
	FakeBackend backend;
	backend.screen_fails = true;
	const AnalysisResult r = analyze(&backend);
	CHECK(r.status == AnalysisStatus::kFailed);
}

TEST_CASE("推論の仕組みが無いときは、不確かで候補なし")
{
	const AnalysisResult r = analyze(nullptr);
	CHECK(r.screen_kind == ScreenKind::kUncertain);
	CHECK(r.tags.empty());
	CHECK(r.status == AnalysisStatus::kOk);
}
