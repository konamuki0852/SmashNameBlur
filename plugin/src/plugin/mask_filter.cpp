#include "plugin/mask_filter.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/delay.hpp"
#include "core/delay_buffer.hpp"
#include "core/draw.hpp"
#include "core/mask_planner.hpp"
#include "core/params.hpp"
#include "core/result_store.hpp"
#include "core/settings.hpp"
#include "core/status.hpp"
#include "core/text.hpp"
#include "core/timing.hpp"
#include "plugin/analysis_worker.hpp"
#include "plugin/module_paths.hpp"
#include "plugin/obs_frame.hpp"
#include "plugin/status_text.hpp"

namespace omask::plugin {

namespace {

constexpr const char *kFilterId = "blur_name_filter";
constexpr const char *kPropStatus = "status";
constexpr const char *kPropDelay = "delay_info"; // 表示のためだけの値（保存しない）
constexpr const char *kPropTagGroup = "tag_group";
constexpr const char *kPropMosaicGroup = "mosaic_group";
constexpr const char *kPropUnit = "unit_info";
constexpr const char *kPropResetSuffix = "_reset";
constexpr int64_t kStatsIntervalNs = 60LL * 1000000000; // 所要時間の集計の間隔（PRC-09）
constexpr int64_t kSettleNs = 2LL * 1000000000;         // 頭上タグの調整が落ち着いたとみなす時間（ログを 1 行にする）
constexpr auto kStopTimeout = std::chrono::milliseconds(2000); // 解析スレッドの停止を待つ上限（PRC-07）

double ms_since(uint64_t start_ns)
{
	return static_cast<double>(os_gettime_ns() - start_ns) / 1e6;
}

// 整数の設定を読む。手で書き換えた設定の文字列・真偽値・桁外れの小数は既定値にする
// （obs_data_get_int はこれらを 0 にし、丸めると隠す範囲が最小になる）。読めなかったら invalid を立てる。
// 既定値は OBS に聞かない（型の違う値が入った項目では、OBS の既定値も 0 になる）
int64_t read_int(obs_data_t *data, const char *key, int64_t fallback, bool &invalid)
{
	constexpr double kLimit = 1e9;
	obs_data_item_t *item = obs_data_item_byname(data, key);
	if (item == nullptr)
		return fallback;
	int64_t value = fallback;
	switch (obs_data_item_numtype(item)) {
	case OBS_DATA_NUM_INT:
		value = obs_data_item_get_int(item);
		break;
	case OBS_DATA_NUM_DOUBLE: {
		const double d = obs_data_item_get_double(item);
		if (std::isfinite(d) && std::fabs(d) < kLimit)
			value = static_cast<int64_t>(d);
		else
			invalid = true;
		break;
	}
	default:
		invalid = true;
		break;
	}
	obs_data_item_release(&item);
	return value;
}

struct ReadResult {
	core::Settings settings;
	bool invalid = false;         // 数として読めない値があった（既定値にした）
	bool unknown_version = false; // 知らない版（すべて既定値にした）
};

ReadResult read_settings_checked(obs_data_t *data)
{
	ReadResult result;
	bool &invalid = result.invalid;
	core::RawSettings raw;
	raw.settings_version = read_int(data, core::kKeySettingsVersion, core::kSettingsVersion, invalid);
	raw.policy = obs_data_get_string(data, core::kKeyPolicy);
	raw.debug_overlay = obs_data_get_bool(data, core::kKeyDebugOverlay);
	raw.style = obs_data_get_string(data, core::kKeyStyle);
	raw.mosaic_strength = obs_data_get_string(data, core::kKeyMosaicStrength);
	raw.fill_color = read_int(data, core::kKeyFillColor, core::kFillColorDefault, invalid);
	raw.tag_width_px = read_int(data, core::kKeyTagWidthPx, core::kTagWidthDefaultPx, invalid);
	raw.tag_height_px = read_int(data, core::kKeyTagHeightPx, core::kTagHeightDefaultPx, invalid);
	raw.tag_hold_ms = read_int(data, core::kKeyTagHoldMs, core::kTagHoldDefaultMs, invalid);
	raw.tag_look_ahead_ms = read_int(data, core::kKeyTagLookAheadMs, core::kTagLookAheadDefaultMs, invalid);
	int64_t defaults[3][4] = {};
	core::fixed_rect_fields(core::default_fixed_rects(), defaults);
	raw.has_fixed = true;
	for (int region = 0; region < 3; ++region)
		for (int field = 0; field < 4; ++field)
			raw.fixed[region][field] = read_int(data, core::fixed_key(region, field).c_str(),
							    defaults[region][field], invalid);
	result.unknown_version = raw.settings_version != core::kSettingsVersion;
	result.settings = core::normalize_settings(raw);
	return result;
}

core::Settings read_settings(obs_data_t *data)
{
	return read_settings_checked(data).settings;
}

struct Slider {
	const char *key;
	const char *label;
	const char *tooltip;
	int min;
	int max;
	int step;
	int default_value;
};

void add_slider(obs_properties_t *group, const Slider &slider)
{
	obs_property_t *p = obs_properties_add_int_slider(group, slider.key, obs_module_text(slider.label), slider.min,
							  slider.max, slider.step);
	const std::string tooltip = core::fill_placeholder(obs_module_text(slider.tooltip), "default",
							   std::to_string(slider.default_value));
	obs_property_set_long_description(p, tooltip.c_str());
}

// 「既定値に戻す」で戻すグループ（0: 頭上タグ、1〜3: 位置の決まった範囲の vs・hud・result、4: モザイクの強さ）
constexpr int kResetGroupCount = 5;
constexpr int kResetMosaic = 4;
const char *const kResetGroupNames[kResetGroupCount] = {"tag", "vs", "hud", "result", "mosaic"};

struct ResetTarget {
	obs_source_t *source = nullptr;
	int filter_id = 0;
	int group = 0;
};

std::vector<std::string> reset_keys(int group)
{
	if (group == 0)
		return {core::kKeyTagWidthPx, core::kKeyTagHeightPx, core::kKeyTagLookAheadMs, core::kKeyTagHoldMs};
	if (group == kResetMosaic)
		return {core::kKeyMosaicStrength};
	std::vector<std::string> keys;
	for (int field = 0; field < 4; ++field)
		keys.push_back(core::fixed_key(group - 1, field));
	return keys;
}

// I16・I24・I30・I36・I37: そのグループの設定を既定値に戻して設定画面を作り直す（PRC-07 手順 5）
bool on_reset_clicked(obs_properties_t *, obs_property_t *, void *priv)
{
	try {
		const auto *target = static_cast<const ResetTarget *>(priv);
		obs_data_t *settings = obs_source_get_settings(target->source);
		for (const std::string &key : reset_keys(target->group))
			obs_data_unset_user_value(settings, key.c_str());
		obs_data_release(settings);
		obs_source_update(target->source, nullptr);
		obs_log(LOG_INFO, "filter #%d settings: %s reset to defaults", target->filter_id,
			kResetGroupNames[target->group]);
		return true;
	} catch (...) {
		obs_log(LOG_ERROR, "settings could not be reset");
	}
	return false;
}

// グループの末尾に「既定値に戻す」を置く（設定を持たない呼び出しでは置かない）
void add_reset_button(obs_properties_t *group, const ResetTarget *target)
{
	if (target == nullptr)
		return;
	const std::string key = std::string(kResetGroupNames[target->group]) + kPropResetSuffix;
	obs_properties_add_button2(group, key.c_str(), obs_module_text("ResetGroup"), on_reset_clicked,
				   const_cast<ResetTarget *>(target));
}

// I12〜I17 頭上タグの隠し方（REQ-14・FN-17）。並びは横幅・高さ・隠し始める・隠し続ける・既定値に戻す（I16）
void add_tag_properties(obs_properties_t *props, const ResetTarget *reset)
{
	obs_properties_t *group = obs_properties_create();
	add_slider(group, {core::kKeyTagWidthPx, "TagWidth", "TagWidth.Tooltip", core::kTagWidthMinPx,
			   core::kTagWidthMaxPx, 1, core::kTagWidthDefaultPx});
	add_slider(group, {core::kKeyTagHeightPx, "TagHeight", "TagHeight.Tooltip", core::kTagHeightMinPx,
			   core::kTagHeightMaxPx, 1, core::kTagHeightDefaultPx});
	add_slider(group, {core::kKeyTagLookAheadMs, "TagLookAhead", "TagLookAhead.Tooltip", core::kTagLookAheadMinMs,
			   core::kTagLookAheadMaxMs, 10, core::kTagLookAheadDefaultMs});
	add_slider(group, {core::kKeyTagHoldMs, "TagHold", "TagHold.Tooltip", core::kTagHoldMinMs, core::kTagHoldMaxMs,
			   50, core::kTagHoldDefaultMs});
	add_reset_button(group, reset);
	obs_properties_add_group(props, kPropTagGroup, obs_module_text("TagGroup"), OBS_GROUP_NORMAL, group);
}

// 位置の決まった範囲の既定値（x・y・幅・高さ。[vs・hud・result][x・y・w・h]）
struct FixedDefaults {
	int64_t value[3][4] = {};
	FixedDefaults() { core::fixed_rect_fields(core::default_fixed_rects(), value); }
};

const FixedDefaults &fixed_defaults()
{
	static const FixedDefaults defaults;
	return defaults;
}

const char *const kFixedGroupLabels[3] = {"FixedVsGroup", "FixedHudGroup", "FixedResultGroup"};
const char *const kFixedGroupTooltips[3] = {"FixedVs.Tooltip", "FixedHud.Tooltip", "FixedResult.Tooltip"};
const char *const kFixedFieldLabels[4] = {"FixedX", "FixedY", "FixedW", "FixedH"};

std::string fixed_group_key(int region)
{
	return std::string("fixed_") + core::kFixedRegionNames[region] + "_group";
}

// I19〜I36 位置の決まった範囲の 1 つ（REQ-15・FN-18。region は vs・hud・result）。左端・上端・横幅・高さ
// （1080p 換算）と既定値に戻す
void add_fixed_group(obs_properties_t *props, int region, const ResetTarget *reset)
{
	constexpr int kMax[4] = {core::kReferenceWidth, core::kReferenceHeight, core::kReferenceWidth,
				 core::kReferenceHeight};
	obs_properties_t *group = obs_properties_create();
	for (int field = 0; field < 4; ++field) {
		const std::string key = core::fixed_key(region, field);
		obs_property_t *p = obs_properties_add_int_slider(
			group, key.c_str(), obs_module_text(kFixedFieldLabels[field]), 0, kMax[field], 1);
		const std::string tooltip =
			core::fill_placeholder(obs_module_text(kFixedGroupTooltips[region]), "default",
					       std::to_string(fixed_defaults().value[region][field]));
		obs_property_set_long_description(p, tooltip.c_str());
	}
	add_reset_button(group, reset);
	obs_properties_add_group(props, fixed_group_key(region).c_str(), obs_module_text(kFixedGroupLabels[region]),
				 OBS_GROUP_NORMAL, group);
}

// I08・I10・I37 モザイクの強さ（REQ-13・FN-10。弱い・普通・強いのラジオボタン。px の値は出さない）
void add_mosaic_properties(obs_properties_t *props, const ResetTarget *reset)
{
	obs_properties_t *group = obs_properties_create();
	obs_property_t *p = obs_properties_add_list(group, core::kKeyMosaicStrength, obs_module_text("MosaicStrength"),
						    OBS_COMBO_TYPE_RADIO, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("MosaicWeak"), core::kMosaicWeak);
	obs_property_list_add_string(p, obs_module_text("MosaicNormal"), core::kMosaicNormal);
	obs_property_list_add_string(p, obs_module_text("MosaicStrong"), core::kMosaicStrong);
	obs_property_set_long_description(p, obs_module_text("MosaicStrength.Tooltip"));
	add_reset_button(group, reset);
	obs_properties_add_group(props, kPropMosaicGroup, obs_module_text("MosaicGroup"), OBS_GROUP_NORMAL, group);
}

// 設定のグループを並べる（SCR-01。モザイクの強さ → VS 画面 → 頭上タグ → HUD → リザルト）。
// reset は kResetGroupCount 個の並び（設定を持たない呼び出しでは nullptr）
void add_groups(obs_properties_t *props, const ResetTarget *reset)
{
	const auto target = [&](int group) {
		return reset == nullptr ? nullptr : &reset[group];
	};
	add_mosaic_properties(props, target(kResetMosaic));
	add_fixed_group(props, 0, target(1));
	add_tag_properties(props, target(0));
	add_fixed_group(props, 1, target(2));
	add_fixed_group(props, 2, target(3));
}

int next_filter_id()
{
	static std::atomic<int> next{1};
	return next.fetch_add(1);
}

class MaskFilter final : public AnalysisListener {
public:
	MaskFilter(obs_source_t *context, obs_data_t *settings);
	~MaskFilter() override;
	MaskFilter(const MaskFilter &) = delete;
	MaskFilter &operator=(const MaskFilter &) = delete;

