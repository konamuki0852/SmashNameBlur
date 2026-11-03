// プラグインの置き場所（処理詳細設計書 PRC-08 手順 2・3）。onnxruntime.dll は自分の DLL と同じフォルダ、
// モデルは OBS が教えるプラグインのデータのフォルダ（data/models/）から読む。
#pragma once

#include <string>

namespace omask::plugin {

// 自分の DLL のあるフォルダ（取れなければ空）
std::wstring module_directory();

// プラグインのデータのフォルダの中のファイル（無ければ空）。relative は "models/tagnet.onnx" のように書く
std::wstring module_data_file(const char *relative);

} // namespace omask::plugin
