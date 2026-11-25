// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace oled::localization {
struct Source {
    std::string_view locale;
    std::string_view data;
};

inline constexpr Source sources[] = {
#include "translation_catalog.inc"
};

using Messages = std::map<std::string, std::string, std::less<>>;
using Catalog = std::map<std::string, Messages, std::less<>>;

inline Catalog load() {
    Catalog catalog;
    for (const auto& source : sources) {
        std::istringstream input{std::string(source.data)};
        auto& messages = catalog[std::string(source.locale)];
        std::string line;
        unsigned row = 0;
        while (std::getline(input, line)) {
            ++row;
            if (row == 1 && line.starts_with("\xef\xbb\xbf"))
                line.erase(0, 3);
            if (line.ends_with('\r'))
                line.pop_back();
            if (line.empty() || line.starts_with('#'))
                continue;
            const auto tab = line.find('\t');
            std::string key, value;
            const auto read = [](std::string field, std::string& result) {
                if (field.size() < 2 || field.front() != '"' || field.back() != '"')
                    return false;
                std::istringstream stream(std::move(field));
                return (stream >> std::quoted(result)) && stream.peek() == EOF;
            };
            if (tab == std::string::npos || !read(line.substr(0, tab), key) || key.empty() ||
                !read(line.substr(tab + 1), value) || value.find_first_of("\t\r\n") != std::string::npos ||
                !messages.emplace(key, value).second)
                throw std::runtime_error(std::string(source.locale) + ".tsv:" + std::to_string(row) +
                                         ": invalid or duplicate translation");
        }
    }
    const auto& base = catalog.at("en");
    for (const auto& [locale, messages] : catalog)
        for (const auto& [key, value] : messages)
            if (!base.contains(key))
                throw std::runtime_error(locale + ".tsv: unknown English key: " + key);
    return catalog;
}

inline std::string select_locale(const Catalog& catalog, std::string locale) {
    for (auto& c : locale) {
        if (c == '.' || c == '@')
            break;
        if (c == '_') c = '-';
        else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    locale = locale.substr(0, locale.find_first_of(".@"));
    if (catalog.contains(locale))
        return locale;
    if (locale == "zh" || locale.starts_with("zh-")) {
        const auto part = locale.size() > 3 ? locale.substr(3) : std::string{};
        const auto component = std::string_view(part).substr(0, part.find('-'));
        if (component == "hant" || component == "tw" || component == "hk" || component == "mo")
            return "zh-hant";
        return "zh-hans";
    }
    for (auto separator = locale.rfind('-'); separator != std::string::npos; separator = locale.rfind('-')) {
        locale.resize(separator);
        if (catalog.contains(locale))
            return locale;
    }
    return "en";
}

inline std::string_view lookup(const Catalog& catalog, std::string_view locale, std::string_view key) {
    const auto find = [&](std::string_view language) -> std::string_view {
        const auto language_messages = catalog.find(language);
        if (language_messages == catalog.end())
            return {};
        const auto text = language_messages->second.find(key);
        return text == language_messages->second.end() ? std::string_view{} : std::string_view(text->second);
    };
    if (auto text = find(locale); !text.empty())
        return text;
    if (auto text = find("en"); !text.empty())
        return text;
    return key;
}
}
