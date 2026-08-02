#ifndef AI_CAPTION_PLUGIN_CAPTION_FILE_WRITER_H
#define AI_CAPTION_PLUGIN_CAPTION_FILE_WRITER_H

#include "SourceCaptioner.h"
#include "log.c"

#include <QDir>
#include <QFileInfo>

#include <exception>
#include <fstream>

static void file_output_writer_loop(
        shared_ptr<CaptionOutputControl<FileOutputSettings>> control) {
    const QFileInfo output_directory(QString::fromStdString(control->arg.output_folder));
    if (!output_directory.exists() || !output_directory.isDir()) {
        error_log("Caption file directory is unavailable: %s", control->arg.output_folder.c_str());
        return;
    }

    const QString output_path = QDir(output_directory.absoluteFilePath()).absoluteFilePath(
            QString::fromStdString(control->arg.filename_custom));
    CaptionOutput caption_output;
    std::string previous_line;
    bool has_previous_line = false;
    while (!control->stop) {
        control->caption_queue.wait_dequeue(caption_output);
        if (control->stop)
            break;
        if (!caption_output.output_result)
            continue;

        const std::string &line = caption_output.output_result->output_line;
        if (has_previous_line && line == previous_line)
            continue;

        try {
            std::ofstream output;
#if _WIN32
            output.open(output_path.toStdWString(), std::ios::binary | std::ios::trunc);
#else
            output.open(output_path.toStdString(), std::ios::binary | std::ios::trunc);
#endif
            if (output.fail()) {
                error_log("Unable to open caption file for writing");
                continue;
            }
            output << line;
            output.close();
            if (output.fail()) {
                error_log("Unable to finish writing caption file");
                continue;
            }
            previous_line = line;
            has_previous_line = true;
        } catch (const std::exception &error) {
            error_log("Unable to write caption file: %s", error.what());
        }
    }
}

#endif // AI_CAPTION_PLUGIN_CAPTION_FILE_WRITER_H
