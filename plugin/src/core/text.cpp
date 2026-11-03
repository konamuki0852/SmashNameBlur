#include "core/text.hpp"

#include <cstdio>

namespace omask::core {

std::string fill_placeholder(std::string text, const std::string &name, const std::string &value)
{
	const std::string key = "{" + name + "}";
	size_t pos = 0;
	while ((pos = text.find(key, pos)) != std::string::npos) {
		text.replace(pos, key.size(), value);
		pos += value.size();
	}
	return text;
}

} // namespace omask::core
