#include "ImageCache.h"

#include "Logger.h"
#include "Utils.h"

#include <inc/main.h>
#include <jpegsize.h>

#include <Windows.h>

#include <cstdio>
#include <format>
#include <fstream>
#include <string_view>

ImageCache gImages;

namespace {
std::optional<std::pair<unsigned int, unsigned int>> PngDimensions(
    const std::filesystem::path& path) {
    constexpr uint64_t pngSignature = 0x89504E470D0A1A0A;
    std::ifstream image(path, std::ios::binary);
    if (!image)
        return {};
    uint64_t signature = 0;
    image.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    if (_byteswap_uint64(signature) != pngSignature)
        return {};
    uint32_t width = 0;
    uint32_t height = 0;
    image.seekg(16);
    image.read(reinterpret_cast<char*>(&width), sizeof(width));
    image.read(reinterpret_cast<char*>(&height), sizeof(height));
    if (!image)
        return {};
    return std::pair{ _byteswap_ulong(width), _byteswap_ulong(height) };
}

std::optional<std::pair<unsigned int, unsigned int>> JpegDimensions(
    const std::filesystem::path& path) {
    FILE* image = nullptr;
    if (_wfopen_s(&image, path.c_str(), L"rb") != 0 || !image)
        return {};
    int width = 0;
    int height = 0;
    const int result = scanhead(image, &width, &height);
    fclose(image);
    if (result != 1 || width <= 0 || height <= 0)
        return {};
    return std::pair{ static_cast<unsigned int>(width), static_cast<unsigned int>(height) };
}

std::optional<std::pair<unsigned int, unsigned int>> WebpDimensions(
    const std::filesystem::path& path) {
    std::ifstream image(path, std::ios::binary);
    if (!image)
        return {};
    char header[30]{};
    image.read(header, sizeof(header));
    if (image.gcount() < 21 || std::string_view(header, 4) != "RIFF" ||
        std::string_view(header + 8, 4) != "WEBP") {
        return {};
    }
    const std::string_view fourCc(header + 12, 4);
    if (fourCc == "VP8 " && image.gcount() >= 30) {
        const auto width = static_cast<unsigned int>(
            static_cast<unsigned char>(header[26]) | (static_cast<unsigned char>(header[27]) << 8));
        const auto height = static_cast<unsigned int>(
            static_cast<unsigned char>(header[28]) | (static_cast<unsigned char>(header[29]) << 8));
        return std::pair{ width & 0x3FFFu, height & 0x3FFFu };
    }
    if (fourCc == "VP8L" && image.gcount() >= 25 && static_cast<unsigned char>(header[20]) == 0x2F) {
        const uint32_t bits = static_cast<unsigned char>(header[21]) |
            (static_cast<unsigned char>(header[22]) << 8) |
            (static_cast<unsigned char>(header[23]) << 16) |
            (static_cast<unsigned char>(header[24]) << 24);
        return std::pair{ (bits & 0x3FFFu) + 1, ((bits >> 14) & 0x3FFFu) + 1 };
    }
    return {};
}
}

void ImageCache::Initialize(const std::filesystem::path& dataDirectory) {
    fallbackPath_ = dataDirectory / L"Previews" / L"noimage.png";
}

std::pair<unsigned int, unsigned int> ImageCache::Dimensions(
    const std::filesystem::path& path) {
    const std::string extension = Utils::ToLower(path.extension().string());
    std::optional<std::pair<unsigned int, unsigned int>> result;
    if (extension == ".png")
        result = PngDimensions(path);
    else if (extension == ".jpg" || extension == ".jpeg")
        result = JpegDimensions(path);
    else if (extension == ".webp")
        result = WebpDimensions(path);
    if (!result || result->first == 0 || result->second == 0) {
        LOG_WARNING("[Preview] Couldn't read dimensions for {}; using 480x270", path.string());
        return { 480, 270 };
    }
    return *result;
}

std::optional<PreviewImage> ImageCache::Load(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        LOG_WARNING("[Preview] Image not found: {}", path.string());
        return {};
    }
    const auto [width, height] = Dimensions(path);
    const std::string narrowPath = path.string();
    const int textureId = createTexture(narrowPath.c_str());
    if (textureId < 0) {
        LOG_WARNING("[Preview] Failed to create texture: {}", path.string());
        return {};
    }
    LOG_DEBUG("[Preview] Loaded {} as texture {}", path.string(), textureId);
    return PreviewImage{ textureId, width, height };
}

std::string ImageCache::MenuExtra(const std::filesystem::path& configuredPath) {
    std::filesystem::path selected = configuredPath;
    if (selected.empty() || !std::filesystem::exists(selected))
        selected = fallbackPath_;

    const std::wstring key = selected.lexically_normal().wstring();
    auto found = images_.find(key);
    if (found == images_.end() && !failed_.contains(key)) {
        const auto loaded = Load(selected);
        if (loaded)
            found = images_.emplace(key, *loaded).first;
        else
            failed_[key] = true;
    }
    if (found == images_.end())
        return {};

    return std::format("!IMG:{}W{}H{}", found->second.TextureId,
        found->second.Width, found->second.Height);
}
