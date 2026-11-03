#include "plugin/module_paths.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <obs-module.h>

#include <vector>

namespace omask::plugin {

namespace {

std::wstring widen(const char *utf8)
{
	if (utf8 == nullptr || *utf8 == '\0')
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
	if (length <= 0)
		return {};
	std::wstring wide(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide.data(), length);
	wide.resize(static_cast<size_t>(length - 1)); // 終端の 0 を除く
	return wide;
}

} // namespace

std::wstring module_directory()
{
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&module_directory), &module))
		return {};
	std::vector<wchar_t> path(MAX_PATH);
	for (;;) {
		const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0)
			return {};
		if (length < path.size())
			break;
		path.resize(path.size() * 2); // 長いパス（32767 文字まで）
		if (path.size() > 32768)
			return {};
	}
	std::wstring full(path.data());
	const size_t slash = full.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : full.substr(0, slash);
}

std::wstring module_data_file(const char *relative)
{
	char *path = obs_module_file(relative);
	const std::wstring wide = widen(path);
	bfree(path);
	return wide;
}

} // namespace omask::plugin
