#include "CaptionSettingsWidget.h"

#include "uiutils.h"
#include "../data.h"
#include "../storage_utils.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QWidget *line_edit_with_button(QLineEdit *line_edit, QPushButton *button) {
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(line_edit, 1);
    layout->addWidget(button);
    return row;
}
}

CaptionSettingsWidget::CaptionSettingsWidget(
        const CaptionPluginSettings &latest_settings,
        const QString &overlay_url,
        const QString &designer_url)
        : QWidget(),
          current_settings(latest_settings),
          browser_designer_url(designer_url) {
    setWindowTitle(QStringLiteral("AI Captions"));
    setMinimumSize(660, 640);
    resize(720, 760);
    setStyleSheet(QStringLiteral(R"STYLE(
        QFrame#captionHero {
            background: #211a35;
            border: 1px solid #513b7d;
            border-radius: 14px;
        }
        QLabel#captionHeroTitle {
            color: #ffffff;
            font-family: "Segoe UI Variable Display", "Segoe UI";
            font-size: 22px;
            font-weight: 700;
        }
        QLabel#captionHeroText { color: #d8cfee; }
        QLabel#captionFlow {
            color: #f4efff;
            background: #33264e;
            border-radius: 8px;
            padding: 9px 12px;
            font-weight: 600;
        }
        QLabel#captionStateOn {
            color: #d9c8ff;
            font-weight: 600;
        }
        QGroupBox {
            font-weight: 650;
            margin-top: 12px;
            padding-top: 12px;
        }
        QLineEdit, QComboBox { min-height: 28px; }
        QPushButton { min-height: 30px; padding: 2px 12px; }
        QPushButton#captionPrimary {
            color: white;
            background: #7657d6;
            border: 1px solid #9378e8;
            border-radius: 6px;
            font-weight: 650;
        }
        QLabel#captionHint { color: #a9a3b3; }
        QLabel#captionSuccess { color: #8bd8aa; }
        QLabel#captionValidation {
            color: #ffd8d4;
            background: #4a2428;
            border: 1px solid #87434a;
            border-radius: 6px;
            padding: 8px 10px;
        }
    )STYLE"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(10);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget;
    auto *page_layout = new QVBoxLayout(page);
    page_layout->setContentsMargins(2, 2, 8, 2);
    page_layout->setSpacing(12);

    auto *hero = new QFrame;
    hero->setObjectName(QStringLiteral("captionHero"));
    auto *hero_layout = new QVBoxLayout(hero);
    hero_layout->setContentsMargins(18, 16, 18, 16);
    hero_layout->setSpacing(7);
    auto *hero_title = new QLabel(QStringLiteral("Локальные субтитры для эфира"));
    hero_title->setObjectName(QStringLiteral("captionHeroTitle"));
    auto *hero_text = new QLabel(QStringLiteral(
            "Звук остаётся на компьютере. Распознавание включается только тогда, когда субтитры кому-то нужны."));
    hero_text->setObjectName(QStringLiteral("captionHeroText"));
    hero_text->setWordWrap(true);
    auto *flow = new QLabel(QStringLiteral("ИСТОЧНИК  →  T-ONE  →  ЭКРАН · ФАЙЛ · СТРИМ"));
    flow->setObjectName(QStringLiteral("captionFlow"));
    hero_layout->addWidget(hero_title);
    hero_layout->addWidget(hero_text);
    hero_layout->addWidget(flow);
    page_layout->addWidget(hero);

    auto *activation_row = new QWidget;
    auto *activation_layout = new QHBoxLayout(activation_row);
    activation_layout->setContentsMargins(2, 0, 2, 0);
    enabled_checkbox = new QCheckBox(QStringLiteral("Субтитры включены"));
    activation_status = new QLabel;
    activation_status->setObjectName(QStringLiteral("captionStateOn"));
    activation_layout->addWidget(enabled_checkbox);
    activation_layout->addStretch();
    activation_layout->addWidget(activation_status);
    page_layout->addWidget(activation_row);

    auto *audio_group = new QGroupBox(QStringLiteral("1 · Откуда брать речь"));
    auto *audio_group_layout = new QVBoxLayout(audio_group);
    auto *audio_hint = new QLabel(QStringLiteral(
            "Лучше выбрать отдельный микрофон без музыки и системных звуков."));
    audio_hint->setObjectName(QStringLiteral("captionHint"));
    audio_hint->setWordWrap(true);
    audio_group_layout->addWidget(audio_hint);
    auto *audio_form = new QFormLayout;
    audio_group_layout->addLayout(audio_form);
    sources_combo = new QComboBox;
    caption_when_combo = new QComboBox;
    caption_when_combo->addItem(QStringLiteral("Когда источник слышен"), "own_source");
    caption_when_combo->addItem(QStringLiteral("Даже если источник скрыт или заглушён"), "always");
    caption_when_combo->addItem(QStringLiteral("Следовать другому источнику"), "other_mute_source");
    mute_source_combo = new QComboBox;
    mute_source_row = new QWidget;
    auto *mute_source_layout = new QHBoxLayout(mute_source_row);
    mute_source_layout->setContentsMargins(0, 0, 0, 0);
    mute_source_layout->addWidget(mute_source_combo);
    audio_form->addRow(QStringLiteral("Источник звука"), sources_combo);
    audio_form->addRow(QStringLiteral("Когда слушать"), caption_when_combo);
    audio_form->addRow(QStringLiteral("Источник-переключатель"), mute_source_row);
    page_layout->addWidget(audio_group);

    auto *browser_group = new QGroupBox(QStringLiteral("2 · Показывать на экране"));
    auto *browser_layout = new QVBoxLayout(browser_group);
    auto *browser_note = new QLabel(
            QStringLiteral("Добавьте URL как Browser Source. Открытый источник сам включает распознавание и сразу получает новые слова."));
    browser_note->setWordWrap(true);
    browser_layout->addWidget(browser_note);
    browser_url = new QLineEdit(overlay_url);
    browser_url->setReadOnly(true);
    copy_browser_button = new QPushButton(QStringLiteral("Копировать"));
    browser_layout->addWidget(line_edit_with_button(browser_url, copy_browser_button));
    auto *browser_actions = new QWidget;
    auto *browser_actions_layout = new QHBoxLayout(browser_actions);
    browser_actions_layout->setContentsMargins(0, 0, 0, 0);
    browser_designer_button = new QPushButton(QStringLiteral("Настроить внешний вид"));
    auto *preview_button = new QPushButton(QStringLiteral("Открыть живое превью"));
    copy_status = new QLabel;
    copy_status->setObjectName(QStringLiteral("captionSuccess"));
    browser_actions_layout->addWidget(browser_designer_button);
    browser_actions_layout->addWidget(preview_button);
    browser_actions_layout->addWidget(copy_status);
    browser_actions_layout->addStretch();
    browser_layout->addWidget(browser_actions);
    page_layout->addWidget(browser_group);

    auto *file_group = new QGroupBox(QStringLiteral("3 · Дополнительный вывод"));
    auto *file_layout = new QVBoxLayout(file_group);
    file_output_checkbox = new QCheckBox(QStringLiteral("Обновлять текстовый файл с текущими субтитрами"));
    file_layout->addWidget(file_output_checkbox);
    file_output_controls = new QWidget;
    auto *file_form = new QFormLayout(file_output_controls);
    file_form->setContentsMargins(22, 0, 0, 4);
    file_output_folder = new QLineEdit;
    auto *folder_button = new QPushButton(QStringLiteral("Выбрать…"));
    file_output_filename = new QLineEdit;
    file_output_filename->setPlaceholderText(QStringLiteral("captions.txt"));
    file_form->addRow(
            QStringLiteral("Папка"),
            line_edit_with_button(file_output_folder, folder_button));
    file_form->addRow(QStringLiteral("Имя файла"), file_output_filename);
    file_layout->addWidget(file_output_controls);

    stream_output_checkbox = new QCheckBox(
            QStringLiteral("Отправлять native CC в активный стрим — только латиница"));
    file_layout->addWidget(stream_output_checkbox);
    auto *stream_hint = new QLabel(QStringLiteral(
            "Кириллица в стандартном native CC OBS/Twitch не передаётся. Browser Source и файл поддерживают русский полностью."));
    stream_hint->setObjectName(QStringLiteral("captionHint"));
    stream_hint->setWordWrap(true);
    file_layout->addWidget(stream_hint);
    page_layout->addWidget(file_group);
    page_layout->addStretch();

    scroll->setWidget(page);
    outer->addWidget(scroll, 1);

    validation_label = new QLabel;
    validation_label->setObjectName(QStringLiteral("captionValidation"));
    validation_label->setWordWrap(true);
    validation_label->hide();
    outer->addWidget(validation_label);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Сохранить"));
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("captionPrimary"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    outer->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &CaptionSettingsWidget::accept_current_settings);
    connect(buttons, &QDialogButtonBox::rejected, this, &CaptionSettingsWidget::hide);
    connect(folder_button, &QPushButton::clicked, this, &CaptionSettingsWidget::choose_file_output_folder);
    connect(copy_browser_button, &QPushButton::clicked, this, &CaptionSettingsWidget::copy_browser_url);
    connect(browser_designer_button, &QPushButton::clicked, this, &CaptionSettingsWidget::open_browser_designer);
    connect(preview_button, &QPushButton::clicked, this, &CaptionSettingsWidget::preview_requested);
    connect(caption_when_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &CaptionSettingsWidget::update_source_controls);
    connect(sources_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &CaptionSettingsWidget::update_source_controls);
    connect(file_output_checkbox, &QCheckBox::toggled, this, &CaptionSettingsWidget::update_output_controls);
    connect(enabled_checkbox, &QCheckBox::toggled, this, &CaptionSettingsWidget::update_output_controls);

    update_ui();
    set_browser_urls(overlay_url, designer_url);
}

