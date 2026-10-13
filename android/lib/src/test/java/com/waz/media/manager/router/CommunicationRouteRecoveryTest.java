/*
 * Wire
 * Copyright (C) 2026 Wire Swiss GmbH
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/.
 */
package com.waz.media.manager.router;

import android.content.Context;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Looper;
import java.time.Duration;
import java.util.Arrays;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.LooperMode;
import org.robolectric.shadows.AudioDeviceInfoBuilder;
import org.robolectric.shadows.ShadowAudioManager;
import org.robolectric.util.ReflectionHelpers;
import static org.junit.Assert.assertEquals;
import static org.robolectric.Shadows.shadowOf;

@RunWith(RobolectricTestRunner.class)
@Config(sdk = {31, 33}, manifest = Config.NONE)
@LooperMode(LooperMode.Mode.PAUSED)
public class CommunicationRouteRecoveryTest {
    private AudioManager audioManager;
    private ShadowAudioManager shadowAudioManager;
    private CommunicationRouteRecovery recovery;
    private AudioDeviceInfo earpiece;
    private AudioDeviceInfo speaker;
    private AudioDeviceInfo headset;
    private boolean speakerRequested;

    @Before public void setup() {
        audioManager = (AudioManager) RuntimeEnvironment.getApplication().getSystemService(Context.AUDIO_SERVICE);
        shadowAudioManager = shadowOf(audioManager);
        recovery = new CommunicationRouteRecovery(audioManager, new Handler(Looper.getMainLooper()), () -> speakerRequested);
        earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1, "");
        speaker = device(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER, 2, "");
        headset = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3, "headset");
        audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
        available(earpiece, speaker, headset);
        audioManager.setCommunicationDevice(headset);
    }

    @Test public void restoresConnectedHeadsetAfterEarpieceFallback() {
        startCall();
        changeRoute(earpiece);
        assertRoute(headset);
    }

    @Test public void restoresConnectedHeadsetAfterNullRoute() {
        startCall();
        audioManager.clearCommunicationDevice();
        recovery.onRouteChanged(null);
        idle();
        assertRoute(headset);
    }

    @Test public void restoresSameHeadsetAfterReconnectWithNewId() {
        startCall();
        available(earpiece, speaker);
        recovery.onDevicesChanged();
        changeRoute(earpiece);
        assertRoute(earpiece);
        AudioDeviceInfo reconnected = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 4, "headset");
        available(earpiece, speaker, reconnected);
        recovery.onDevicesChanged();
        idle();
        assertRoute(reconnected);
    }

    @Test public void doesNotRestoreAnUnrelatedHeadset() {
        startCall();
        available(earpiece, speaker);
        recovery.onDevicesChanged();
        changeRoute(earpiece);
        available(earpiece, speaker, device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 4, "other"));
        recovery.onDevicesChanged();
        idle();
        assertRoute(earpiece);
    }

    @Test public void preservesExplicitSpeakerChoiceWhenDevicesChange() {
        startCall();
        speakerRequested = true;
        changeRoute(speaker);
        recovery.onDevicesChanged();
        idle();
        assertRoute(speaker);
    }

    @Test public void restoresHeadsetWhenSpeakerIsDisabled() {
        startCall();
        speakerRequested = true;
        changeRoute(speaker);
        speakerRequested = false;
        recovery.onRouteChanged(speaker);
        idle();
        assertRoute(headset);
    }

    @Test public void preservesSpeakerChoiceThroughDelayedCallbacks() {
        startCall();
        speakerRequested = true;
        recovery.onRouteChanged(headset);
        idle();
        recovery.onRouteChanged(speaker);
        changeRoute(headset);
        assertRoute(speaker);
    }

    @Test public void defersToWiredHeadsets() {
        startCall();
        available(earpiece, speaker, headset, device(AudioDeviceInfo.TYPE_WIRED_HEADSET, 4, ""));
        recovery.onDevicesChanged();
        changeRoute(earpiece);
        assertRoute(earpiece);
    }

    @Test public void restoresBleHeadsets() {
        AudioDeviceInfo ble = device(AudioDeviceInfo.TYPE_BLE_HEADSET, 4, "ble");
        available(earpiece, speaker, ble);
        audioManager.setCommunicationDevice(ble);
        startCall();
        changeRoute(earpiece);
        assertRoute(ble);
    }

    @Test public void neverSelectsA2dpMusicOutputForCalls() {
        available(earpiece, speaker, device(AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, 4, ""));
        audioManager.setCommunicationDevice(earpiece);
        startCall();
        assertRoute(earpiece);
    }

    @Test public void stopsRecoveryWhenCallEnds() {
        startCall();
        recovery.onCallStopped();
        idle();
        changeRoute(earpiece);
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMinutes(2));
        assertRoute(earpiece);
    }

    @Test public void doesNotRestoreAfterCommunicationModeEnds() {
        startCall();
        audioManager.setMode(AudioManager.MODE_NORMAL);
        changeRoute(earpiece);
        assertRoute(earpiece);
    }

    @Test public void boundsRejectedRequestsEvenWithNullCallbacks() {
        startCall();
        audioManager.setCommunicationDevice(earpiece);
        shadowAudioManager.lockCommunicationDevice(true);
        recovery.onRouteChanged(earpiece);
        idle();
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofSeconds(5));
        shadowAudioManager.lockCommunicationDevice(false);
        recovery.onRouteChanged(null);
        idle();
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMinutes(2));
        assertRoute(earpiece);
    }

    @Test public void cancelsRetryWhenCallEnds() {
        startCall();
        audioManager.setCommunicationDevice(earpiece);
        shadowAudioManager.lockCommunicationDevice(true);
        recovery.onRouteChanged(earpiece);
        idle();
        recovery.onCallStopped();
        idle();
        shadowAudioManager.lockCommunicationDevice(false);
        shadowOf(Looper.getMainLooper()).idleFor(Duration.ofMinutes(2));
        assertRoute(earpiece);
    }

    @Test public void remembersHeadsetBeforeCommunicationModeStarts() {
        AudioDeviceInfo other = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 4, "other");
        available(earpiece, speaker, other, headset);
        audioManager.setMode(AudioManager.MODE_NORMAL);
        startCall();
        audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
        changeRoute(earpiece);
        assertRoute(headset);
    }

    private void startCall() {
        recovery.onCallStarted();
        idle();
    }

    private void changeRoute(AudioDeviceInfo device) {
        audioManager.setCommunicationDevice(device);
        recovery.onRouteChanged(device);
        idle();
    }

    private void available(AudioDeviceInfo... devices) {
        shadowAudioManager.setAvailableCommunicationDevices(Arrays.asList(devices));
    }

    private void idle() {
        shadowOf(Looper.getMainLooper()).idle();
    }

    private void assertRoute(AudioDeviceInfo expected) {
        assertEquals(expected.getId(), audioManager.getCommunicationDevice().getId());
    }

    private AudioDeviceInfo device(int type, int id, String address) {
        AudioDeviceInfo device = AudioDeviceInfoBuilder.newBuilder().setType(type).build();
        Object port = ReflectionHelpers.getField(device, "mPort");
        ReflectionHelpers.setField(port, "mAddress", address);
        ReflectionHelpers.setField(ReflectionHelpers.getField(port, "mHandle"), "mId", id);
        return device;
    }
}
