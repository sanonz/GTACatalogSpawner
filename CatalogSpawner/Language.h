#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct LanguageInfo {
    std::string Code;
    std::string Name;
    std::filesystem::path FilePath;
};

class LanguageManager {
public:
    bool Reload(const std::filesystem::path& directory, const std::string& preferredCode);
    bool Select(const std::string& code);
    std::string Text(const std::string& key) const;
    std::string Format(const std::string& key,
        std::initializer_list<std::pair<std::string, std::string>> replacements) const;
    const std::vector<LanguageInfo>& Languages() const;
    const std::string& ActiveCode() const;

private:
    struct LanguagePack {
        LanguageInfo Info;
        std::unordered_map<std::string, std::string> Strings;
    };

    static bool ParseFile(const std::filesystem::path& path, LanguagePack& pack);
    void Activate(const LanguagePack& pack);

    std::vector<LanguagePack> packs_;
    std::vector<LanguageInfo> languageInfos_;
    std::unordered_map<std::string, std::string> activeStrings_;
    std::string activeCode_ = "en-US";
};

extern LanguageManager gLanguage;

