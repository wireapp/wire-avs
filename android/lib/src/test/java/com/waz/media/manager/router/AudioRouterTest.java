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
import org.robolectric.shadow.api.Shadow;
import org.robolectric.shadows.AudioDeviceInfoBuilder;
import org.robolectric.util.ReflectionHelpers;
import static org.junit.Assert.*;
import static org.robolectric.Shadows.shadowOf;

@RunWith(RobolectricTestRunner.class)
@Config(sdk = {31, 33}, manifest = Config.NONE, shadows = AudioRouterTest.NativeCallbacks.class)
@LooperMode(LooperMode.Mode.PAUSED)
public class AudioRouterTest {
    private Context context;
    private AudioManager audioManager;

    @Before public void setup() {
        context = RuntimeEnvironment.getApplication();
        audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
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
        int connectedEvents;
        @Implementation protected void nativeBTDeviceConnected(boolean connected, long nativeMM) {
            if (connected) connectedEvents++;
        }
        @Implementation protected void nativeHeadsetConnected(boolean connected, long nativeMM) {}
        @Implementation protected void nativeUpdateRoute(int route, long nativeMM) {}
    }
}
