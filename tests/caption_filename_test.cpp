#include "CaptionFileName.h"

#include <QDate>
#include <QDateTime>
#include <QTime>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}
}

int main() {
    const QDateTime timestamp(QDate(2026, 8, 3), QTime(14, 5, 9));
    require(
            format_caption_filename(
                    QStringLiteral("captions_%CCYY-%MM-%DD_%hh-%mm-%ss.txt"),
                    timestamp) == QStringLiteral("captions_2026-08-03_14-05-09.txt"),
            "Caption filename must support OBS-style date and time tokens");
    require(
            format_caption_filename(QStringLiteral("captions.txt"), timestamp) ==
                    QStringLiteral("captions.txt"),
            "A filename without tokens must remain unchanged");
    require(
            sanitize_caption_filename_template(QStringLiteral("../unsafe\\captions_%CCYY.txt")) ==
                    QStringLiteral("captions_%CCYY.txt"),
            "Caption filename templates must not escape the selected folder");
    require(
            format_caption_filename(QStringLiteral("captions_100%%.txt"), timestamp) ==
                    QStringLiteral("captions_100%.txt"),
            "A doubled percent sign must produce a literal percent sign");
    return 0;
}
