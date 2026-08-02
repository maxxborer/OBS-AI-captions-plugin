#ifndef AI_CAPTION_PLUGIN_ENGLISH_TERM_REPLACEMENTS_H
#define AI_CAPTION_PLUGIN_ENGLISH_TERM_REPLACEMENTS_H

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringView>

#include <string>

inline std::string restore_common_english_terms(const std::string &input) {
    static const QRegularExpression recognized_term(
            QStringLiteral(
                    "(?<![\\p{L}\\p{N}_])"
                    "(стример бот|чат гпт|дискорд|ютуб|твич|стим|обс)"
                    "(?![\\p{L}\\p{N}_])"),
            QRegularExpression::CaseInsensitiveOption |
                    QRegularExpression::UseUnicodePropertiesOption);
    static const QHash<QString, QString> latin_terms{
            {QStringLiteral("обс"), QStringLiteral("OBS")},
            {QStringLiteral("твич"), QStringLiteral("Twitch")},
            {QStringLiteral("дискорд"), QStringLiteral("Discord")},
            {QStringLiteral("ютуб"), QStringLiteral("YouTube")},
            {QStringLiteral("стим"), QStringLiteral("Steam")},
            {QStringLiteral("чат гпт"), QStringLiteral("ChatGPT")},
            {QStringLiteral("стример бот"), QStringLiteral("Streamer.bot")},
    };

    const QString source = QString::fromStdString(input);
    QRegularExpressionMatchIterator matches = recognized_term.globalMatch(source);
    if (!matches.hasNext())
        return input;

    QString result;
    result.reserve(source.size());
    const QStringView source_view(source);
    qsizetype previous_end = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const qsizetype start = match.capturedStart(1);
        result.append(source_view.sliced(previous_end, start - previous_end));
        result.append(latin_terms.value(match.captured(1).toCaseFolded()));
        previous_end = match.capturedEnd(1);
    }
    result.append(source_view.sliced(previous_end));
    return result.toStdString();
}

#endif // AI_CAPTION_PLUGIN_ENGLISH_TERM_REPLACEMENTS_H
