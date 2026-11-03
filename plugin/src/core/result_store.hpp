// 結果の置き場（ENT-06 の並び）。コマの時刻の順に、受け取ったコマと解析の結果を持つ。
// スレッドに安全ではない（使う側がロックする）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include "core/types.hpp"

namespace omask::core {

class ResultStore {
public:
	struct Entry {
		int64_t timestamp_ns = 0;
		bool done = false; // 偽: 受け取り済みで解析を待っている
		AnalysisResult result;
	};

	// コマを受け取った（解析を待つ）。同じ時刻がすでにあれば何もしない
	void add_frame(int64_t timestamp_ns);
	// 解析が済んだ。結果の時刻のコマが無ければ（捨てた後など）偽を返して何もしない
	bool put(const AnalysisResult &result);
	// 解析用のバッファが足りず、解析しなかった（「解析なし」）
	void mark_not_analyzed(int64_t timestamp_ns);
	// この時刻より前のものを捨てる
	void prune_before(int64_t timestamp_ns);
	void clear();

	std::size_t size() const { return entries_.size(); }
	bool empty() const { return entries_.empty(); }

	// 時刻が [from_ns, to_ns] のものを時刻の順に渡す
	template<class Fn> void for_each_in(int64_t from_ns, int64_t to_ns, Fn &&fn) const
	{
		for (auto it = lower(from_ns); it != entries_.end() && it->timestamp_ns <= to_ns; ++it)
			fn(*it);
	}

private:
	std::deque<Entry>::const_iterator lower(int64_t timestamp_ns) const;
	std::deque<Entry>::iterator find(int64_t timestamp_ns);

	std::deque<Entry> entries_;
};

} // namespace omask::core
