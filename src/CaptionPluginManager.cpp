#include "CaptionPluginManager.h"

#include "log.c"

CaptionPluginManager::CaptionPluginManager(const CaptionPluginSettings &initial_settings)
        : plugin_settings(initial_settings),
          source_captioner(
                  initial_settings.source_cap_settings,
                  false),
          browser_caption_server(initial_settings.browser_overlay) {
    plugin_settings.browser_overlay = browser_caption_server.settings();
    QObject::connect(
            &source_captioner,
            &SourceCaptioner::caption_result_received,
            &browser_caption_server,
            &BrowserCaptionServer::update_caption);
    QObject::connect(
            &browser_caption_server,
            &BrowserCaptionServer::browser_consumer_presence_changed,
            this,
            [this](bool) { update_settings(plugin_settings); });

    if (browser_caption_server.is_listening())
        info_log("Browser caption overlay ready on a protected localhost endpoint");
    else
        warn_log("Browser caption overlay could not listen on localhost");
}

void CaptionPluginManager::external_state_changed(
        bool is_live,
        bool is_preview_open,
        const string &scene_collection_name) {
    state.external_is_streaming = is_live;
    state.external_is_preview_open = is_preview_open;
    state.external_scene_collection_name = scene_collection_name;
    update_settings(plugin_settings);
}

void CaptionPluginManager::update_settings(const CaptionPluginSettings &new_settings) {
    CaptionPluginSettings applied_settings = new_settings;
    if (applied_settings.browser_overlay != plugin_settings.browser_overlay) {
        applied_settings.browser_overlay =
                browser_caption_server.configure(applied_settings.browser_overlay);
    }
    const SourceCaptionerSettings &source_settings = applied_settings.source_cap_settings;
    const string scene_collection_name = state.external_scene_collection_name;

    const bool streaming_consumer =
            state.external_is_streaming && source_settings.native_stream_output_enabled;
    const bool preview_consumer = state.external_is_preview_open;
    const bool file_consumer = source_settings.file_output_settings.isValidEnabled();
    const bool browser_consumer =
            browser_caption_server.is_listening() && browser_caption_server.has_browser_consumer();
    const bool should_caption =
            applied_settings.enabled &&
            (streaming_consumer || preview_consumer || file_consumer || browser_consumer);

    const bool settings_equal = applied_settings == plugin_settings;
    const bool was_captioning = state.is_captioning;
    const bool was_native_output_active =
            state.is_captioning && state.is_captioning_streaming;
    const bool runtime_settings_changed =
            !settings_equal || scene_collection_name != state.captioning_scene_collection_name;
    if (update_count != 0 &&
        settings_equal &&
        should_caption == state.is_captioning &&
        streaming_consumer == state.is_captioning_streaming &&
        preview_consumer == state.is_captioning_preview &&
        file_consumer == state.is_captioning_file_output &&
        browser_consumer == state.is_captioning_browser_overlay &&
        scene_collection_name == state.captioning_scene_collection_name) {
        return;
    }

    ++update_count;
    plugin_settings = applied_settings;
    state.is_captioning_streaming = streaming_consumer;
    state.is_captioning_preview = preview_consumer;
    state.is_captioning_file_output = file_consumer;
    state.is_captioning_browser_overlay = browser_consumer;
    state.captioning_scene_collection_name = scene_collection_name;

    bool captioning_active = was_captioning;
    if (should_caption && (!was_captioning || runtime_settings_changed)) {
        captioning_active = source_captioner.start_caption_stream(source_settings);
    } else if (!should_caption && (was_captioning || runtime_settings_changed)) {
        source_captioner.set_settings(source_settings);
        captioning_active = false;
    }
    state.is_captioning = captioning_active;

    const bool native_output_active = captioning_active && streaming_consumer;
    if (native_output_active != was_native_output_active) {
        if (native_output_active)
            source_captioner.stream_started_event();
        else
            source_captioner.stream_stopped_event();
    }

    info_log(
            "Caption consumers: stream=%d preview=%d file=%d browser=%d; requested=%d active=%d",
            streaming_consumer,
            preview_consumer,
            file_consumer,
            browser_consumer,
            should_caption,
            captioning_active);

    if (!settings_equal)
        emit settings_changed(applied_settings);
}

CaptioningState CaptionPluginManager::captioning_state() const {
    return state;
}
