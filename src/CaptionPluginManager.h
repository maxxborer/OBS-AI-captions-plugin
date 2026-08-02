#ifndef AI_CAPTION_PLUGIN_CAPTION_PLUGIN_MANAGER_H
#define AI_CAPTION_PLUGIN_CAPTION_PLUGIN_MANAGER_H

#include "CaptionPluginSettings.h"
#include "SourceCaptioner.h"
#include "BrowserCaptionServer.h"

struct CaptioningState {
    bool external_is_streaming = false;
    bool external_is_preview_open = false;
    string external_scene_collection_name;

    bool is_captioning = false;
    bool is_captioning_streaming = false;
    bool is_captioning_preview = false;
    bool is_captioning_file_output = false;
    bool is_captioning_browser_overlay = false;
    string captioning_scene_collection_name;
};

class CaptionPluginManager : public QObject {
Q_OBJECT

public:
    CaptionPluginSettings plugin_settings;
    SourceCaptioner source_captioner;
    BrowserCaptionServer browser_caption_server;
    CaptioningState state;

    explicit CaptionPluginManager(const CaptionPluginSettings &initial_settings);

    void external_state_changed(
            bool is_live,
            bool is_preview_open,
            const string &scene_collection_name);
    void update_settings(const CaptionPluginSettings &new_settings);
    CaptioningState captioning_state() const;

signals:
    void settings_changed(CaptionPluginSettings new_settings);

private:
    int update_count = 0;
};

#endif // AI_CAPTION_PLUGIN_CAPTION_PLUGIN_MANAGER_H
