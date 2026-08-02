#include <cassert>
#include <stdexcept>
#include <string>

#include "CaptionEngine.h"
#include "SherpaTOneCaptionEngine.h"

class FakeCaptionEngine final : public CaptionEngine {
public:
    bool queue_audio_data(const char *data, unsigned int data_size) override {
        CaptionResult result;
        result.final = true;
        result.caption_text.assign(data, data_size);

        std::lock_guard<std::recursive_mutex> lock(on_caption_cb_handle.mutex);
        if (on_caption_cb_handle.callback_fn)
            on_caption_cb_handle.callback_fn(result, false);
        return true;
    }
};

int main() {
    FakeCaptionEngine engine;
    std::string received;
    bool interrupted = true;
    engine.on_caption_cb_handle.set([&](const CaptionResult &result, bool was_interrupted) {
        received = result.caption_text;
        interrupted = was_interrupted;
    });

    assert(engine.queue_audio_data("test", 4));
    assert(received == "test");
    assert(!interrupted);

    LocalCaptionEngineSettings missing_model;
    missing_model.model_directory = "this-model-directory-does-not-exist";
    bool rejected_missing_model = false;
    try {
        SherpaTOneCaptionEngine unavailable(missing_model);
    } catch (const std::runtime_error &) {
        rejected_missing_model = true;
    }
    assert(rejected_missing_model);
    return 0;
}
