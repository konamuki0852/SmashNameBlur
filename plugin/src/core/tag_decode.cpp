#include "core/tag_decode.hpp"

#include <cmath>
#include <cstddef>

namespace omask::core {

std::vector<TagCandidate> decode_tag_candidates(const float *out, float threshold)
{
	constexpr size_t kPlane = static_cast<size_t>(kTagGridWidth) * kTagGridHeight;
	const float *logits = out;
	const float *offset_x = out + kPlane;
	const float *offset_y = out + 2 * kPlane;
	// 確からしさの比較はロジットで行う（シグモイドは単調。丸めで別の値が同じにならない）
	const float logit_threshold = std::log(threshold / (1.0f - threshold));
	auto at = [&](int x, int y) {
		return logits[static_cast<size_t>(y) * kTagGridWidth + x];
	};

	std::vector<TagCandidate> candidates;
	for (int y = 0; y < kTagGridHeight; ++y) {
		for (int x = 0; x < kTagGridWidth; ++x) {
			const float z = at(x, y);
			if (z < logit_threshold)
				continue;
			bool peak = true;
			for (int dy = -1; dy <= 1 && peak; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const int nx = x + dx;
					const int ny = y + dy;
					if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= kTagGridWidth ||
					    ny >= kTagGridHeight)
						continue;
					if (at(nx, ny) > z) {
						peak = false;
						break;
					}
				}
			}
			if (!peak)
				continue;
			const size_t index = static_cast<size_t>(y) * kTagGridWidth + x;
			TagCandidate candidate;
			candidate.x = (static_cast<float>(x) + offset_x[index]) * kGridStride;
			candidate.y = (static_cast<float>(y) + offset_y[index]) * kGridStride;
			candidate.score = 1.0f / (1.0f + std::exp(-z));
			candidates.push_back(candidate);
		}
	}
	return candidates;
}

} // namespace omask::core