void CaptionSettingsWidget::populate_audio_sources() {
    const QString selected_source = QString::fromStdString(
            current_settings.source_cap_settings.caption_source_settings.caption_source_name);
    const QString selected_mute_source = QString::fromStdString(
            current_settings.source_cap_settings.caption_source_settings.mute_source_name);

    sources_combo->clear();
    mute_source_combo->clear();
    sources_combo->addItem(QStringLiteral("Не выбран"), QString());
    mute_source_combo->addItem(QStringLiteral("Не выбран"), QString());
    for (const string &source : get_audio_sources()) {
        const QString name = QString::fromStdString(source);
        sources_combo->addItem(name, name);
        mute_source_combo->addItem(name, name);
    }
    for (int index = 0; index < kObsAudioTrackCount; ++index) {
        sources_combo->addItem(
                QString::fromStdString(all_audio_output_capture_source_name(index)),
                QString::fromStdString(all_audio_output_capture_source_data(index)));
    }

    int source_index = sources_combo->findData(selected_source);
    sources_combo->setCurrentIndex(source_index >= 0 ? source_index : 0);
    int mute_index = mute_source_combo->findData(selected_mute_source);
    mute_source_combo->setCurrentIndex(mute_index >= 0 ? mute_index : 0);
}

void CaptionSettingsWidget::update_ui() {
    const SourceCaptionerSettings &source = current_settings.source_cap_settings;
    const CaptionSourceSettings &audio = source.caption_source_settings;
    populate_audio_sources();

    const string mute_mode = mute_setting_to_string(audio.mute_when);
    const int mute_mode_index = caption_when_combo->findData(QString::fromStdString(mute_mode));
    caption_when_combo->setCurrentIndex(mute_mode_index >= 0 ? mute_mode_index : 0);

    enabled_checkbox->setChecked(current_settings.enabled);
    stream_output_checkbox->setChecked(source.native_stream_output_enabled);
    file_output_checkbox->setChecked(source.file_output_settings.enabled);
    file_output_folder->setText(QString::fromStdString(source.file_output_settings.output_folder));
    file_output_filename->setText(QString::fromStdString(source.file_output_settings.filename_custom));
    validation_label->hide();
    copy_status->clear();
    update_source_controls();
    update_output_controls();
}

