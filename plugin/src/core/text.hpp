// 文言の差し込み（言語ファイルの文の {名前} を値に置き換える）。printf の書式を使わず、翻訳の誤りで壊れないようにする。
#pragma once

#include <string>

namespace omask::core {

// text の中の "{name}" をすべて value に置き換える
std::string fill_placeholder(std::string text, const std::string &name, const std::string &value);

} // namespace omask::core
