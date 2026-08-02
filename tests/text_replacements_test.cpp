#include "TextReplacements.h"

#include <QFile>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

std::vector<TextReplacement> load_documented_profile() {
    QFile profile(QString::fromUtf8(PROFANITY_FILTER_PATH));
    require(profile.open(QIODevice::ReadOnly | QIODevice::Text),
            "The documented profanity profile must be readable");

    const QString document = QString::fromUtf8(profile.readAll());
    const QRegularExpression fenced_regex(
            QStringLiteral("```regex\\r?\\n(.*?)\\r?\\n```"),
            QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator matches = fenced_regex.globalMatch(document);
    std::vector<TextReplacement> replacements;
    while (matches.hasNext()) {
        const QString pattern = matches.next().captured(1);
        const std::size_t index = replacements.size();
        const QString replacement =
                index < 3 ? QStringLiteral("****")
                          : index == 3 ? QStringLiteral("**")
                                       : QStringLiteral("\\1***\\2");
        replacements.push_back(TextReplacement{
                "regex_case_insensitive",
                pattern.toStdString(),
                replacement.toStdString()});
    }
    return replacements;
}
}

int main() {
    const std::vector<TextReplacement> replacements = load_documented_profile();
    require(replacements.size() == 22,
            "The documented profanity profile must contain exactly 22 rules");

    for (std::size_t index = 0; index < replacements.size(); ++index) {
        const TextReplacement &replacement = replacements[index];
        if (!text_replacement_is_valid(replacement)) {
            const QString pattern = QString::fromStdString(replacement.from);
            const QRegularExpression expression(
                    pattern,
                    QRegularExpression::CaseInsensitiveOption |
                            QRegularExpression::UseUnicodePropertiesOption);
            std::cerr << "Invalid profanity rule " << (index + 1)
                      << ", length " << pattern.size() << ": "
                      << expression.errorString().toStdString() << std::endl;
            return 1;
        }
        require(QString::fromStdString(replacement.from).size() <=
                        kMaximumReplacementCharacters,
                "Every profanity pattern must fit the settings limit");
    }

    const TextReplacer replacer(replacements);
    const std::string input =
            "Блядь, я заебался, какого хуя этот распиздяй всё проебал. "
            "Сука, это полный пиздец. Fuck, this fucking asshole ruined everything. "
            "Blyat, nahuy. Пидор. Корабля, рубля, сабля, употреблять, мебель, ребёнок. "
            "Подстрахуй команду, открой педикюр рядом с Херсоном.";
    const std::string expected =
            "Б***ь, я з***я, какого х***я этот р***й всё п***л. "
            "С***а, это полный п***ц. F***k, this f***g a***e ruined everything. "
            "B***t, n***y. ****. Корабля, рубля, сабля, употреблять, мебель, ребёнок. "
            "Подстрахуй команду, открой педикюр рядом с Херсоном.";
    require(replacer.replace(input) == expected,
            "The profanity profile must mask target words and preserve safe words");

    return 0;
}
