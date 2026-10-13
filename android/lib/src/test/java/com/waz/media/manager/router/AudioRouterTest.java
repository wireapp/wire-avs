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

/**
 * The native media manager decides the call route. AudioRouter reports Android's
 * route and device changes to it, and applies the route it asks for.
 */
@RunWith(RobolectricTestRunner.class)
@Config(sdk = {31, 33}, manifest = Config.NONE, shadows = {AudioRouterTest.NativeRouter.class})
@LooperMode(LooperMode.Mode.PAUSED)
public class AudioRouterTest {
    private Context context;
    private AudioManager audioManager;

    @Before public void setup() {
        context = RuntimeEnvironment.getApplication();
        audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
    }

    @Test public void reportsBleHeadsetAsCallDevice() {
        AudioDeviceInfo ble = device(AudioDeviceInfo.TYPE_BLE_HEADSET, 3);
        available(ble);
        AudioRouter router = new AudioRouter(context, 0);
        assertEquals(1, nativeRouter(router).connectedEvents);
        assertBluetoothRoute(router, ble);
    }

    @Test public void switchesFromScoToBleWithoutNullDevice() {
        AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        AudioDeviceInfo ble = device(AudioDeviceInfo.TYPE_BLE_HEADSET, 4);
        available(sco);
        AudioRouter router = new AudioRouter(context, 0);
        available(ble);
        router.onAudioDevicesRemoved(new AudioDeviceInfo[] {sco});
        assertEquals(2, nativeRouter(router).connectedEvents);
        assertEquals(0, nativeRouter(router).disconnectedEvents);
        assertBluetoothRoute(router, ble);
    }

    @Test public void doesNotAdvertiseA2dpAsCallDevice() {
        available(device(AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, 3));
        AudioRouter router = new AudioRouter(context, 0);
        assertEquals(0, nativeRouter(router).connectedEvents);
        router.OnStartingCall();
        assertEquals(-1, router.EnableBTSco());
    }

    @Test public void refreshedDeviceWithSameIdDoesNotDuplicateConnectionEvent() {
        AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        available(sco);
        AudioRouter router = new AudioRouter(context, 0);
        AudioDeviceInfo refreshed = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        available(refreshed);
        router.onAudioDevicesAdded(new AudioDeviceInfo[] {refreshed});
        assertEquals(1, nativeRouter(router).connectedEvents);
        router.OnStartingCall();
        router.EnableBTSco();
        assertSame(refreshed, audioManager.getCommunicationDevice());
    }

    @Test public void nullCommunicationDeviceHasInvalidRoute() {
        available();
        AudioRouter router = new AudioRouter(context, 0);
        audioManager.clearCommunicationDevice();
        assertEquals(AudioRouter.ROUTE_INVALID, router.GetAudioRoute());
    }

    @Test public void usbHeadsetIsReportedAndSelectedAsHeadset() {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        AudioDeviceInfo usb = device(AudioDeviceInfo.TYPE_USB_HEADSET, 5);
        available(earpiece, usb);
        AudioRouter router = new AudioRouter(context, 0);
        assertEquals(1, nativeRouter(router).headsetEvents);
        assertEquals(1, router.EnableHeadset());
        assertEquals(usb.getId(), audioManager.getCommunicationDevice().getId());
        assertEquals(AudioRouter.ROUTE_HEADSET, router.GetAudioRoute());
    }

    @Test public void otherCallOutputsAreReportedAsExternalRoutes() {
        available();
        AudioRouter router = new AudioRouter(context, 0);
        int[] wired = {AudioDeviceInfo.TYPE_WIRED_HEADPHONES, AudioDeviceInfo.TYPE_USB_DEVICE,
                AudioDeviceInfo.TYPE_LINE_ANALOG, AudioDeviceInfo.TYPE_HDMI, AudioDeviceInfo.TYPE_AUX_LINE};
        for (int type : wired) {
            audioManager.setCommunicationDevice(device(type, 5));
            assertEquals(AudioRouter.ROUTE_HEADSET, router.GetAudioRoute());
        }
        audioManager.setCommunicationDevice(device(AudioDeviceInfo.TYPE_HEARING_AID, 6));
        assertEquals(AudioRouter.ROUTE_BT, router.GetAudioRoute());
    }

