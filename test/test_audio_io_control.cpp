/*
 * Wire
 * Copyright (C) 2026 Wire Swiss GmbH
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../src/audio_io/ios/audio_io_control.h"
#include <gtest/gtest.h>
#include <chrono>
#include <future>

using namespace std::chrono_literals;

namespace {
struct Device {
    webrtc::audio_io_control control;
    bool ready = false;
    bool recording = false;
    bool playing = false;
    int recording_starts = 0;
    int playout_starts = 0;

    int record() {
        EXPECT_TRUE(ready);
        if (!recording) ++recording_starts;
        recording = true;
        return 0;
    }
    int play() {
        EXPECT_TRUE(ready);
        if (!playing) ++playout_starts;
        playing = true;
        return 0;
    }
    int initialize() {
        return control.initialize([&] { ready = true; return 0; },
                                  [&] { return play(); }, [&] { return record(); });
    }
    int start_recording() { return control.start_recording([&] { return record(); }); }
    int stop_recording() {
        return control.stop_recording([&] { recording = false; return 0; });
    }
};
}

TEST(audio_io_control, replays_start_before_initialization) {
    Device device;
    EXPECT_EQ(0, device.start_recording());
    EXPECT_EQ(0, device.control.start_playout([&] { return device.play(); }));
    EXPECT_EQ(0, device.recording_starts);
    EXPECT_EQ(0, device.initialize());
    EXPECT_EQ(1, device.recording_starts);
    EXPECT_EQ(1, device.playout_starts);
}

TEST(audio_io_control, start_during_initialization_waits_for_device) {
    Device device;
    std::promise<void> initializing, finish_initializing, requesting;
    auto release = finish_initializing.get_future();
    auto init = std::async(std::launch::async, [&] {
        return device.control.initialize([&] {
            initializing.set_value();
            release.wait();
            device.ready = true;
            return 0;
        }, [&] { return device.play(); }, [&] { return device.record(); });
    });
    initializing.get_future().wait();
    auto start = std::async(std::launch::async, [&] {
        requesting.set_value();
        return device.start_recording();
    });
    requesting.get_future().wait();
    // A start cannot return success while initialization is still blocked.
    EXPECT_EQ(std::future_status::timeout, start.wait_for(50ms));
    finish_initializing.set_value();
    EXPECT_EQ(0, init.get());
    EXPECT_EQ(0, start.get());
    EXPECT_TRUE(device.recording);
    EXPECT_EQ(1, device.recording_starts);
}

TEST(audio_io_control, stop_cancels_start_before_initialization) {
    Device device;
    device.start_recording();
    device.control.start_playout([&] { return device.play(); });
    device.stop_recording();
    device.control.stop_playout([&] { device.playing = false; return 0; });
    device.initialize();
    EXPECT_FALSE(device.recording);
    EXPECT_FALSE(device.playing);
    EXPECT_EQ(0, device.recording_starts);
}

TEST(audio_io_control, failed_initialization_does_not_publish_readiness) {
    Device device;
    device.start_recording();
    EXPECT_EQ(-1, device.control.initialize([] { return -1; },
                   [&] { return device.play(); }, [&] { return device.record(); }));
    EXPECT_EQ(0, device.start_recording());
    EXPECT_EQ(0, device.recording_starts);
    EXPECT_EQ(0, device.initialize());
    EXPECT_EQ(1, device.recording_starts);
}

TEST(audio_io_control, stop_waits_for_initialization_replay) {
    Device device;
    device.start_recording();
    std::promise<void> initializing, finish_initializing, stopping;
    auto release = finish_initializing.get_future();
    auto init = std::async(std::launch::async, [&] {
        return device.control.initialize([&] {
            initializing.set_value();
            release.wait();
            device.ready = true;
            return 0;
        }, [&] { return device.play(); }, [&] { return device.record(); });
    });
    initializing.get_future().wait();
    auto stop = std::async(std::launch::async, [&] {
        stopping.set_value();
        return device.stop_recording();
    });
    stopping.get_future().wait();
    EXPECT_EQ(std::future_status::timeout, stop.wait_for(50ms));
    finish_initializing.set_value();
    EXPECT_EQ(0, init.get());
    EXPECT_EQ(0, stop.get());
    EXPECT_FALSE(device.recording);
}

TEST(audio_io_control, stop_waits_for_reset_then_stops_capture) {
    Device device;
    device.initialize();
    device.start_recording();
    std::promise<void> resetting, finish_reset, stopping;
    auto release = finish_reset.get_future();
    auto reset = std::async(std::launch::async, [&] {
        return device.control.reset([&] {
            resetting.set_value();
            release.wait();
            device.recording = false;
            return device.record();
        });
    });
    resetting.get_future().wait();
    auto stop = std::async(std::launch::async, [&] {
        stopping.set_value();
        return device.stop_recording();
    });
    stopping.get_future().wait();
    EXPECT_EQ(std::future_status::timeout, stop.wait_for(50ms));
    finish_reset.set_value();
    EXPECT_EQ(0, reset.get());
    EXPECT_EQ(0, stop.get());
    EXPECT_FALSE(device.recording);
}

TEST(audio_io_control, terminate_cancels_pending_start) {
    Device device;
    device.start_recording();
    device.control.terminate([] { return 0; });
    device.initialize();
    EXPECT_FALSE(device.recording);
}

TEST(audio_io_control, terminate_requires_new_initialization) {
    Device device;
    device.initialize();
    device.start_recording();
    device.stop_recording();
    device.control.terminate([&] { device.ready = false; return 0; });
    device.start_recording();
    EXPECT_EQ(1, device.recording_starts);
    device.initialize();
    EXPECT_EQ(2, device.recording_starts);
}

TEST(audio_io_control, failed_reset_does_not_publish_readiness) {
    Device device;
    device.initialize();
    EXPECT_EQ(-1, device.control.reset([&] { device.ready = false; return -1; }));
    device.start_recording();
    EXPECT_EQ(0, device.recording_starts);
    device.initialize();
    EXPECT_EQ(1, device.recording_starts);
}
