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
import android.os.Looper;
import com.waz.media.manager.MediaManager;
import java.util.Arrays;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;
import org.robolectric.annotation.LooperMode;
import org.robolectric.annotation.RealObject;
import org.robolectric.shadow.api.Shadow;
import org.robolectric.shadows.AudioDeviceInfoBuilder;
import org.robolectric.util.ReflectionHelpers;
import static org.junit.Assert.*;
import static org.robolectric.Shadows.shadowOf;

@RunWith(RobolectricTestRunner.class)
@Config(sdk = {31, 33}, manifest = Config.NONE, shadows = {AudioRouterTest.NativeCallbacks.class, AudioRouterTest.MediaManagerWithoutJni.class})
@LooperMode(LooperMode.Mode.PAUSED)
public class AudioRouterTest {
    private Context context;
    private AudioManager audioManager;
    private MediaManager mediaManager;

    @Before public void setup() {
        context = RuntimeEnvironment.getApplication();
        audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        mediaManager = MediaManager.getInstance(context);
        mediaManager.turnLoudSpeakerOff();
    }

    @Test public void retainsBleCallDevice() {
        AudioDeviceInfo ble = device(AudioDeviceInfo.TYPE_BLE_HEADSET, 3);
        available(ble);
        AudioRouter router = new AudioRouter(context, 0);
        assertEquals(ble.getId(), selectedBluetooth(router).getId());
        assertEquals(1, callbacks(router).connectedEvents);
    }

    @Test public void switchesFromScoToBleWithoutNullDevice() {
        AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        AudioDeviceInfo ble = device(AudioDeviceInfo.TYPE_BLE_HEADSET, 4);
        available(sco);
        AudioRouter router = new AudioRouter(context, 0);
        available(ble);
        router.onAudioDevicesRemoved(new AudioDeviceInfo[] {sco});
        assertEquals(ble.getId(), selectedBluetooth(router).getId());
        assertEquals(2, callbacks(router).connectedEvents);
    }

    @Test public void doesNotAdvertiseA2dpAsCallDevice() {
        available(device(AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, 3));
        AudioRouter router = new AudioRouter(context, 0);
        assertNull(selectedBluetooth(router));
        assertEquals(0, callbacks(router).connectedEvents);
    }

    @Test public void refreshedDeviceWithSameIdDoesNotDuplicateConnectionEvent() {
        AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        available(sco);
        AudioRouter router = new AudioRouter(context, 0);
        AudioDeviceInfo refreshed = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        available(refreshed);
        router.onAudioDevicesAdded(new AudioDeviceInfo[] {refreshed});
        assertSame(refreshed, selectedBluetooth(router));
        assertEquals(1, callbacks(router).connectedEvents);
    }

    @Test public void nullCommunicationDeviceHasInvalidRoute() {
        available();
        AudioRouter router = new AudioRouter(context, 0);
        audioManager.clearCommunicationDevice();
        assertEquals(AudioRouter.ROUTE_INVALID, router.GetAudioRoute());
    }

    @Test public void wiredInsertionClearsSpeakerIntentAndKeepsWiredAudio() {
        assertHeadsetInsertion(AudioDeviceInfo.TYPE_WIRED_HEADSET);
    }

    @Test public void usbInsertionClearsSpeakerIntentAndKeepsUsbAudio() {
        assertHeadsetInsertion(AudioDeviceInfo.TYPE_USB_HEADSET);
    }

