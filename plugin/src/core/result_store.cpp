#include "core/result_store.hpp"

#include <algorithm>

namespace omask::core {

namespace {

bool earlier(const ResultStore::Entry &entry, int64_t timestamp_ns)
{
	return entry.timestamp_ns < timestamp_ns;
}

} // namespace

std::deque<ResultStore::Entry>::const_iterator ResultStore::lower(int64_t timestamp_ns) const
{
	return std::lower_bound(entries_.begin(), entries_.end(), timestamp_ns, earlier);
}

std::deque<ResultStore::Entry>::iterator ResultStore::find(int64_t timestamp_ns)
{
	auto it = std::lower_bound(entries_.begin(), entries_.end(), timestamp_ns, earlier);
	if (it != entries_.end() && it->timestamp_ns == timestamp_ns)
		return it;
	return entries_.end();
}

void ResultStore::add_frame(int64_t timestamp_ns)
{
	auto it = std::lower_bound(entries_.begin(), entries_.end(), timestamp_ns, earlier);
	if (it != entries_.end() && it->timestamp_ns == timestamp_ns)
		return;
	Entry entry;
	entry.timestamp_ns = timestamp_ns;
	entries_.insert(it, entry);
}

bool ResultStore::put(const AnalysisResult &result)
{
	auto it = find(result.timestamp_ns);
	if (it == entries_.end())
		return false;
	it->result = result;
	it->done = true;
	return true;
}

void ResultStore::mark_not_analyzed(int64_t timestamp_ns)
{
	add_frame(timestamp_ns);
	auto it = find(timestamp_ns);
	it->result = AnalysisResult{};
	it->result.timestamp_ns = timestamp_ns;
	it->result.status = AnalysisStatus::kNotAnalyzed;
	it->done = true;
}

void ResultStore::prune_before(int64_t timestamp_ns)
{
	while (!entries_.empty() && entries_.front().timestamp_ns < timestamp_ns)
		entries_.pop_front();
}

void ResultStore::clear()
{
	entries_.clear();
}

} // namespace omask::core
