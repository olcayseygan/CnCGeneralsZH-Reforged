#include "test_harness.h"
#include "Platform/ModernWaterConfig.h"

#include <fstream>
#include <iterator>
#include <limits>

using namespace ModernWaterConfig;

namespace {
const char *const sample =
    "ModernWaterRule First\n"
    " MatchColors\n"
    "  Color = R:50 G:75 B:50\n"
    "  Color = R:190 G:15 B:20\n"
    " End\n"
    " MatchTextures\n"
    "  Texture = TSWater03.tga\n"
    "  Texture = Art\\Textures\\RedWater.tga\n"
    " End\n"
    " ReplacementColor = R:45 G:58 B:11\n"
    " Brightness = 0.67\n"
    " Pollution = 0.80\n"
    " Bubbles = Yes\n"
    "End\n";

std::string Changed(const std::string &before, const std::string &after) {
    std::string text(sample);
    const std::size_t position = text.find(before);
    CHECK(position != std::string::npos);
    if (position != std::string::npos) text.replace(position, before.size(), after);
    return text;
}

void CheckDefaults(const Rules &rules) {
    CHECK_EQ(rules.size(), 2u);
    if (rules.size() < 2) return;
    CHECK_STR(rules[0].name.c_str(), "PollutedWater");
    CHECK_EQ(rules[0].colors.size(), 1u);
    CHECK_EQ(rules[0].textures.size(), 1u);
    CHECK(rules[0].colors[0] == (Color{50, 75, 50}));
    CHECK_STR(rules[0].textures[0].c_str(), "TSWater03.tga");
    CHECK(rules[0].replacement == (Color{45, 58, 11}));
    CHECK_NEAR(rules[0].brightness, .67f, 0.00001f);
    CHECK_NEAR(rules[0].pollution, .80f, 0.00001f);
    CHECK(rules[0].bubbles);
    CHECK(rules[0].enabled);
    CHECK_STR(rules[1].name.c_str(), "ClearWater");
    CHECK_NEAR(rules[1].underwaterClarity, .90f, .00001f);
    CHECK_EQ(rules[1].brightness, 1.0f);
    CHECK_EQ(rules[1].pollution, 0.0f);
    CHECK(!rules[1].bubbles);
}
}

TEST(modern_water_defaults_and_shipped_file_stay_identical)
{
    CheckDefaults(Defaults());
    // Defined by CMake so the test is independent of its working directory.
    std::ifstream file(MODERN_WATER_INI_PATH, std::ios::binary);
    CHECK(file.good());
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Rules rules;
    std::string error;
    CHECK(Parse(text, rules, error));
    CHECK(error.empty());
    CheckDefaults(rules);
}

TEST(modern_water_lists_match_cross_combinations_and_require_both)
{
    Rules rules;
    std::string error;
    CHECK(Parse(sample, rules, error));
    CHECK_EQ(rules.size(), 1u);
    if (rules.empty()) return;
    for (const Color color : {Color{50, 75, 50}, Color{190, 15, 20}})
        for (const char *texture : {"TSWater03.tga", "redwater.TGA", "ART/Textures/tsWATER03.TGA", "Art\\Textures\\REDwater.tga"})
            CHECK(Match(rules, color, texture) == &rules[0]);
    CHECK(Match(rules, Color{50, 76, 50}, "TSWater03.tga") == NULL);
    CHECK(Match(rules, Color{50, 75, 50}, "Other.tga") == NULL);
    CHECK(Match(rules, Color{50, 75, 50}, "") == NULL);
    CHECK(Match(rules, Color{50, 75, 50}, "Art/Textures/") == NULL);
}