	void update(obs_data_t *settings);
	obs_source_frame *filter_video(obs_source_frame *frame);
	void remove(obs_source_t *parent);
	obs_properties_t *properties();

	void on_engine_report(const infer::EngineReport &report, double prepare_ms) override;
	void on_gpu_fallback() override;
	void on_engine_failed() override;
	void on_result(uint64_t generation, const core::AnalysisResult &result) override;

private:
	std::shared_ptr<const core::Settings> settings() const;
	void release_buffered(obs_source_t *parent);
	void restart();
	void on_unsupported(const obs_source_frame &frame, obs_source_t *parent, int64_t now);
	void on_supported(const obs_source_frame &frame, int64_t now);
	void submit(const obs_source_frame &frame, int64_t timestamp_ns);
	void render(obs_source_frame &frame, int64_t now);
	void tick(int64_t now);
	void note_tag_adjust(int64_t now);
	void note_state_locked(int64_t now); // status_mutex_ を持って呼ぶ

	obs_source_t *context_;
	const int id_; // ログで同じソースに付いた複数のフィルターを見分ける番号（1 から）
	bool warned_invalid_ = false;
	std::array<ResetTarget, kResetGroupCount> reset_targets_; // 「既定値に戻す」の押し先（I16・I24・I30・I36・I37）
	bool warned_version_ = false;

