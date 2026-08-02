#ifndef AI_CAPTION_PLUGIN_OBS_FRONTEND_STATE_H
#define AI_CAPTION_PLUGIN_OBS_FRONTEND_STATE_H

#include <obs-frontend-api.h>
#include <util/bmem.h>

#include <string>

static std::string current_scene_collection_name() {
    std::string name;
    char *scene_collection_name = obs_frontend_get_current_scene_collection();
    if (scene_collection_name)
        name = scene_collection_name;
    bfree(scene_collection_name);
    return name;
}

static bool is_stream_live() {
    return obs_frontend_streaming_active();
}

#endif // AI_CAPTION_PLUGIN_OBS_FRONTEND_STATE_H
