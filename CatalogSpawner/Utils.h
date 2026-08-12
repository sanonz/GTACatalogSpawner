#pragma once

#include <inc/types.h>

#include <string>

namespace Utils {
Hash Joaat(std::string value);
std::string Trim(std::string value);
std::string ToLower(std::string value);
std::string WideToUtf8(const wchar_t* value, unsigned int length);
std::string GxtText(const std::string& label);
void ShowSubtitle(const std::string& message, int duration = 2500);
std::string HexHash(Hash hash);
}

