#include "caption_settings_storage.h"

#include <obs-data.h>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}
}

int main() {
    obs_data_t *root = obs_data_create();

    const CaptionPluginSettings fresh = load_CaptionPluginSettings(root);
    require(fresh.browser_overlay.port >= 49152,
            "A first-run configuration must generate a browser overlay port");
    require(fresh.browser_overlay.access_token.size() == 64,
            "A first-run configuration must generate a protected browser URL");

    obs_data_t *incomplete_root = obs_data_create();
    obs_data_t *incomplete_current = obs_data_create();
    obs_data_set_int(incomplete_current, "browser_overlay_port", 0);
    obs_data_set_string(incomplete_current, "browser_overlay_token", "");
    obs_data_set_obj(incomplete_root, kSettingsSaveEntryName, incomplete_current);
    obs_data_release(incomplete_current);
    const CaptionPluginSettings repaired = load_CaptionPluginSettings(incomplete_root);
    require(repaired.browser_overlay.port >= 49152 &&
                    repaired.browser_overlay.access_token.size() == 64,
            "An incomplete 0.40.x browser configuration must repair itself");
    obs_data_release(incomplete_root);

    obs_data_t *legacy = obs_data_create();
    obs_data_array_t *legacy_replacements = obs_data_array_create();
    obs_data_t *legacy_replacement = obs_data_create();
    obs_data_set_string(legacy_replacement, "type", "whole_word_case_insensitive");
    obs_data_set_string(legacy_replacement, "from", "блять");
    obs_data_set_string(legacy_replacement, "to", "***");
    obs_data_array_push_back(legacy_replacements, legacy_replacement);
    obs_data_t *builtin_replacement = obs_data_create();
    obs_data_set_string(builtin_replacement, "type", "whole_word_case_insensitive");
    obs_data_set_string(builtin_replacement, "from", "обс");
    obs_data_set_string(builtin_replacement, "to", "OBS");
    obs_data_array_push_back(legacy_replacements, builtin_replacement);
    obs_data_set_array(legacy, "word_replacements", legacy_replacements);
    obs_data_set_obj(root, kLegacySettingsSaveEntryName, legacy);
    obs_data_release(legacy_replacement);
    obs_data_release(builtin_replacement);
    obs_data_array_release(legacy_replacements);
    obs_data_release(legacy);

    CaptionPluginSettings migrated = load_CaptionPluginSettings(root);
    require(migrated.source_cap_settings.format_settings.text_replacements.size() == 1,
            "Legacy text replacements must migrate into the simplified plugin settings");
    require(migrated.source_cap_settings.format_settings.text_replacements.front().to == "***",
            "A migrated replacement must preserve its output text");

    save_CaptionPluginSettings(root, migrated);
    const CaptionPluginSettings saved = load_CaptionPluginSettings(root);
    require(saved.source_cap_settings.format_settings.text_replacements ==
                    migrated.source_cap_settings.format_settings.text_replacements,
            "Migrated replacements must survive a save and reload");

    obs_data_t *banned_root = obs_data_create();
    obs_data_t *banned_legacy = obs_data_create();
    obs_data_set_string(banned_legacy, "manual_banned_words", "блять сука");
    obs_data_set_obj(banned_root, kLegacySettingsSaveEntryName, banned_legacy);
    obs_data_release(banned_legacy);
    const CaptionPluginSettings banned_migrated = load_CaptionPluginSettings(banned_root);
    require(banned_migrated.source_cap_settings.format_settings.text_replacements.size() == 2,
            "The oldest banned-word list must migrate into text replacements");
    require(banned_migrated.source_cap_settings.format_settings.text_replacements.front().type ==
                    "text_case_insensitive",
            "The oldest banned-word migration must preserve substring matching");
    obs_data_release(banned_root);

    obs_data_release(root);
    return 0;
}
