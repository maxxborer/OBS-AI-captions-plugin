#ifndef AI_CAPTION_PLUGIN_CAPTION_SETTINGS_WIDGET_H
#define AI_CAPTION_PLUGIN_CAPTION_SETTINGS_WIDGET_H

#include "../CaptionPluginSettings.h"

#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTableWidget;
class QTextEdit;
class QWidget;

class CaptionSettingsWidget final : public QWidget {
Q_OBJECT

public:
    explicit CaptionSettingsWidget(
            const CaptionPluginSettings &latest_settings,
            const QString &overlay_url,
            const QString &designer_url);
    void set_settings(const CaptionPluginSettings &new_settings);
    void set_browser_urls(const QString &overlay_url, const QString &designer_url);

signals:
    void settings_accepted(CaptionPluginSettings new_settings);
    void preview_requested();

private slots:
    void accept_current_settings();
    void choose_file_output_folder();
    void copy_browser_url();
    void open_browser_designer();
    void add_text_replacement();
    void update_filename_preview();
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
    QComboBox *local_model_combo = nullptr;
    QTextEdit *local_hotwords = nullptr;
    QCheckBox *stream_output_checkbox = nullptr;
    QCheckBox *file_output_checkbox = nullptr;
    QWidget *file_output_controls = nullptr;
    QLineEdit *file_output_folder = nullptr;
    QLineEdit *file_output_filename = nullptr;
    QLabel *file_output_filename_preview = nullptr;
    QTableWidget *text_replacements = nullptr;
    QLineEdit *browser_url = nullptr;
    QPushButton *copy_browser_button = nullptr;
    QPushButton *browser_designer_button = nullptr;
    QString browser_designer_url;
    QLabel *copy_status = nullptr;
    QLabel *validation_label = nullptr;

    void populate_audio_sources();
    void populate_text_replacements();
    void append_text_replacement_row(const TextReplacement &replacement);
    void update_ui();
    void show_validation_error(const QString &message, QWidget *field);
    void showEvent(QShowEvent *event) override;
};

#endif // AI_CAPTION_PLUGIN_CAPTION_SETTINGS_WIDGET_H
