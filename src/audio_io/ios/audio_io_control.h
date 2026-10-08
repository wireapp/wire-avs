/*
 * Wire
 * Copyright (C) 2026 Wire Swiss GmbH
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <mutex>

namespace webrtc {

// Serializes the media-manager and WebRTC workers. Callbacks run under this
// lock and must not reenter the controller. Audio buffer callbacks do not use it.
// Control operations run off the main thread: device teardown can synchronously
// dispatch there. Keep the main-thread media-manager API asynchronous.
//
// Start may precede device creation. Once initialization begins, Start waits
// until setup finishes instead of observing a partly initialized AudioUnit.
class audio_io_control {
public:
    template <class Init, class Play, class Record>
    int initialize(Init init, Play play, Record record) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (ready_) return 0;
        int result = init();
        if (result < 0) return result;
        ready_ = true;
        if (want_play_) {
            result = play();
            if (result < 0) return result;
            want_play_ = false;
        }
        if (want_record_) {
            result = record();
            if (result < 0) return result;
            want_record_ = false;
        }
        return result;
    }

    template <class Start>
    int start_recording(Start start) {
        std::lock_guard<std::mutex> guard(mutex_);
        want_record_ = true;
        if (!ready_) return 0;
        int result = start();
        if (result == 0) want_record_ = false;
        return result;
    }

    template <class Start>
    int start_playout(Start start) {
        std::lock_guard<std::mutex> guard(mutex_);
        want_play_ = true;
        if (!ready_) return 0;
        int result = start();
        if (result == 0) want_play_ = false;
        return result;
    }

    template <class Stop>
    int stop_recording(Stop stop) {
        std::lock_guard<std::mutex> guard(mutex_);
        want_record_ = false;
        return stop();
    }

    template <class Stop>
    int stop_playout(Stop stop) {
        std::lock_guard<std::mutex> guard(mutex_);
        want_play_ = false;
        return stop();
    }

    template <class Reset>
    int reset(Reset reset_device) {
        std::lock_guard<std::mutex> guard(mutex_);
        ready_ = false;
        int result = reset_device();
        if (result == 0) {
            ready_ = true;
            want_record_ = false;
            want_play_ = false;
        }
        return result;
    }

    template <class Terminate>
    int terminate(Terminate terminate_device) {
        std::lock_guard<std::mutex> guard(mutex_);
        want_record_ = false;
        want_play_ = false;
        if (!ready_) return 0;
        ready_ = false;
        return terminate_device();
    }

private:
    std::mutex mutex_;
    bool ready_ = false;
    bool want_record_ = false;
    bool want_play_ = false;
};

} // namespace webrtc
