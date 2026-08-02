/******************************************************************************
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#ifndef AI_CAPTION_PLUGIN_CAPTION_FILE_NAME_H
#define AI_CAPTION_PLUGIN_CAPTION_FILE_NAME_H

#include <QDateTime>
#include <QString>

inline QString sanitize_caption_filename_template(QString value) {
    value = value.trimmed();
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    value = value.section(QLatin1Char('/'), -1);

    static const QString invalid_characters = QStringLiteral("<>:\"|?*");
    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar character = value.at(index);
        if (character.unicode() < 0x20 || invalid_characters.contains(character))
            value[index] = QLatin1Char('_');
    }
    while (value.endsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char(' ')))
        value.chop(1);
    if (value.isEmpty() || value == QStringLiteral(".") || value == QStringLiteral(".."))
        value = QStringLiteral("captions.txt");
    return value.left(240);
}

inline QString format_caption_filename(
        const QString &filename_template,
        const QDateTime &timestamp = QDateTime::currentDateTime()) {
    QString result = sanitize_caption_filename_template(filename_template);
    constexpr char16_t escaped_percent_code_point = 0xe000;
    const QChar escaped_percent(escaped_percent_code_point);
    result.replace(QStringLiteral("%%"), QString(1, escaped_percent));

    const struct {
        const char *token;
        const char *format;
    } replacements[] = {
            {"%CCYY", "yyyy"},
            {"%YY", "yy"},
            {"%MM", "MM"},
            {"%DD", "dd"},
            {"%hh", "HH"},
            {"%mm", "mm"},
            {"%ss", "ss"},
    };
    for (const auto &replacement : replacements) {
        result.replace(
                QString::fromLatin1(replacement.token),
                timestamp.toString(QString::fromLatin1(replacement.format)));
    }
    result.replace(escaped_percent, QLatin1Char('%'));
    return sanitize_caption_filename_template(result);
}

#endif // AI_CAPTION_PLUGIN_CAPTION_FILE_NAME_H
