#ifndef AI_CAPTION_PLUGIN_CAPTION_SETTINGS_WIDGET_H
#define AI_CAPTION_PLUGIN_CAPTION_SETTINGS_WIDGET_H

#include "../CaptionPluginSettings.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QWidget;

class CaptionSettingsWidget final : public QWidget {
Q_OBJECT

public:
    explicit CaptionSettingsWidget(const CaptionPluginSettings &latest_settings);
    void set_settings(const CaptionPluginSettings &new_settings);

signals:
    void settings_accepted(CaptionPluginSettings new_settings);
    void preview_requested();

private slots:
    void accept_current_settings();
    void choose_file_output_folder();
    void copy_browser_url();
    void open_browser_designer();
    void update_source_controls();
    void update_output_controls();

private:
    CaptionPluginSettings current_settings;
    QCheckBox *enabled_checkbox = nullptr;
    QLabel *activation_status = nullptr;
    QComboBox *sources_combo = nullptr;
    QComboBox *caption_when_combo = nullptr;
    QComboBox *mute_source_combo = nullptr;
    QWidget *mute_source_row = nullptr;
    QCheckBox *stream_output_checkbox = nullptr;
    QCheckBox *file_output_checkbox = nullptr;
    QWidget *file_output_controls = nullptr;
    QLineEdit *file_output_folder = nullptr;
    QLineEdit *file_output_filename = nullptr;
    QLineEdit *browser_url = nullptr;
    QLabel *copy_status = nullptr;
    QLabel *validation_label = nullptr;

    void populate_audio_sources();
    void update_ui();
    void show_validation_error(const QString &message, QWidget *field);
    void showEvent(QShowEvent *event) override;
};

#endif // AI_CAPTION_PLUGIN_CAPTION_SETTINGS_WIDGET_H