    @Test public void wiredUnplugDoesNotRestoreTheOldSpeakerChoice() {
        AudioRouter router = assertHeadsetInsertion(AudioDeviceInfo.TYPE_WIRED_HEADSET);
        AudioDeviceInfo wired = audioManager.getCommunicationDevice();
        available(device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1),
                device(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER, 2),
                device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3));
        router.onAudioDevicesRemoved(new AudioDeviceInfo[] {wired});
        idle();
        assertEquals(AudioRouter.ROUTE_BT, router.GetAudioRoute());
        assertFalse(mediaManager.isLoudSpeakerRequested());
    }

    @Test public void allowsANewSpeakerChoiceAfterWiredInsertion() {
        AudioRouter router = assertHeadsetInsertion(AudioDeviceInfo.TYPE_WIRED_HEADSET);
        AudioDeviceInfo wired = audioManager.getCommunicationDevice();
        mediaManager.turnLoudSpeakerOn();
        router.EnableSpeaker();
        idle();
        assertEquals(AudioRouter.ROUTE_SPEAKER, router.GetAudioRoute());
        router.onAudioDevicesAdded(new AudioDeviceInfo[] {wired});
        idle();
        assertTrue(mediaManager.isLoudSpeakerRequested());
        assertEquals(AudioRouter.ROUTE_SPEAKER, router.GetAudioRoute());
    }

    @Test public void startupKeepsTheSelectedHeadsetThroughAQueuedFallback() {
        AudioDeviceInfo selected = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 5);
        AudioDeviceInfo other = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 4);
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        available(earpiece, other, selected);
        audioManager.setMode(AudioManager.MODE_NORMAL);
        audioManager.setCommunicationDevice(selected);
        AudioRouter router = new AudioRouter(context, 0);
        router.OnStartingCall();
        audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
        audioManager.setCommunicationDevice(earpiece);
        idle();
        assertEquals(selected.getId(), audioManager.getCommunicationDevice().getId());
    }

    @Test public void modeChangeStartsTheInitiallyQueuedBluetoothRoute() {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        available(earpiece, sco);
        audioManager.setMode(AudioManager.MODE_NORMAL);
        audioManager.setCommunicationDevice(earpiece);
        AudioRouter router = new AudioRouter(context, 0);
        router.OnStartingCall();
        idle();
        assertEquals(earpiece.getId(), audioManager.getCommunicationDevice().getId());
        audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
        idle();
        assertEquals(sco.getId(), audioManager.getCommunicationDevice().getId());
    }

    @Test public void initialWiredDevicesDoNotCreateMediaManagerRecursively() {
        ReflectionHelpers.setStaticField(MediaManager.class, "_instance", null);
        available(device(AudioDeviceInfo.TYPE_WIRED_HEADSET, 5));
        new AudioRouter(context, 0);
        assertNull(ReflectionHelpers.getStaticField(MediaManager.class, "_instance"));
    }

    private AudioRouter assertHeadsetInsertion(int type) {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        AudioDeviceInfo speaker = device(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER, 2);
        AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        AudioDeviceInfo wired = device(type, 5);
        available(earpiece, speaker, sco);
        audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
        audioManager.setCommunicationDevice(sco);
        AudioRouter router = new AudioRouter(context, 0);
        router.OnStartingCall();
        idle();
        mediaManager.turnLoudSpeakerOn();
        router.EnableSpeaker();
        idle();
        assertEquals(AudioRouter.ROUTE_SPEAKER, router.GetAudioRoute());
        available(earpiece, speaker, sco, wired);
        router.onAudioDevicesAdded(new AudioDeviceInfo[] {wired});
        idle();
        assertFalse(mediaManager.isLoudSpeakerRequested());
        assertEquals(wired.getId(), audioManager.getCommunicationDevice().getId());
        assertEquals(AudioRouter.ROUTE_HEADSET, router.GetAudioRoute());
        return router;
    }

    private void idle() {
        shadowOf(Looper.getMainLooper()).idle();
    }

    private void available(AudioDeviceInfo... devices) {
        shadowOf(audioManager).setAvailableCommunicationDevices(Arrays.asList(devices));
    }

    private AudioDeviceInfo selectedBluetooth(AudioRouter router) {
        return ReflectionHelpers.getField(router, "bluetoothCurrentDevice");
    }

    private NativeCallbacks callbacks(AudioRouter router) {
        return Shadow.extract(router);
    }

    private AudioDeviceInfo device(int type, int id) {
        AudioDeviceInfo device = AudioDeviceInfoBuilder.newBuilder().setType(type).build();
        Object port = ReflectionHelpers.getField(device, "mPort");
        ReflectionHelpers.setField(ReflectionHelpers.getField(port, "mHandle"), "mId", id);
        return device;
    }

    @Implements(AudioRouter.class)
    public static class NativeCallbacks {
        @RealObject private AudioRouter router;
        int connectedEvents;
        @Implementation protected void nativeBTDeviceConnected(boolean connected, long nativeMM) {
            if (connected) connectedEvents++;
        }
        @Implementation protected void nativeHeadsetConnected(boolean connected, long nativeMM) {
            if (!ReflectionHelpers.<Boolean>getField(router, "_inCall")) return;
            // Model native MM_HEADSET_PLUGGED/UNPLUGGED routing, rather than swallowing it.
            if (connected) router.EnableHeadset();
            else if (ReflectionHelpers.getField(router, "bluetoothCurrentDevice") != null) router.EnableBTSco();
        }
        @Implementation protected void nativeUpdateRoute(int route, long nativeMM) {}
    }

    @Implements(MediaManager.class)
    public static class MediaManagerWithoutJni {
        @Implementation protected static void __staticInitializer__() {}
        @RealObject private MediaManager manager;
        @Implementation protected void __constructor__(Context context) {
            ReflectionHelpers.setField(manager, "ctx", context);
            ReflectionHelpers.setField(manager, "audioManager", context.getSystemService(Context.AUDIO_SERVICE));
        }
        @Implementation protected void EnableSpeaker(boolean enable) {}
    }
}