TEST(modern_water_rule_order_and_disable_are_explicit)
{
    const std::string second = Changed("First", "Second");
    Rules rules;
    std::string error;
    CHECK(Parse(std::string(sample) + second, rules, error));
    const Rule *rule = Match(rules, Color{50, 75, 50}, "TSWater03.tga");
    CHECK(rule != NULL);
    if (rule) CHECK_STR(rule->name.c_str(), "First");
    CHECK(Parse(Changed(" Bubbles = Yes", " Bubbles = Yes\n Enabled = No") + second, rules, error));
    rule = Match(rules, Color{50, 75, 50}, "TSWater03.tga");
    CHECK(rule != NULL);
    if (rule) CHECK_STR(rule->name.c_str(), "Second");
    CHECK(Parse(Changed(" Bubbles = Yes", " Bubbles = Yes\n Enabled = No"), rules, error));
    CHECK(Match(rules, Color{50, 75, 50}, "TSWater03.tga") == NULL);
}

TEST(modern_water_bad_edits_preserve_previous_rules_and_report_line)
{
    const std::string invalid[] = {
        Changed("R:50", "R:-1"), Changed("R:50", "R:256"),
        Changed("R:50", "R:50.5"), Changed("R:50", "R:9999999999999999999"),
        Changed("G:75", "X:75"), Changed("B:50", "B:50 extra"),
        Changed("0.67", "nan"), Changed("0.67", "inf"), Changed("0.67", "0.09"),
        Changed("0.67", "1.01"), Changed("0.80", "-0.01"), Changed("0.80", "1.01"),
        Changed(" Bubbles = Yes", " UnderwaterClarity = -0.01"),
        Changed(" Bubbles = Yes", " UnderwaterClarity = 1.01"),
        Changed(" Bubbles = Yes", " UnderwaterClarity = nan"),
        Changed(" Bubbles = Yes", " UnderwaterClarity = 0.9\n UnderwaterClarity = 0.5"),
        Changed("0.80", "0.8junk"), Changed("Bubbles = Yes", "Bubbles = Maybe"),
        Changed("Bubbles = Yes", "Enabled = Maybe"), Changed("Bubbles = Yes", "Unknown = Yes"),
        Changed(" Bubbles = Yes", " Bubbles = Yes\n Bubbles = No"),
        Changed("  Texture = TSWater03.tga", "  Texture = Art/Textures/"),
        Changed("  Texture = TSWater03.tga", "  Texture = Two Words.tga"),
        Changed("MatchColors", "MatchColors = Yes"),
        Changed("MatchColors", "MatchTextures"),
        Changed(" ReplacementColor = R:45 G:58 B:11\n", ""),
        Changed("  Color = R:50 G:75 B:50\n  Color = R:190 G:15 B:20\n", ""),
        Changed("  Texture = TSWater03.tga\n  Texture = Art\\Textures\\RedWater.tga\n", ""),
        std::string(sample).substr(0, std::string(sample).size() - 4),
        std::string(sample) + Changed("First", "FIRST"),
        std::string(sample) + "End\n",
        std::string(sample) + "ModernWaterRule unfinished\n"
    };
    for (const std::string &text : invalid) {
        Rules rules = Defaults();
        std::string error;
        CHECK(!Parse(text, rules, error));
        CHECK(error.find("line ") == 0);
        CheckDefaults(rules);
    }
    Rules rules = Defaults();
    std::string error;
    CHECK(!Parse("; comment\nModernWaterRule Broken\nBogus = 1\n", rules, error));
    CHECK(error.find("line 3:") == 0);
}

TEST(modern_water_empty_file_is_deliberate_disable_and_syntax_handles_comments)
{
    Rules rules = Defaults();
    std::string error = "old error";
    CHECK(Parse("\xEF\xBB\xBF; disabled\r\n \t\n", rules, error));
    CHECK(rules.empty());
    CHECK(error.empty());
    std::string text = Changed("ModernWaterRule First", "modernwaterrule First ; first rule");
    text.replace(text.find("MatchColors"), 11, "matchcolors");
    text.replace(text.find("Bubbles = Yes"), 13, "Bubbles = yEs");
    CHECK(Parse(text, rules, error));
    CHECK_EQ(rules.size(), 1u);
}