void CaptionSettingsWidget::update_source_controls() {
    const bool output_track = is_all_audio_output_capture_source_data(
            sources_combo->currentData().toString().toStdString());
    caption_when_combo->setEnabled(!output_track);
    mute_source_row->setVisible(
            !output_track && caption_when_combo->currentData().toString() == QStringLiteral("other_mute_source"));
}

void CaptionSettingsWidget::update_output_controls() {
    file_output_controls->setEnabled(file_output_checkbox->isChecked());
    activation_status->setText(
            enabled_checkbox->isChecked()
                    ? QStringLiteral("Готовы включаться по потребителю")
                    : QStringLiteral("Остановлены"));
}

void CaptionSettingsWidget::show_validation_error(
        const QString &message,
        QWidget *field) {
    validation_label->setText(message);
    validation_label->show();
    if (field)
        field->setFocus(Qt::OtherFocusReason);
}

void CaptionSettingsWidget::accept_current_settings() {
    current_settings.enabled = enabled_checkbox->isChecked();
    SourceCaptionerSettings &source = current_settings.source_cap_settings;
    CaptionSourceSettings &audio = source.caption_source_settings;
    audio.caption_source_name = sources_combo->currentData().toString().toStdString();
    audio.mute_source_name = mute_source_combo->currentData().toString().toStdString();
    audio.mute_when = string_to_mute_setting(
            caption_when_combo->currentData().toString().toStdString());

    source.native_stream_output_enabled = stream_output_checkbox->isChecked();
    FileOutputSettings &file = source.file_output_settings;
    file.enabled = file_output_checkbox->isChecked();
    file.output_folder = file_output_folder->text().trimmed().toStdString();
    const QString safe_filename = QFileInfo(file_output_filename->text().trimmed()).fileName();
    file.filename_custom = safe_filename.isEmpty() ? "captions.txt" : safe_filename.toStdString();

    if (current_settings.enabled && audio.caption_source_name.empty()) {
        show_validation_error(
                QStringLiteral("Выберите источник звука — без него распознавание не запустится."),
                sources_combo);
        return;
    }
    if (current_settings.enabled &&
        audio.mute_when == CAPTION_SOURCE_MUTE_TYPE_USE_OTHER_MUTE_SOURCE &&
        audio.mute_source_name.empty()) {
        show_validation_error(
                QStringLiteral("Выберите источник, по состоянию которого нужно включать распознавание."),
                mute_source_combo);
        return;
    }
    if (file.enabled && file.output_folder.empty()) {
        show_validation_error(
                QStringLiteral("Выберите папку для текстового файла или отключите вывод в файл."),
                file_output_folder);
        return;
    }
    if (file.enabled && !QFileInfo(QString::fromStdString(file.output_folder)).isDir()) {
        show_validation_error(
                QStringLiteral("Папка для субтитров не существует или недоступна."),
                file_output_folder);
        return;
    }

    validation_label->hide();
    emit settings_accepted(current_settings);
    hide();
}