	mutable std::mutex settings_mutex_;
	std::shared_ptr<const core::Settings> settings_;

	// 描画スレッドだけが触る（外す・破棄のときは OBS がもう filter_video を呼ばない）
	core::DelayBuffer<obs_source_frame *> buffer_;
	core::DelayEstimator delay_;
	core::MaskPlanner planner_;
	core::TimingWindow render_timing_;
	core::TimingWindow copy_timing_; // 解析用のバッファへの画素の写し
	core::TimingWindow draw_timing_;
	bool unsupported_ = false;
	bool has_format_ = false;
	enum video_format last_format_ = VIDEO_FORMAT_NONE;
	uint32_t last_width_ = 0;
	uint32_t last_height_ = 0;
	bool warned_aspect_ = false;
	bool tags_ready_ = false;
	int64_t tags_ready_since_ns_ = 0;
	int not_analyzed_ = 0;
	int64_t next_stats_ns_ = 0;
	core::TagAdjust logged_tag_;  // ログに出した頭上タグの調整
	core::TagAdjust pending_tag_; // 最後に見た頭上タグの調整
	int64_t tag_changed_ns_ = 0;

	// 描画スレッドと解析スレッドで共有する結果の置き場
	std::mutex store_mutex_;
	core::ResultStore store_;
	std::atomic<uint64_t> generation_{0}; // 時刻の飛びで増やす（古い結果を捨てる）

