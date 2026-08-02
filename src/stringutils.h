#ifndef AI_CAPTION_PLUGIN_STRING_UTILS_H
#define AI_CAPTION_PLUGIN_STRING_UTILS_H

#include <QString>
#include <QStringList>
#include <QStringView>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

static void lstrip(std::string &text) {
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [](unsigned char character) {
        return !std::isspace(character);
    }));
}

static void split_into_lines(
        std::vector<std::string> &output_lines,
        const std::string &text,
        unsigned int maximum_line_length) {
    if (maximum_line_length == 0)
        return;

    const qsizetype maximum_length = static_cast<qsizetype>(maximum_line_length);
    const QStringList words = QString::fromStdString(text).simplified().split(' ', Qt::SkipEmptyParts);
    QString line;
    for (const QString &word : words) {
        const qsizetype joined_length = line.size() + (line.isEmpty() ? 0 : 1) + word.size();
        if (joined_length <= maximum_length) {
            if (!line.isEmpty())
                line.append(' ');
            line.append(word);
            continue;
        }

        if (!line.isEmpty()) {
            output_lines.push_back(line.toStdString());
            line.clear();
        }

        if (word.size() <= maximum_length) {
            line = word;
            continue;
        }

        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, word);
        const QStringView word_view(word);
        qsizetype grapheme_start = 0;
        while (finder.toNextBoundary() != -1) {
            const qsizetype grapheme_length = finder.position() - grapheme_start;
            if (!line.isEmpty() && line.size() + grapheme_length > maximum_length) {
                output_lines.push_back(line.toStdString());
                line.clear();
            }
            line.append(word_view.sliced(grapheme_start, grapheme_length));
            grapheme_start = finder.position();
        }
    }

    if (!line.isEmpty())
        output_lines.push_back(line.toStdString());
}

static void join_strings(
        const std::vector<std::string> &lines,
        const std::string &joiner,
        std::string &output) {
    std::size_t added_size = lines.empty() ? 0 : joiner.size() * (lines.size() - 1);
    for (const std::string &line : lines)
        added_size += line.size();
    output.reserve(output.size() + added_size);

    for (const std::string &line : lines) {
        if (!output.empty())
            output.append(joiner);
        output.append(line);
    }
}

#endif // AI_CAPTION_PLUGIN_STRING_UTILS_H