    @Test public void routeRequestsReportWhetherAndroidAcceptedThem() {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        AudioDeviceInfo speaker = device(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER, 2);
        available(earpiece, speaker);
        AudioRouter router = new AudioRouter(context, 0);

        // Accepted requests are applied asynchronously and reported when done
        assertEquals(1, router.EnableSpeaker());
        assertEquals(speaker.getId(), audioManager.getCommunicationDevice().getId());
        assertEquals(1, router.EnableEarpiece());
        assertEquals(earpiece.getId(), audioManager.getCommunicationDevice().getId());
        assertEquals(-1, router.EnableHeadset());

        shadowOf(audioManager).lockCommunicationDevice(true);
        assertEquals(-1, router.EnableSpeaker());
        assertEquals(-1, router.EnableEarpiece());
    }

    @Test public void reportsRouteChangesWithoutReroutingItself() {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        available(earpiece, sco);
        audioManager.setCommunicationDevice(sco);
        AudioRouter router = new AudioRouter(context, 0);
        router.OnStartingCall();
        idle();
        int reports = nativeRouter(router).routeChanges;

        fallBackTo(earpiece);

        assertEquals(reports + 1, nativeRouter(router).routeChanges);
        assertEquals(earpiece.getId(), audioManager.getCommunicationDevice().getId());
    }

    @Test public void callStartLeavesTheRouteToTheNativeRouter() {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        available(earpiece, sco);
        audioManager.setCommunicationDevice(earpiece);
        AudioRouter router = new AudioRouter(context, 0);
        idle();
        int reports = nativeRouter(router).deviceChanges;

        router.OnStartingCall();
        idle();

        assertEquals(reports + 1, nativeRouter(router).deviceChanges);
        assertEquals(earpiece.getId(), audioManager.getCommunicationDevice().getId());
    }

    @Test public void reportsDeviceChangesToTheNativeRouter() {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        available(earpiece);
        AudioRouter router = new AudioRouter(context, 0);
        idle();
        int reports = nativeRouter(router).deviceChanges;

        available(earpiece, sco);
        router.onAudioDevicesAdded(new AudioDeviceInfo[] {sco});
        available(earpiece);
        router.onAudioDevicesRemoved(new AudioDeviceInfo[] {sco});

        assertEquals(1, nativeRouter(router).connectedEvents);
        assertEquals(1, nativeRouter(router).disconnectedEvents);
        assertEquals(reports + 2, nativeRouter(router).deviceChanges);
    }

    @Test public void appliesTheNativeBluetoothRouteAfterAFallback() {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        AudioDeviceInfo sco = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3);
        available(earpiece, sco);
        audioManager.setCommunicationDevice(sco);
        AudioRouter router = new AudioRouter(context, 0);
        router.OnStartingCall();
        idle();
        // The native router answers the fallback with its Bluetooth route
        nativeRouter(router).onRouteChanged = () -> {
            if (router.GetAudioRoute() == AudioRouter.ROUTE_EARPIECE) router.EnableBTSco();
        };

        fallBackTo(earpiece);