	// 状態（描画スレッド・解析スレッド・UI のスレッドから）
	std::mutex status_mutex_;
	core::StatusMonitor status_;
	core::LogLimiter log_limiter_;
	bool has_logged_state_ = false;
	core::EngineState logged_state_ = core::EngineState::kPreparing;
	std::string unsupported_format_;
	std::atomic<int64_t> delay_ns_;
	std::atomic<int64_t> interval_ns_;

	std::unique_ptr<AnalysisWorker> worker_;
};

MaskFilter::MaskFilter(obs_source_t *context, obs_data_t *settings)
	: context_(context),
	  id_(next_filter_id()),
	  delay_ns_(core::DelayEstimator{}.delay_ns(core::delay_look_ahead_ms(), core::kAnalysisMarginFrames)),
	  interval_ns_(core::DelayEstimator::kDefaultIntervalNs)
{
	for (int group = 0; group < kResetGroupCount; ++group)
		reset_targets_[group] = ResetTarget{context, id_, group};
	update(settings);
	infer::EngineOptions options;
	options.runtime_dir = module_directory();
	options.tagnet_path = module_data_file("models/tagnet.onnx");
	options.screennet_path = module_data_file("models/screennet.onnx");
	logged_tag_ = pending_tag_ = MaskFilter::settings()->tag;
	obs_log(LOG_INFO, "filter #%d created: version=%s delay=%lld ms tag=%dx%d px ahead=%d ms hold=%d ms", id_,
		PLUGIN_VERSION, static_cast<long long>(delay_ns_.load() / core::kNsPerMs), logged_tag_.width_px,
		logged_tag_.height_px, logged_tag_.look_ahead_ms, logged_tag_.hold_ms);

	const auto now = static_cast<int64_t>(os_gettime_ns());
	worker_ = std::make_unique<AnalysisWorker>(options, this);
	std::lock_guard<std::mutex> lock(status_mutex_);
	if (!worker_->start()) {
		// 解析スレッドを起こせない: 位置の決まった範囲だけで動く（PRC-07 例外）
		status_.on_engine_failed(core::UnavailableReason::kInferenceFailed);
		obs_log(LOG_ERROR, "analysis thread could not be started");
	}
	note_state_locked(now);
}

MaskFilter::~MaskFilter()
{
	const uint64_t start = os_gettime_ns();
	if (worker_ && !worker_->stop(kStopTimeout))
		obs_log(LOG_WARNING, "analysis thread did not stop within %lld ms; continuing",
			static_cast<long long>(kStopTimeout.count())); // ERR-10
	release_buffered(obs_filter_get_parent(context_));
	obs_log(LOG_INFO, "filter #%d destroyed: stop=%.0f ms", id_, ms_since(start));
}

void MaskFilter::update(obs_data_t *data)
{
	const ReadResult read = read_settings_checked(data);
	// 手で書き換えた設定の誤り（ERR-14）。同じ誤りは 1 回だけ書く
	if (read.unknown_version && !warned_version_) {
		warned_version_ = true;
		obs_log(LOG_WARNING, "filter #%d settings: unknown settings_version; all settings use the defaults",
			id_);
	}
	if (read.invalid && !warned_invalid_) {
		warned_invalid_ = true;
		obs_log(LOG_WARNING, "filter #%d settings: a value is not a number; the default is used for it", id_);
	}
	auto next = std::make_shared<const core::Settings>(read.settings);
	std::lock_guard<std::mutex> lock(settings_mutex_);
	settings_ = std::move(next);
}

std::shared_ptr<const core::Settings> MaskFilter::settings() const
{
	std::lock_guard<std::mutex> lock(settings_mutex_);
	return settings_;
}

void MaskFilter::release_buffered(obs_source_t *parent)
{
	if (buffer_.size() == 0)
		return;
	if (parent == nullptr) {
		// 外したときにすでに返しているはず。持ち主が分からないコマは解放しない（二重に解放しない）
		obs_log(LOG_WARNING, "%zu buffered frames had no parent source", buffer_.size());
		buffer_.release_all([](obs_source_frame *) {});
		return;
	}
	buffer_.release_all([parent](obs_source_frame *f) { obs_source_release_frame(parent, f); });
}

// 時刻の飛び・形式の変化: 結果の置き場と決まりの状態を空にしてやり直す（PRC-01 手順 2）
void MaskFilter::restart()
{
	{
		std::lock_guard<std::mutex> lock(store_mutex_);
		store_.clear();
		generation_.fetch_add(1);
	}
	planner_.reset();
	tags_ready_ = false;
}

obs_source_frame *MaskFilter::filter_video(obs_source_frame *frame)
{
	obs_source_t *parent = obs_filter_get_parent(context_);
	bool pushed = false;
	obs_source_frame *output = nullptr;
	try {
		const uint64_t start = os_gettime_ns();
		const auto now = static_cast<int64_t>(start);
		if (!core::is_supported(to_pixel_format(frame->format))) {
			on_unsupported(*frame, parent, now);
			tick(now);
			return frame; // 加工せずに返す（ERR-05）
		}
		on_supported(*frame, now);

		const auto timestamp = static_cast<int64_t>(frame->timestamp);
		delay_.add_timestamp(timestamp);
		const int64_t delay = delay_.delay_ns(core::delay_look_ahead_ms(), core::kAnalysisMarginFrames);
		delay_ns_ = delay;
		interval_ns_ = delay_.frame_interval_ns();

		auto pushed_result = buffer_.push(frame, timestamp, delay, [parent](obs_source_frame *f) {
			obs_source_release_frame(parent, f);
		});
		pushed = true;
		if (pushed_result.time_jump)
			restart();
		submit(*frame, timestamp);
		if (pushed_result.output) {
			output = *pushed_result.output;
			render(*output, now);
		}
		render_timing_.add(ms_since(start));
		tick(now);
		obs_source_frame *result = output;
		output = nullptr;
		return result;
	} catch (...) {
		// 隠し損ねたコマを出さない: 返さずに OBS へ戻す。OBS は前のコマを表示し続ける（PRC-01 例外・ERR-09）
		{
			std::lock_guard<std::mutex> lock(status_mutex_);
			if (log_limiter_.allow("error:filter_video", static_cast<int64_t>(os_gettime_ns())))
				obs_log(LOG_ERROR, "unexpected error while processing a frame; the frame was dropped");
		}
		if (output != nullptr)
			obs_source_release_frame(parent, output);
		else if (!pushed)
			obs_source_release_frame(parent, frame);
		return nullptr;
	}
}

void MaskFilter::on_unsupported(const obs_source_frame &frame, obs_source_t *parent, int64_t now)
{
	if (buffer_.size() > 0) {
		release_buffered(parent);
		restart();
	}
	if (unsupported_ && frame.format == last_format_)
		return;
	unsupported_ = true;
	last_format_ = frame.format;
	has_format_ = false;
	std::lock_guard<std::mutex> lock(status_mutex_);
	unsupported_format_ = format_name(frame.format);
	status_.on_format(false);
	note_state_locked(now);
}

void MaskFilter::on_supported(const obs_source_frame &frame, int64_t now)
{
	if (unsupported_) {
		unsupported_ = false;
		std::lock_guard<std::mutex> lock(status_mutex_);
		status_.on_format(true);
		note_state_locked(now);
	}
	if (has_format_ && frame.format == last_format_ && frame.width == last_width_ && frame.height == last_height_)
		return;
	has_format_ = true;
	last_format_ = frame.format;
	last_width_ = frame.width;
	last_height_ = frame.height;
	obs_log(LOG_INFO, "frame: format=%s size=%ux%u flip=%d", format_name(frame.format), frame.width, frame.height,
		frame.flip ? 1 : 0);
	if (!warned_aspect_ && static_cast<uint64_t>(frame.width) * 9 != static_cast<uint64_t>(frame.height) * 16) {
		warned_aspect_ = true; // ERR-12（最初に 1 回）
		obs_log(LOG_WARNING, "frame is not 16:9 (%ux%u); the fixed regions may not match", frame.width,
			frame.height);
	}
}

// 解析ジョブを出す（PRC-01 手順 5）
void MaskFilter::submit(const obs_source_frame &frame, int64_t timestamp_ns)
{
	{
		std::lock_guard<std::mutex> lock(store_mutex_);
		store_.add_frame(timestamp_ns);
	}
	if (!worker_->tag_available()) {
		// 準備中・検出できない: 解析しない（位置の決まった範囲はすべて隠す）
		std::lock_guard<std::mutex> lock(store_mutex_);
		store_.mark_not_analyzed(timestamp_ns);
		return;
	}
	if (!tags_ready_) {
		// このコマから解析する。これより前のコマを出すときは頭上タグの推論が無い扱いにする（準備が済んだ直後を遅れと数えない）
		tags_ready_ = true;
		tags_ready_since_ns_ = timestamp_ns;
	}
	const uint64_t copy_start = os_gettime_ns();
	const bool submitted = worker_->submit(to_raw_view(frame), frame.flip, timestamp_ns, generation_.load());
	copy_timing_.add(ms_since(copy_start));
	if (!submitted) {
		std::lock_guard<std::mutex> lock(store_mutex_);
		store_.mark_not_analyzed(timestamp_ns); // ERR-11（60 秒ごとの集計に入れる）
		++not_analyzed_;
	}
}

// 出力するコマの隠す範囲を決めて描き込む（PRC-01 手順 8・9）
void MaskFilter::render(obs_source_frame &frame, int64_t now)
{
	const std::shared_ptr<const core::Settings> current = settings();
	const auto timestamp = static_cast<int64_t>(frame.timestamp);
	const bool tag_ok = worker_->tag_available() && tags_ready_ && timestamp >= tags_ready_since_ns_;
	const bool screen_ok = worker_->screen_available();
	core::MaskPlan plan;
	{
		std::lock_guard<std::mutex> lock(store_mutex_);
		plan = planner_.plan(store_, timestamp, current->fixed,
				     core::apply_tag_adjust(core::tag_params_for(current->policy), current->tag),
				     screen_ok, tag_ok, static_cast<int>(frame.width), static_cast<int>(frame.height));
		store_.prune_before(timestamp - core::result_keep_ms() * core::kNsPerMs);
	}
	if (plan.late) {
		std::lock_guard<std::mutex> lock(status_mutex_);
		status_.on_late(now);
		note_state_locked(now);
	}
	const uint64_t draw_start = os_gettime_ns();
	core::FrameView view = to_frame_view(frame);
	core::draw_regions(view, plan.regions, current->draw);
	draw_timing_.add(ms_since(draw_start));
}

// 頭上タグの調整が変わったら、落ち着いてから 1 行だけログに出す（スライダーを動かしている間は出さない）
void MaskFilter::note_tag_adjust(int64_t now)
{
	const core::TagAdjust tag = settings()->tag;
	if (tag != pending_tag_) {
		pending_tag_ = tag;
		tag_changed_ns_ = now;
		return;
	}
	if (tag == logged_tag_ || now - tag_changed_ns_ < kSettleNs)
		return;
	logged_tag_ = tag;
	obs_log(LOG_INFO, "filter #%d settings: tag=%dx%d px ahead=%d ms hold=%d ms", id_, tag.width_px, tag.height_px,
		tag.look_ahead_ms, tag.hold_ms);
}

void MaskFilter::tick(int64_t now)
{
	{
		std::lock_guard<std::mutex> lock(status_mutex_);
		status_.on_tick(now);
		note_state_locked(now);
	}
	note_tag_adjust(now);
	if (now < next_stats_ns_)
		return;
	if (next_stats_ns_ != 0) {
		// 60 秒ごとの集計（デバッグの水準。OBS を --verbose で起動したときだけ書かれる。NFR-03 の測定に使う）
		const core::TimingSummary render = render_timing_.take();
		const core::TimingSummary copy = copy_timing_.take();
		const core::TimingSummary draw = draw_timing_.take();
		const AnalysisTiming analysis = worker_->take_timing();
		int late = 0;
		{
			std::lock_guard<std::mutex> lock(status_mutex_);
			late = status_.late_count();
		}
		obs_log(LOG_DEBUG,
			"stats: render avg=%.2f p99=%.2f max=%.2f ms (n=%zu; copy avg=%.2f draw avg=%.2f) "
			"analysis avg=%.2f p99=%.2f max=%.2f ms (n=%zu; preprocess avg=%.2f inference avg=%.2f) "
			"late=%d not_analyzed=%d",
			render.average_ms, render.p99_ms, render.max_ms, render.count, copy.average_ms, draw.average_ms,
			analysis.total.average_ms, analysis.total.p99_ms, analysis.total.max_ms, analysis.total.count,
			analysis.preprocess.average_ms, analysis.inference.average_ms, late, not_analyzed_);
		not_analyzed_ = 0;
	}
	next_stats_ns_ = now + kStatsIntervalNs;
}

// 状態が変わったらログに出す（同じ内容は 60 秒に 1 回まで。PRC-09 手順 3）
void MaskFilter::note_state_locked(int64_t now)
{
	const core::EngineState state = status_.state();
	if (has_logged_state_ && state == logged_state_)
		return;
	has_logged_state_ = true;
	logged_state_ = state;
	if (!log_limiter_.allow(std::string("state:") + state_name(state), now))
		return;
	switch (state) {
	case core::EngineState::kPreparing:
	case core::EngineState::kRunningGpu:
		obs_log(LOG_INFO, "filter #%d state: %s", id_, state_name(state));
		break;
	case core::EngineState::kRunningCpu:
		obs_log(LOG_WARNING, "filter #%d state: %s", id_, state_name(state)); // ERR-02
		break;
	case core::EngineState::kLagging:
		obs_log(LOG_WARNING, "filter #%d state: %s (late=%d in 60 s)", id_, state_name(state),
			status_.late_count()); // ERR-03
		break;
	case core::EngineState::kUnavailable:
		obs_log(LOG_ERROR, "filter #%d state: %s (reason=%s)", id_, state_name(state),
			reason_name(status_.reason())); // ERR-04
		break;
	case core::EngineState::kUnsupportedFormat:
		obs_log(LOG_ERROR, "filter #%d state: %s (format=%s)", id_, state_name(state),
			unsupported_format_.c_str()); // ERR-05
		break;
	}
}

void MaskFilter::remove(obs_source_t *parent)
{
	release_buffered(parent);
	restart();
	delay_.reset();
}

obs_properties_t *MaskFilter::properties()
{
	const auto now = static_cast<int64_t>(os_gettime_ns());
	StatusText status;
	bool abnormal = false;
	{
		std::lock_guard<std::mutex> lock(status_mutex_);
		status_.on_tick(now);
		const core::EngineState state = status_.state();
		abnormal = state != core::EngineState::kPreparing && state != core::EngineState::kRunningGpu;
		status = status_text(state, status_.reason(), status_.late_count(), unsupported_format_);
	}
	obs_properties_t *props = obs_properties_create();

	// I01 状態（異常のときだけ。案内文で、種別で色が変わる。ERR-02〜05）
	if (abnormal) {
		obs_property_t *status_prop =
			obs_properties_add_text(props, kPropStatus, obs_module_text("Status"), OBS_TEXT_INFO);
		obs_property_set_long_description(status_prop, status.text.c_str());
		obs_property_text_set_info_type(status_prop, status.type);
		obs_property_text_set_info_word_wrap(status_prop, true);
	}

	// I03 遅延時間（値は表示のためだけに設定へ置き、保存の前に消す。「？」の吹き出しは付けない）
	const int64_t delay = delay_ns_.load();
	obs_data_t *data = obs_source_get_settings(context_);
	obs_data_set_string(data, kPropDelay, delay_text(delay, interval_ns_.load()).c_str());
	obs_properties_add_text(props, kPropDelay, obs_module_text("Delay"), OBS_TEXT_INFO);

	// I18 パラメータのpx単位（範囲の px は 1080p 基準。代表で 1 つだけ出す）
	obs_property_t *p = obs_properties_add_text(props, kPropUnit, obs_module_text("Unit"), OBS_TEXT_INFO);
	obs_property_set_long_description(p, obs_module_text("Unit.Text"));
	obs_property_text_set_info_word_wrap(p, true);

	add_groups(props, reset_targets_.data());
	obs_data_release(data);
	return props;
}

void MaskFilter::on_engine_report(const infer::EngineReport &report, double prepare_ms)
{
	if (report.ready) {
		obs_log(LOG_INFO, "inference ready: provider=%s ort=%s tagnet=%s screennet=%s prepare=%.0f ms",
			report.gpu ? "DML" : "CPU", report.ort_version.c_str(), report.tagnet_version.c_str(),
			report.screen_ready ? report.screennet_version.c_str() : "unavailable", prepare_ms);
		if (!report.gpu && !report.detail.empty())
			obs_log(LOG_WARNING, "inference: GPU could not be used: %s", report.detail.c_str());
		if (!report.screen_ready)
			obs_log(LOG_WARNING, "screen model unavailable (%s); the fixed regions are always masked",
				report.detail.c_str());
	} else {
		obs_log(LOG_ERROR, "inference unavailable: reason=%s ort=%s detail=%s", reason_name(report.reason),
			report.ort_version.empty() ? "-" : report.ort_version.c_str(), report.detail.c_str());
	}
	std::lock_guard<std::mutex> lock(status_mutex_);
	if (report.ready)
		status_.on_engine_ready(report.gpu);
	else
		status_.on_engine_failed(report.reason == core::UnavailableReason::kNone
						 ? core::UnavailableReason::kInferenceFailed
						 : report.reason);
	note_state_locked(static_cast<int64_t>(os_gettime_ns()));
}

void MaskFilter::on_gpu_fallback()
{
	obs_log(LOG_WARNING, "inference: GPU failed during inference; switched to CPU");
	std::lock_guard<std::mutex> lock(status_mutex_);
	status_.on_gpu_fallback();
	note_state_locked(static_cast<int64_t>(os_gettime_ns()));
}

void MaskFilter::on_engine_failed()
{
	obs_log(LOG_ERROR, "inference failed on CPU as well; overhead tags are no longer detected");
	std::lock_guard<std::mutex> lock(status_mutex_);
	status_.on_engine_failed(core::UnavailableReason::kInferenceFailed);
	note_state_locked(static_cast<int64_t>(os_gettime_ns()));
}

void MaskFilter::on_result(uint64_t generation, const core::AnalysisResult &result)
{
	std::lock_guard<std::mutex> lock(store_mutex_);
	if (generation == generation_.load())
		store_.put(result);
}

// ---- OBS のコールバック（例外を OBS の外に出さない） ----

const char *filter_name(void *)
{
	return obs_module_text("FilterName");
}

void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	try {
		return new MaskFilter(source, settings);
	} catch (...) {
		obs_log(LOG_ERROR, "filter could not be created");
		return nullptr;
	}
}

void filter_destroy(void *data)
{
	try {
		delete static_cast<MaskFilter *>(data);
	} catch (...) {
		obs_log(LOG_ERROR, "error while destroying the filter");
	}
}

void filter_update(void *data, obs_data_t *settings)
{
	try {
		static_cast<MaskFilter *>(data)->update(settings);
	} catch (...) {
		obs_log(LOG_ERROR, "settings could not be applied");
	}
}

void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, core::kKeySettingsVersion, core::kSettingsVersion);
	obs_data_set_default_string(settings, core::kKeyPolicy, core::kPolicyLeakFirst);
	obs_data_set_default_bool(settings, core::kKeyDebugOverlay, false);
	obs_data_set_default_string(settings, core::kKeyStyle, core::kStyleMosaic);
	obs_data_set_default_string(settings, core::kKeyMosaicStrength, core::kMosaicNormal);
	obs_data_set_default_int(settings, core::kKeyFillColor, core::kFillColorDefault);
	obs_data_set_default_int(settings, core::kKeyTagWidthPx, core::kTagWidthDefaultPx);
	obs_data_set_default_int(settings, core::kKeyTagHeightPx, core::kTagHeightDefaultPx);
	obs_data_set_default_int(settings, core::kKeyTagHoldMs, core::kTagHoldDefaultMs);
	obs_data_set_default_int(settings, core::kKeyTagLookAheadMs, core::kTagLookAheadDefaultMs);
	for (int region = 0; region < 3; ++region)
		for (int field = 0; field < 4; ++field)
			obs_data_set_default_int(settings, core::fixed_key(region, field).c_str(),
						 fixed_defaults().value[region][field]);
}

