#include "MainCaptionWidget.h"

#include "uiutils.h"
#include "../obs_frontend_state.h"

#include <QHideEvent>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

MainCaptionWidget::MainCaptionWidget(CaptionPluginManager &plugin_manager)
        : QWidget(),
          plugin_manager(plugin_manager),
          caption_settings_widget(plugin_manager.plugin_settings) {
    setWindowTitle(QStringLiteral("AI Captions — локальное превью"));
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel(QStringLiteral(
            "Это диагностическое превью. Пока окно открыто, оно считается потребителем субтитров."));
    note->setWordWrap(true);
    layout->addWidget(note);

    caption_text = new QPlainTextEdit;
    caption_text->setReadOnly(true);
    caption_text->setMinimumHeight(120);
    QFont caption_font = caption_text->font();
    caption_font.setPointSize(15);
    caption_text->setFont(caption_font);
    layout->addWidget(caption_text);

    status_text = new QLabel(QStringLiteral("Ожидание речи"));
    status_text->setWordWrap(true);
    layout->addWidget(status_text);

    auto *settings_button = new QPushButton(QStringLiteral("Настройки"));
    layout->addWidget(settings_button);

    connect(settings_button, &QPushButton::clicked, this, &MainCaptionWidget::show_settings_dialog);
    connect(
            &caption_settings_widget,
            &CaptionSettingsWidget::settings_accepted,
            this,
            &MainCaptionWidget::accept_widget_settings);
    connect(
            &caption_settings_widget,
            &CaptionSettingsWidget::preview_requested,
            this,
            &MainCaptionWidget::show_self);
    connect(
            &plugin_manager.source_captioner,
            &SourceCaptioner::caption_result_received,
            this,
            &MainCaptionWidget::handle_caption_data,
            Qt::QueuedConnection);
    connect(
            &plugin_manager.source_captioner,
            &SourceCaptioner::source_capture_status_changed,
            this,
            &MainCaptionWidget::handle_source_capture_status_change,
            Qt::QueuedConnection);
    connect(
            &plugin_manager,
            &CaptionPluginManager::settings_changed,
            &caption_settings_widget,
            &CaptionSettingsWidget::set_settings);
}

void MainCaptionWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    caption_text->clear();
    status_text->setText(
            plugin_manager.plugin_settings.enabled
                    ? QStringLiteral("Запуск локального распознавания…")
                    : QStringLiteral("Субтитры выключены"));
    external_state_changed();
}

void MainCaptionWidget::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    external_state_changed();
}

void MainCaptionWidget::show_self() {
    show();
    raise();
    activateWindow();
}

void MainCaptionWidget::show_settings_dialog() {
    caption_settings_widget.set_settings(plugin_manager.plugin_settings);
    caption_settings_widget.show();
    caption_settings_widget.raise();
    caption_settings_widget.activateWindow();
}

void MainCaptionWidget::accept_widget_settings(CaptionPluginSettings new_settings) {
    plugin_manager.update_settings(new_settings);
}

void MainCaptionWidget::handle_caption_data(
        std::shared_ptr<OutputCaptionResult> caption_result,
        bool cleared) {
    if (!isVisible())
        return;
    if (cleared || !caption_result) {
        caption_text->clear();
        return;
    }
    caption_text->setPlainText(QString::fromStdString(caption_result->output_line));
}

void MainCaptionWidget::external_state_changed() {
    plugin_manager.external_state_changed(
            is_stream_live(),
            isVisible(),
            current_scene_collection_name());
}

void MainCaptionWidget::scene_collection_changed() {
    caption_settings_widget.hide();
    hide();
    external_state_changed();
}

void MainCaptionWidget::stream_started_event() {
    external_state_changed();
}

void MainCaptionWidget::stream_stopped_event() {
    external_state_changed();
}

void MainCaptionWidget::stop_captioning() {
    plugin_manager.source_captioner.stop_caption_stream(false);
}

void MainCaptionWidget::handle_source_capture_status_change(
        std::shared_ptr<SourceCaptionerStatus> status) {
    if (!status)
        return;

    std::string text;
    captioning_status_string(
            plugin_manager.plugin_settings.enabled,
            plugin_manager.captioning_state(),
            *status,
            text);
    status_text->setText(QString::fromStdString(text));
}

MainCaptionWidget::~MainCaptionWidget() {
    plugin_manager.source_captioner.stop_caption_stream(false);
}
