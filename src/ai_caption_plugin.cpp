/******************************************************************************
Copyright (C) 2019 by <rat.with.a.compiler@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <obs.hpp>
#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include "SourceCaptioner.h"

#include <QAction>
#include <QMenu>
#include <QPointer>
#include <thread>

#include "ui/MainCaptionWidget.h"
#include "caption_settings_storage.h"
#include "CaptionPluginManager.h"

#include "log.c"

using namespace std;

MainCaptionWidget *main_caption_widget = nullptr;
CaptionPluginManager *plugin_manager = nullptr;
QPointer<QAction> tools_menu_action;

bool ui_setup_done = false;
bool callbacks_registered = false;

OBS_DECLARE_MODULE()


void finished_loading_event();

void stream_started_event();

void stream_stopped_event();

void setup_UI();

void closed_caption_tool_menu_clicked();

void obs_frontend_exiting();

void destroy_plugin_runtime();

void obs_frontend_scene_collection_changed();

void obs_frontend_scene_collection_changing();

static void obs_event(enum obs_frontend_event event, void *) {
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        finished_loading_event();
    } else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED) {
        stream_started_event();
    } else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
        stream_stopped_event();
    } else if (event == OBS_FRONTEND_EVENT_EXIT) {
        obs_frontend_exiting();
    } else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
        obs_frontend_scene_collection_changed();
    } else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING) {
        obs_frontend_scene_collection_changing();
    }
}


void closed_caption_tool_menu_clicked() {
    debug_log("caption menu button clicked ");
    if (main_caption_widget) {
        main_caption_widget->show_settings_dialog();
    }
}

void setup_UI() {
    if (ui_setup_done)
        return;

    debug_log("setup_UI()");
    tools_menu_action = static_cast<QAction *>(
            obs_frontend_add_tools_menu_qaction("AI Captions"));
    if (!tools_menu_action) {
        warn_log("Could not add AI Captions to the OBS Tools menu");
        return;
    }
    QObject::connect(
            tools_menu_action,
            &QAction::triggered,
            &closed_caption_tool_menu_clicked);

    ui_setup_done = true;
}

void finished_loading_event() {
    info_log("OBS_FRONTEND_EVENT_FINISHED_LOADING, plugin_manager loaded: %d, %s",
             plugin_manager != nullptr, qVersion());
    if (main_caption_widget) {
        main_caption_widget->external_state_changed();
#ifdef USE_DEVMODE
        main_caption_widget->show();
#endif
    }
}

void stream_started_event() {
    info_log("stream_started_event");
    if (main_caption_widget)
        main_caption_widget->stream_started_event();
}

void stream_stopped_event() {
    info_log("stream_stopped_event");
    if (main_caption_widget)
        main_caption_widget->stream_stopped_event();
}

void obs_frontend_scene_collection_changing() {
    info_log("obs_frontend_scene_collection_changing");
    if (main_caption_widget) {
        main_caption_widget->stop_captioning();
    }
}

void obs_frontend_scene_collection_changed() {
    info_log("obs_frontend_scene_collection_changed");
    if (main_caption_widget) {
        main_caption_widget->scene_collection_changed();
    }

}

void obs_frontend_exiting() {
    info_log("obs_frontend_exiting, stopping captioner");
    destroy_plugin_runtime();
    info_log("obs_frontend_exiting done");
}

void destroy_plugin_runtime() {
    if (main_caption_widget) {
        delete main_caption_widget;
        main_caption_widget = nullptr;
    }

    if (plugin_manager) {
        delete plugin_manager;
        plugin_manager = nullptr;
    }

    if (tools_menu_action) {
        if (auto *menu = qobject_cast<QMenu *>(tools_menu_action->parent()))
            menu->removeAction(tools_menu_action);
        delete tools_menu_action;
        tools_menu_action = nullptr;
    }
    ui_setup_done = false;
}

static void save_or_load_event_callback(obs_data_t *save_data, bool saving, void *) {
    int tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    info_log("save_or_load_event_callback %d, %d", saving, tid);

    if (saving && plugin_manager) {
        save_CaptionPluginSettings(save_data, plugin_manager->plugin_settings);
    }

    if (!saving) {
        auto loaded_settings = load_CaptionPluginSettings(save_data);
        if (plugin_manager && main_caption_widget) {
            plugin_manager->update_settings(loaded_settings);
        } else if (plugin_manager || main_caption_widget) {
            error_log("only one of plugin_manager and main_caption_widget, wtf, %d %d",
                      plugin_manager != nullptr, main_caption_widget != nullptr);
        } else {
            plugin_manager = new CaptionPluginManager(loaded_settings);
            main_caption_widget = new MainCaptionWidget(*plugin_manager);
            setup_UI();
        }
    }

}


bool obs_module_load(void) {
    info_log("ai_caption_plugin %s obs_module_load %d", VERSION_STRING,
             (int) std::hash<std::thread::id>{}(std::this_thread::get_id()));
    qRegisterMetaType<std::string>();
    qRegisterMetaType<shared_ptr<OutputCaptionResult>>();
    qRegisterMetaType<CaptionResult>();
    qRegisterMetaType<std::shared_ptr<SourceCaptionerStatus>>();

    obs_frontend_add_event_callback(obs_event, nullptr);
    obs_frontend_add_save_callback(save_or_load_event_callback, nullptr);
    callbacks_registered = true;
    return true;
}

void obs_module_post_load(void) {
    info_log("ai_caption_plugin %s obs_module_post_load", VERSION_STRING);
}

void obs_module_unload(void) {
    info_log("ai_caption_plugin %s obs_module_unload", VERSION_STRING);
    if (callbacks_registered) {
        obs_frontend_remove_save_callback(save_or_load_event_callback, nullptr);
        obs_frontend_remove_event_callback(obs_event, nullptr);
        callbacks_registered = false;
    }
    destroy_plugin_runtime();
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Provides offline Russian captions from a local speech recognition model";
}

MODULE_EXPORT const char *obs_module_name(void)
{
    return "AI Caption Plugin";
}

