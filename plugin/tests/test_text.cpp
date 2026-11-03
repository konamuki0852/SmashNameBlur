// 文言の差し込み（SCR-01 の状態の文・映像の遅れ。ERR-03〜05・07）。テスト設計書 TC-15
#include "doctest/doctest.h"

#include "core/text.hpp"

using omask::core::fill_placeholder;

TEST_CASE("TC-15 文言の {名前} をすべて置き換え、ほかは変えない")
{
	CHECK(fill_placeholder("約 {delay} ms・{delay} ms", "delay", "133") == "約 133 ms・133 ms");
	CHECK(fill_placeholder("({count})", "reason", "x") == "({count})");
	CHECK(fill_placeholder("%s {count} %d", "count", "5") == "%s 5 %d"); // 書式の記号はそのまま
	CHECK(fill_placeholder("{a}", "a", "{a}") == "{a}");                 // 置き換えた値は読み直さない
}