TEST(modern_water_optional_parameters_and_numeric_endpoints)
{
    Rules rules;
    std::string error;
    std::string text = Changed(" Brightness = 0.67\n Pollution = 0.80\n Bubbles = Yes\n", "");
    CHECK(Parse(text, rules, error));
    if (rules.empty()) return;
    CHECK_EQ(rules[0].brightness, 1.0f);
    CHECK_EQ(rules[0].pollution, 0.0f);
    CHECK(!rules[0].bubbles);
    CHECK(rules[0].enabled);
    CHECK(Parse(Changed("0.67", "0.1"), rules, error));
    CHECK(Parse(Changed("0.67", "1"), rules, error));
    CHECK(Parse(Changed("0.80", "0"), rules, error));
    CHECK(Parse(Changed("0.80", "1"), rules, error));
    CHECK(Parse(Changed("ReplacementColor = R:45 G:58 B:11", "ReplacementColor = R:0 G:255 B:0"), rules, error));
}

TEST(modern_water_resolution_never_carries_pollution_to_next_map)
{
    const Rules rules = Defaults();
    Material material = Resolve(rules, Color{50, 75, 50}, "TSWater03.tga");
    CHECK(material.color == (Color{45, 58, 11}));
    CHECK_NEAR(material.brightness, .67f, .00001f);
    CHECK_NEAR(material.pollution, .80f, .00001f);
    CHECK_EQ(material.tintAmount, 1.0f);
    CHECK(material.bubbles);
    CHECK_STR(material.ruleName.c_str(), "PollutedWater");

    for (const Color color : {Color{255, 255, 255}, Color{255, 0, 0}, Color{0, 0, 0}, Color{50, 75, 50}}) {
        material = Resolve(rules, color, "Unmatched.tga");
        CHECK(material.color == color);
        CHECK_EQ(material.brightness, 1.0f);
        CHECK_EQ(material.pollution, 0.0f);
        CHECK_EQ(material.tintAmount, color.r == 255 && color.g == 255 && color.b == 255 ? 0.0f : 1.0f);
        CHECK(!material.bubbles);
        CHECK(material.ruleName.empty());
    }
}

TEST(modern_water_clarity_defaults_match_and_reset)
{
    Rules rules = Defaults();
    Material clear = Resolve(rules, Color{255, 255, 255}, "TWWater01.tga");
    CHECK_NEAR(clear.underwaterClarity, .90f, .00001f);
    CHECK_EQ(clear.tintAmount, 0.0f);
    CHECK_EQ(clear.brightness, 1.0f);
    CHECK_EQ(clear.pollution, 0.0f);
    CHECK(!clear.bubbles);
    Material other = Resolve(rules, Color{255, 255, 255}, "Other.tga");
    CHECK_EQ(other.underwaterClarity, 0.0f);
    CHECK_EQ(other.tintAmount, 0.0f);
    std::string error;
    CHECK(Parse(Changed(" Bubbles = Yes", " UnderwaterClarity = 1"), rules, error));
    CHECK_EQ(rules[0].underwaterClarity, 1.0f);
    CHECK(Parse(Changed(" Bubbles = Yes", " UnderwaterClarity = 0"), rules, error));
    CHECK_EQ(rules[0].underwaterClarity, 0.0f);
}

TEST(modern_water_normalized_byte_colors_round_trip_exactly)
{
    for (int value = 0; value < 256; ++value) {
        const Color color = ByteColor(value / 255.0f, value / 255.0f, value / 255.0f);
        CHECK(color == (Color{value, value, value}));
    }
    const Rules rules = Defaults();
    CHECK(Match(rules, ByteColor(50 / 255.0f, 75 / 255.0f, 50 / 255.0f), "TSWater03.tga") != NULL);
    CHECK(ByteColor(-1, 2, .5f) == (Color{0, 255, 128}));
    CHECK_EQ(ColorByte(std::numeric_limits<float>::quiet_NaN()), 0);
    CHECK_EQ(ColorByte(std::numeric_limits<float>::infinity()), 0);
}
