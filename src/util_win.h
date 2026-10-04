#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace dw {

std::wstring toWide(const std::string& utf8);
std::string toUtf8(const std::wstring& wide);
std::string winErrorText(DWORD code);
// Nummern der physischen Laufwerke, auf denen das geöffnete Volume liegt. Leer bei Fehler.
std::vector<DWORD> volumeDiskNumbers(HANDLE volume);
bool isProcessElevated();

}  // namespace dw
