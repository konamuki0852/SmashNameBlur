// 本フィルター（処理詳細設計書 PRC-01・07・09）。OBS の非同期フィルターとして、コマを遅らせ、
// 解析スレッドの結果から隠す範囲を決めて描き込む。
#pragma once

namespace omask::plugin {

// OBS にフィルターを登録する（PRC-07 手順 1。プラグインの読み込み時に 1 回）
void register_mask_filter();

} // namespace omask::plugin