void CaptionSettingsWidget::choose_file_output_folder() {
    const QString directory = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("Папка для субтитров"),
            file_output_folder->text(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!directory.isEmpty())
        file_output_folder->setText(directory);
}

void CaptionSettingsWidget::copy_browser_url() {
    QApplication::clipboard()->setText(browser_url->text());
    copy_status->setText(QStringLiteral("URL скопирован"));
    QTimer::singleShot(2200, copy_status, [this] { copy_status->clear(); });
}

void CaptionSettingsWidget::open_browser_designer() {
    if (!browser_designer_url.isEmpty())
        QDesktopServices::openUrl(QUrl(browser_designer_url));
}

void CaptionSettingsWidget::set_browser_urls(
        const QString &overlay_url,
        const QString &designer_url) {
    browser_url->setText(overlay_url);
    browser_designer_url = designer_url;
    const bool available = !overlay_url.isEmpty() && !designer_url.isEmpty();
    copy_browser_button->setEnabled(available);
    browser_designer_button->setEnabled(available);
    if (!available)
        copy_status->setText(QStringLiteral("Локальный браузерный источник не запустился"));
}

void CaptionSettingsWidget::set_settings(const CaptionPluginSettings &new_settings) {
    current_settings = new_settings;
    update_ui();
}

void CaptionSettingsWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    populate_audio_sources();
    update_source_controls();
    update_output_controls();
}
