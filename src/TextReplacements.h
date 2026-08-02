/******************************************************************************
Copyright (C) 2020 by <rat.with.a.compiler@gmail.com>
Copyright (C) 2026 AI Caption Plugin contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#ifndef AI_CAPTION_PLUGIN_TEXT_REPLACEMENTS_H
#define AI_CAPTION_PLUGIN_TEXT_REPLACEMENTS_H

#include <QRegularExpression>
#include <QString>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

constexpr std::size_t kMaximumTextReplacements = 100;
constexpr qsizetype kMaximumReplacementCharacters = 256;

struct TextReplacement {
    std::string type = "whole_word_case_insensitive";
    std::string from;
    std::string to;

    bool operator==(const TextReplacement &rhs) const {
        return type == rhs.type && from == rhs.from && to == rhs.to;
    }

    bool operator!=(const TextReplacement &rhs) const {
        return !(*this == rhs);
    }
};

inline bool is_valid_text_replacement_type(const std::string &type) {
    return type == "whole_word_case_insensitive" ||
           type == "text_case_insensitive" ||
           type == "text_case_sensitive" ||
           type == "regex_case_insensitive" ||
           type == "regex_case_sensitive";
}

inline bool is_regex_text_replacement_type(const std::string &type) {
    return type == "regex_case_insensitive" || type == "regex_case_sensitive";
}

inline bool text_replacement_is_valid(const TextReplacement &replacement) {
    if (!is_valid_text_replacement_type(replacement.type))
        return false;
    const QString from = QString::fromStdString(replacement.from);
    const QString to = QString::fromStdString(replacement.to);
    if (from.trimmed().isEmpty() || from.size() > kMaximumReplacementCharacters ||
        to.size() > kMaximumReplacementCharacters) {
        return false;
    }
    if (!is_regex_text_replacement_type(replacement.type))
        return true;

    QRegularExpression::PatternOptions options =
            QRegularExpression::UseUnicodePropertiesOption;
    if (replacement.type == "regex_case_insensitive")
        options |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(from, options).isValid();
}

inline std::vector<TextReplacement> normalized_text_replacements(
        const std::vector<TextReplacement> &input) {
    std::vector<TextReplacement> result;
    result.reserve(std::min(input.size(), kMaximumTextReplacements));
    for (const TextReplacement &replacement : input) {
        if (result.size() >= kMaximumTextReplacements)
            break;
        if (!text_replacement_is_valid(replacement))
            continue;
        TextReplacement normalized = replacement;
        normalized.from = QString::fromStdString(normalized.from).trimmed().toStdString();
        result.push_back(std::move(normalized));
    }
    return result;
}

class TextReplacer {
public:
    explicit TextReplacer(const std::vector<TextReplacement> &replacements) {
        for (const TextReplacement &replacement : normalized_text_replacements(replacements)) {
            Rule rule;
            rule.type = replacement.type;
            rule.from = QString::fromStdString(replacement.from);
            rule.to = QString::fromStdString(replacement.to);

            if (replacement.type == "whole_word_case_insensitive") {
                rule.expression = QRegularExpression(
                        QStringLiteral("(?<![\\p{L}\\p{N}_])") +
                                QRegularExpression::escape(rule.from) +
                                QStringLiteral("(?![\\p{L}\\p{N}_])"),
                        QRegularExpression::CaseInsensitiveOption |
                                QRegularExpression::UseUnicodePropertiesOption);
            } else if (is_regex_text_replacement_type(replacement.type)) {
                QRegularExpression::PatternOptions options =
                        QRegularExpression::UseUnicodePropertiesOption;
                if (replacement.type == "regex_case_insensitive")
                    options |= QRegularExpression::CaseInsensitiveOption;
                rule.expression = QRegularExpression(rule.from, options);
            }
            rules.push_back(std::move(rule));
        }
    }

    std::string replace(const std::string &input) const {
        QString result = QString::fromStdString(input);
        for (const Rule &rule : rules) {
            if (rule.type == "text_case_insensitive")
                result.replace(rule.from, rule.to, Qt::CaseInsensitive);
            else if (rule.type == "text_case_sensitive")
                result.replace(rule.from, rule.to, Qt::CaseSensitive);
            else
                result.replace(rule.expression, rule.to);
        }
        return result.toStdString();
    }

private:
    struct Rule {
        std::string type;
        QString from;
        QString to;
        QRegularExpression expression;
    };
    std::vector<Rule> rules;
};

#endif // AI_CAPTION_PLUGIN_TEXT_REPLACEMENTS_H
