/******************************************************************************
Copyright (C) 2019 by <rat.with.a.compiler@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*******************************************************************************/

#ifndef AI_CAPTION_PLUGIN_THREAD_SAFE_CALLBACK_H
#define AI_CAPTION_PLUGIN_THREAD_SAFE_CALLBACK_H

#include <mutex>
#include <utility>

template<typename T>
class ThreadSafeCallback {
public:
    T callback_fn;
    std::recursive_mutex mutex;

    ThreadSafeCallback() = default;

    explicit ThreadSafeCallback(T callback_fn)
            : callback_fn(std::move(callback_fn)) {}

    void clear() {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        callback_fn = nullptr;
    }

    void set(T new_callback_fn) {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        callback_fn = std::move(new_callback_fn);
    }

    ~ThreadSafeCallback() {
        clear();
    }
};

#endif // AI_CAPTION_PLUGIN_THREAD_SAFE_CALLBACK_H