obs_properties_t *filter_properties(void *data)
{
	try {
		if (data != nullptr)
			return static_cast<MaskFilter *>(data)->properties();
	} catch (...) {
		obs_log(LOG_ERROR, "properties could not be created");
	}
	obs_properties_t *props = obs_properties_create();
	try {
		// 設定を持たない呼び出しでも項目はそろえる（状態・遅延時間・単位の行・既定値に戻すは出さない）
		add_groups(props, nullptr);
	} catch (...) {
		obs_log(LOG_ERROR, "properties could not be created");
	}
	return props;
}

obs_source_frame *filter_video(void *data, obs_source_frame *frame)
{
	return static_cast<MaskFilter *>(data)->filter_video(frame); // 例外は中で捕まえる
}

void filter_remove(void *data, obs_source_t *parent)
{
	try {
		static_cast<MaskFilter *>(data)->remove(parent);
	} catch (...) {
		obs_log(LOG_ERROR, "error while removing the filter");
	}
}

void filter_save(void *, obs_data_t *settings)
{
	obs_data_erase(settings, kPropDelay);
}

} // namespace

void register_mask_filter()
{
	obs_source_info info = {};
	info.id = kFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC;
	info.get_name = filter_name;
	info.create = filter_create;
	info.destroy = filter_destroy;
	info.update = filter_update;
	info.get_defaults = filter_defaults;
	info.get_properties = filter_properties;
	info.filter_video = filter_video;
	info.filter_remove = filter_remove;
	info.save = filter_save;
	obs_register_source(&info);
}

} // namespace omask::plugin
