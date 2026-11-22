#pragma once

#include <Windows.h>

#include <optional>
#include <string>

namespace TextInput {
void OnKeyboardMessage(DWORD key, WORD repeats, BYTE scanCode,
    BOOL isExtended, BOOL isWithAlt, BOOL wasDownBefore, BOOL isUpNow);

std::optional<std::string> Prompt(const std::string& title,
    const std::string& defaultText = {}, int maxLength = 64);
}
