#ifndef AI_CAPTION_PLUGIN_CAPTION_RESULT_HANDLER_H
#define AI_CAPTION_PLUGIN_CAPTION_RESULT_HANDLER_H

#include <CaptionResult.h>

#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using uint = unsigned int;

struct CaptionFormatSettings {
    uint caption_line_length;
    uint caption_line_count;
    bool caption_insert_newlines;
    bool caption_insert_punctuation;
    bool caption_timeout_enabled;
    double caption_timeout_seconds;

    bool operator==(const CaptionFormatSettings &rhs) const {
        return caption_line_length == rhs.caption_line_length &&
               caption_line_count == rhs.caption_line_count &&
               caption_insert_newlines == rhs.caption_insert_newlines &&
               caption_insert_punctuation == rhs.caption_insert_punctuation &&
               caption_timeout_enabled == rhs.caption_timeout_enabled &&
               caption_timeout_seconds == rhs.caption_timeout_seconds;
    }

    bool operator!=(const CaptionFormatSettings &rhs) const {
        return !(*this == rhs);
    }
};

struct OutputCaptionResult {
    CaptionResult caption_result;
    bool interrupted;
    std::string clean_caption_text;
    std::vector<std::string> output_lines;
    std::string output_line;

    OutputCaptionResult(const CaptionResult &caption_result, bool interrupted)
            : caption_result(caption_result), interrupted(interrupted) {}
};

class CaptionResultHandler {
public:
    explicit CaptionResultHandler(CaptionFormatSettings settings)
            : settings(std::move(settings)) {}

    std::shared_ptr<OutputCaptionResult> prepare_caption_output(
            const CaptionResult &caption_result,
            bool fillup_with_previous,
            bool insert_newlines,
            bool punctuation,
            uint line_length,
            uint targeted_line_count,
            bool interrupted,
            const std::deque<std::shared_ptr<OutputCaptionResult>> &result_history);

private:
    CaptionFormatSettings settings;
};

#endif // AI_CAPTION_PLUGIN_CAPTION_RESULT_HANDLER_H
