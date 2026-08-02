#ifndef AI_CAPTION_PLUGIN_MAIN_CAPTION_WIDGET_H
#define AI_CAPTION_PLUGIN_MAIN_CAPTION_WIDGET_H

#include "CaptionSettingsWidget.h"
#include "../CaptionPluginManager.h"

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QHideEvent;
class QShowEvent;

class MainCaptionWidget final : public QWidget {
Q_OBJECT

public:
    explicit MainCaptionWidget(CaptionPluginManager &plugin_manager);
    ~MainCaptionWidget() override;

    void show_self();
    void show_settings_dialog();
    void external_state_changed();
    void scene_collection_changed();
    void stream_started_event();
    void stream_stopped_event();
    void stop_captioning();

private:
    CaptionPluginManager &plugin_manager;
    CaptionSettingsWidget caption_settings_widget;
    QPlainTextEdit *caption_text = nullptr;
    QLabel *status_text = nullptr;

    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void accept_widget_settings(CaptionPluginSettings new_settings);
    void handle_caption_data(
            std::shared_ptr<OutputCaptionResult> caption_result,
            bool cleared);
    void handle_source_capture_status_change(std::shared_ptr<SourceCaptionerStatus> status);
};

#endif // AI_CAPTION_PLUGIN_MAIN_CAPTION_WIDGET_H