        assertEquals(sco.getId(), audioManager.getCommunicationDevice().getId());
        assertEquals(AudioRouter.ROUTE_BT, router.GetAudioRoute());
    }

    @Test public void bluetoothRouteKeepsTheHeadsetTheCallStartedOn() {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        AudioDeviceInfo call = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 5);
        AudioDeviceInfo other = device(AudioDeviceInfo.TYPE_BLE_HEADSET, 6);
        available(earpiece, call, other);
        audioManager.setCommunicationDevice(call);
        AudioRouter router = new AudioRouter(context, 0);
        router.OnStartingCall();
        audioManager.setCommunicationDevice(earpiece);

        assertEquals(1, router.EnableBTSco());
        assertEquals(call.getId(), audioManager.getCommunicationDevice().getId());
    }

    @Test public void bluetoothRouteFindsTheCallHeadsetAfterItReconnects() {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        AudioDeviceInfo call = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 4, "00:11:22:33:44:55");
        AudioDeviceInfo other = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 5, "66:77:88:99:AA:BB");
        available(earpiece, call, other);
        audioManager.setCommunicationDevice(earpiece);
        AudioRouter router = new AudioRouter(context, 0);
        router.OnStartingCall();
        audioManager.setCommunicationDevice(call);
        shadowOf(audioManager).callOnCommunicationDeviceChangedListeners(call);
        idle();

        AudioDeviceInfo reconnected = device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 7, "00:11:22:33:44:55");
        available(earpiece, reconnected, other);
        router.onAudioDevicesAdded(new AudioDeviceInfo[] {reconnected});
        fallBackTo(earpiece);

        assertEquals(1, router.EnableBTSco());
        assertEquals(reconnected.getId(), audioManager.getCommunicationDevice().getId());
    }

    @Test public void bluetoothRouteFailsWithoutACallOrHeadset() {
        AudioDeviceInfo earpiece = device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1);
        available(earpiece, device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3));
        AudioRouter router = new AudioRouter(context, 0);
        assertEquals(-1, router.EnableBTSco());

        available(earpiece);
        router.onAudioDevicesRemoved(new AudioDeviceInfo[0]);
        router.OnStartingCall();
        assertEquals(-1, router.EnableBTSco());
    }

    @Test public void bluetoothRouteReportsRejectedRequests() {
        available(device(AudioDeviceInfo.TYPE_BUILTIN_EARPIECE, 1),
                device(AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 3));
        AudioRouter router = new AudioRouter(context, 0);
        router.OnStartingCall();
        shadowOf(audioManager).lockCommunicationDevice(true);

        assertEquals(-1, router.EnableBTSco());
    }

    private void fallBackTo(AudioDeviceInfo device) {
        audioManager.setCommunicationDevice(device);
        shadowOf(audioManager).callOnCommunicationDeviceChangedListeners(device);
        idle();
    }

    private void idle() {
        shadowOf(Looper.getMainLooper()).idle();
    }

    private void available(AudioDeviceInfo... devices) {
        shadowOf(audioManager).setAvailableCommunicationDevices(Arrays.asList(devices));
    }

    private void assertBluetoothRoute(AudioRouter router, AudioDeviceInfo device) {
        router.OnStartingCall();
        assertEquals(1, router.EnableBTSco());
        assertEquals(device.getId(), audioManager.getCommunicationDevice().getId());
    }

    private NativeRouter nativeRouter(AudioRouter router) {
        return Shadow.extract(router);
    }

    private AudioDeviceInfo device(int type, int id) {
        return device(type, id, "");
    }

    // AudioDeviceInfoBuilder sets neither the ID nor the address
    private AudioDeviceInfo device(int type, int id, String address) {
        AudioDeviceInfo device = AudioDeviceInfoBuilder.newBuilder().setType(type).build();
        Object port = ReflectionHelpers.getField(device, "mPort");
        ReflectionHelpers.setField(ReflectionHelpers.getField(port, "mHandle"), "mId", id);
        ReflectionHelpers.setField(port, "mAddress", address);
        return device;
    }

    /**
     * Records what AudioRouter reports to the native media manager. The native
     * methods are shadowed because libavs is not loaded in JVM tests.
     */
    @Implements(AudioRouter.class)
    public static class NativeRouter {
        int connectedEvents;
        int disconnectedEvents;
        int headsetEvents;
        int routeChanges;
        int deviceChanges;
        Runnable onRouteChanged;

        @Implementation protected void nativeBTDeviceConnected(boolean connected, long nativeMM) {
            if (connected) connectedEvents++;
            else disconnectedEvents++;
        }
        @Implementation protected void nativeHeadsetConnected(boolean connected, long nativeMM) {
            if (connected) headsetEvents++;
        }
        @Implementation protected void nativeRouteChanged(long nativeMM) {
            routeChanges++;
            if (onRouteChanged != null) onRouteChanged.run();
        }
        @Implementation protected void nativeDevicesChanged(long nativeMM) {
            deviceChanges++;
        }
        @Implementation protected void nativeUpdateRoute(int route, long nativeMM) {}
    }
}
