#include "Utils.h"

#include <Windows.h>
#include <inc/natives.h>

#include <algorithm>
#include <cctype>
#include <format>

Hash Utils::Joaat(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    Hash hash = 0;
    for (const unsigned char character : value) {
        hash += character;
        hash += hash << 10;
        hash ^= hash >> 6;
    }
    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;
    return hash;
}

std::string Utils::Trim(std::string value) {
    const auto isSpace = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

std::string Utils::ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string Utils::WideToUtf8(const wchar_t* value, unsigned int length) {
    if (!value || length == 0)
        return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
        static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        return {};
    std::string result(required, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(length),
        result.data(), required, nullptr, nullptr) <= 0) {
        return {};
    }
    return result;
}

std::string Utils::GxtText(const std::string& label) {
    if (label.empty() || label == "NULL")
        return {};
    const char* localized = HUD::GET_FILENAME_FOR_AUDIO_CONVERSATION(label.c_str());
    if (!localized || std::string(localized) == "NULL")
        return {};
    return localized;
}

void Utils::ShowSubtitle(const std::string& message, int duration) {
    HUD::BEGIN_TEXT_COMMAND_PRINT("CELL_EMAIL_BCON");
    constexpr size_t maxChunkLength = 99;
    size_t start = 0;
    while (start < message.size()) {
        size_t end = std::min(start + maxChunkLength, message.size());
        while (end < message.size() && end > start &&
            (static_cast<unsigned char>(message[end]) & 0xC0) == 0x80) {
            --end;
        }
        if (end == start)
            end = std::min(start + maxChunkLength, message.size());
        const std::string chunk = message.substr(start, end - start);
        HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(chunk.c_str());
        start = end;
    }
    HUD::END_TEXT_COMMAND_PRINT(duration, true);
}

std::string Utils::HexHash(Hash hash) {
    return std::format("0x{:08X}", hash);
}
