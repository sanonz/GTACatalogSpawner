#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

struct PreviewImage {
    int TextureId = -1;
    unsigned int Width = 480;
    unsigned int Height = 270;
};

class ImageCache {
public:
    void Initialize(const std::filesystem::path& dataDirectory);
    std::string MenuExtra(const std::filesystem::path& configuredPath);

private:
    std::optional<PreviewImage> Load(const std::filesystem::path& path);
    static std::pair<unsigned int, unsigned int> Dimensions(const std::filesystem::path& path);

    std::filesystem::path fallbackPath_;
    std::unordered_map<std::wstring, PreviewImage> images_;
    std::unordered_map<std::wstring, bool> failed_;
};

extern ImageCache gImages;

