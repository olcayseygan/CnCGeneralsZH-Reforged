/* Shared, engine-independent material rules for the game and water preview. */
#pragma once

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace ModernWaterConfig {

struct Color { int r, g, b; };
inline bool operator==(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

struct Rule {
    std::string name;
    std::vector<Color> colors;
    std::vector<std::string> textures;
    Color replacement = {255, 255, 255};
    float brightness = 1.0f;
    float pollution = 0.0f;
    float underwaterClarity = 0.0f;
    bool bubbles = false;
    bool enabled = true;
};
typedef std::vector<Rule> Rules;

inline Rules Defaults() {
    Rule rule;
    rule.name = "PollutedWater";
    rule.colors.push_back(Color{50, 75, 50});
    rule.textures.push_back("TSWater03.tga");
    rule.replacement = Color{45, 58, 11};
    rule.brightness = 0.67f;
    rule.pollution = 0.80f;
    rule.bubbles = true;
    Rules rules(1, rule);
    Rule clear;
    clear.name = "ClearWater";
    clear.colors.push_back(Color{255, 255, 255});
    clear.textures.push_back("TWWater01.tga");
    clear.underwaterClarity = 0.90f;
    rules.push_back(clear);
    return rules;
}

// Authored RGB values are stored by the engine as normalized floats. Round once
// to their original bytes so float representation cannot break an exact match.
inline int ColorByte(float value) {
    if (!std::isfinite(value) || !(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<int>(value * 255.0f + 0.5f);
}
inline Color ByteColor(float r, float g, float b) {
    return Color{ColorByte(r), ColorByte(g), ColorByte(b)};
}

namespace Detail {
inline std::string Lower(std::string text) {
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] >= 'A' && text[i] <= 'Z') text[i] += 'a' - 'A';
    return text;
}
inline std::string Trim(const std::string &text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
inline std::string TextureBase(const std::string &texture) {
    const std::size_t slash = texture.find_last_of("/\\");
    return Lower(texture.substr(slash == std::string::npos ? 0 : slash + 1));
}
inline bool ReadColor(const std::string &text, Color &result) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    int bytes[3];
    for (int i = 0; i < 3; ++i) {
        char channel, colon;
        if (!(input >> channel >> colon >> bytes[i]) || colon != ':' ||
            (channel != "RGB"[i] && channel != "rgb"[i]) || bytes[i] < 0 || bytes[i] > 255)
            return false;
    }
    input >> std::ws;
    if (!input.eof()) return false;
    result = Color{bytes[0], bytes[1], bytes[2]};
    return true;
}
inline bool ReadUnit(const std::string &text, float minimum, float &value) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    float parsed;
    if (!(input >> parsed) || !std::isfinite(parsed) || parsed < minimum || parsed > 1.0f)
        return false;
    input >> std::ws;
    if (!input.eof()) return false;
    value = parsed;
    return true;
}
inline bool ReadBool(const std::string &text, bool &value) {
    const std::string word = Lower(text);
    if (word != "yes" && word != "no") return false;
    value = word == "yes";
    return true;
}
inline bool Fail(std::string &error, std::size_t line, const std::string &message) {
    std::ostringstream output;
    output << "line " << line << ": " << message;
    error = output.str();
    return false;
}
} // namespace Detail

// File order decides overlapping rules. Both lists must match; neither is a
// wildcard. Compare texture basenames because engine callers may pass a path.
inline const Rule *Match(const Rules &rules, Color color, const std::string &texture) {
    const std::string basename = Detail::TextureBase(texture);
    for (Rules::const_iterator rule = rules.begin(); rule != rules.end(); ++rule) {
        if (!rule->enabled || std::find(rule->colors.begin(), rule->colors.end(), color) == rule->colors.end())
            continue;
        for (std::size_t i = 0; i < rule->textures.size(); ++i)
            if (!basename.empty() && Detail::TextureBase(rule->textures[i]) == basename)
                return &*rule;
    }
    return NULL;
}

struct Material {
    Color color = {255, 255, 255};
    float brightness = 1.0f;
    float pollution = 0.0f;
    float underwaterClarity = 0.0f;
    float tintAmount = 0.0f;
    bool bubbles = false;
    std::string ruleName;
};

// Resolve afresh from the effective map settings, including when leaving a
// matched map. White is the legacy neutral multiplier, so it preserves the
// existing blue Modern material; every other authored color keeps its tint.
inline Material Resolve(const Rules &rules, Color mapColor, const std::string &mapTexture) {
    Material material;
    material.color = mapColor;
    material.tintAmount = mapColor == Color{255, 255, 255} ? 0.0f : 1.0f;
    if (const Rule *rule = Match(rules, mapColor, mapTexture)) {
        material.color = rule->replacement;
        material.brightness = rule->brightness;
        material.pollution = rule->pollution;
        material.underwaterClarity = rule->underwaterClarity;
        material.tintAmount = rule->underwaterClarity > 0.0f && rule->replacement == Color{255, 255, 255}
            && rule->brightness == 1.0f ? 0.0f : 1.0f;
        material.bubbles = rule->bubbles;
        material.ruleName = rule->name;
    }
    return material;
}

// Parse into a temporary list: a malformed edit cannot leave half of a new
// configuration installed. An empty/comment-only file intentionally has no
// rules; callers use Defaults() when the file is missing or fails to parse.
// Each declaration/assignment occupies one line; semicolons begin comments.
inline bool Parse(const std::string &text, Rules &result, std::string &error) {
    enum Section { Outside, InRule, InColors, InTextures } section = Outside;
    enum Field { Colors = 1, Textures = 2, Replacement = 4, Brightness = 8,
                 Pollution = 16, Bubbles = 32, Enabled = 64, UnderwaterClarity = 128 };
    Rules parsed;
    Rule current;
    unsigned fields = 0;
    std::size_t lineNumber = 0;
    std::istringstream input(text);
    std::string line;
    error.clear();
    while (std::getline(input, line)) {
        ++lineNumber;
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        const std::size_t comment = line.find(';');
        if (comment != std::string::npos) line.erase(comment);
        line = Detail::Trim(line);
        if (line.empty()) continue;
        const std::string lower = Detail::Lower(line);
        if (section == Outside) {
            std::istringstream declaration(line);
            std::string keyword, name, extra;
            if (!(declaration >> keyword >> name) || Detail::Lower(keyword) != "modernwaterrule" || declaration >> extra)
                return Detail::Fail(error, lineNumber, "expected ModernWaterRule followed by one name");
            for (std::size_t i = 0; i < parsed.size(); ++i)
                if (Detail::Lower(parsed[i].name) == Detail::Lower(name))
                    return Detail::Fail(error, lineNumber, "duplicate rule name '" + name + "'");
            current = Rule();
            current.name = name;
            fields = 0;
            section = InRule;
            continue;
        }
        if (lower == "end") {
            if (section == InColors || section == InTextures) {
                if ((section == InColors && current.colors.empty()) ||
                    (section == InTextures && current.textures.empty()))
                    return Detail::Fail(error, lineNumber, "match lists must not be empty");
                section = InRule;
            } else {
                if ((fields & (Colors | Textures | Replacement)) != (Colors | Textures | Replacement))
                    return Detail::Fail(error, lineNumber, "rule needs MatchColors, MatchTextures and ReplacementColor");
                parsed.push_back(current);
                section = Outside;
            }
            continue;
        }
        if (section == InRule && (lower == "matchcolors" || lower == "matchtextures")) {
            const unsigned field = lower == "matchcolors" ? Colors : Textures;
            if (fields & field) return Detail::Fail(error, lineNumber, "duplicate " + line + " block");
            fields |= field;
            section = field == Colors ? InColors : InTextures;
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            return Detail::Fail(error, lineNumber, "expected a field assignment or End");
        const std::string key = Detail::Lower(Detail::Trim(line.substr(0, equals)));
        const std::string value = Detail::Trim(line.substr(equals + 1));
        if (section == InColors) {
            Color color;
            if (key != "color" || !Detail::ReadColor(value, color))
                return Detail::Fail(error, lineNumber, "expected Color = R:0..255 G:0..255 B:0..255");
            current.colors.push_back(color);
        } else if (section == InTextures) {
            if (key != "texture" || Detail::TextureBase(value).empty() ||
                value.find_first_of(" \t=\"") != std::string::npos)
                return Detail::Fail(error, lineNumber, "expected Texture = filename without spaces");
            current.textures.push_back(value);
        } else {
            unsigned field = 0;
            bool valid = false;
            if (key == "replacementcolor") { field = Replacement; valid = Detail::ReadColor(value, current.replacement); }
            else if (key == "brightness") { field = Brightness; valid = Detail::ReadUnit(value, 0.1f, current.brightness); }
            else if (key == "pollution") { field = Pollution; valid = Detail::ReadUnit(value, 0.0f, current.pollution); }
            else if (key == "underwaterclarity") { field = UnderwaterClarity; valid = Detail::ReadUnit(value, 0.0f, current.underwaterClarity); }
            else if (key == "bubbles") { field = Bubbles; valid = Detail::ReadBool(value, current.bubbles); }
            else if (key == "enabled") { field = Enabled; valid = Detail::ReadBool(value, current.enabled); }
            else return Detail::Fail(error, lineNumber, "unknown field '" + key + "'");
            if (fields & field) return Detail::Fail(error, lineNumber, "duplicate field '" + key + "'");
            if (!valid) return Detail::Fail(error, lineNumber, "invalid value for '" + key + "'");
            fields |= field;
        }
    }
    if (section != Outside) return Detail::Fail(error, lineNumber + 1, "unexpected end of file; missing End");
    result.swap(parsed);
    return true;
}
} // namespace ModernWaterConfig
