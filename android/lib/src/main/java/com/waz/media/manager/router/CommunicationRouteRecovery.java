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

import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Handler;
import java.util.ArrayList;
import java.util.List;

/** Restores a call's selected headset through unintended Android route changes. */
final class CommunicationRouteRecovery {
    interface SpeakerPreference {
        boolean isSpeakerRequested();
    }

    private static final int MAX_ATTEMPTS = 3;
    private static final long REQUEST_TIMEOUT_MS = 30_000;
    private static final long REJECTED_REQUEST_RETRY_MS = 1_000;

    private final AudioManager audioManager;
    private final Handler handler;
    private final SpeakerPreference speakerPreference;
    private boolean inCall;
    private boolean speakerRequested;
    private AudioDeviceInfo preferredHeadset;
    private Integer pendingDeviceId;
    private int attempts;
    private final Runnable retry = new Runnable() {
        @Override public void run() {
            pendingDeviceId = null;
            updateRoute();
        }
    };

    CommunicationRouteRecovery(AudioManager audioManager, Handler handler,
                               SpeakerPreference speakerPreference) {
        this.audioManager = audioManager;
        this.handler = handler;
        this.speakerPreference = speakerPreference;
    }

    void onCallStarted() {
        handler.post(() -> {
            if (inCall) return;
            inCall = true;
            rememberHeadset(audioManager.getCommunicationDevice());
            updateRoute();
        });
    }

    void onCallStopped() {
        handler.post(() -> {
            inCall = false;
            cancelRequest();
            preferredHeadset = null;
            speakerRequested = false;
        });
    }

    void onRouteChanged(AudioDeviceInfo device) {
        handler.post(() -> {
            // The route may change again before delivery. Confirm from the event, then
            // reconcile against the latest selected device.
            if (pendingDeviceId != null && device != null && device.getId() == pendingDeviceId) {
                cancelRequest();
            }
            rememberHeadset(device);
            updateRoute();
        });
    }

    private void rememberHeadset(AudioDeviceInfo device) {
        // AVS starts the router before entering communication mode. Preserve the selected
        // headset during that transition without changing the platform route yet.
        if (inCall && preferredHeadset == null && device != null && isBluetoothHeadset(device)) {
            preferredHeadset = device;
        }
    }

    void onDevicesChanged() {
        handler.post(() -> {
            if (!inCall) return;
            cancelRequest();
            updateRoute();
        });
    }

    private void updateRoute() {
        if (!inCall || audioManager.getMode() != AudioManager.MODE_IN_COMMUNICATION) return;
        boolean requested = speakerPreference.isSpeakerRequested();
        if (requested != speakerRequested) {
            speakerRequested = requested;
            cancelRequest();
        }

        AudioDeviceInfo current = audioManager.getCommunicationDevice();
        List<AudioDeviceInfo> devices = audioManager.getAvailableCommunicationDevices();
        if (speakerRequested) {
            if (current != null && current.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) {
                cancelRequest();
            } else {
                for (AudioDeviceInfo device : devices) {
                    if (device.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) {
                        requestRoute(device);
                        break;
                    }
                }
            }
        } else if (current != null && isBluetoothHeadset(current)) {
            preferredHeadset = current;
            cancelRequest();
        } else if (canRestoreHeadset(current, devices)) {
            restoreHeadset(devices);
        }
    }

    private boolean canRestoreHeadset(AudioDeviceInfo current, List<AudioDeviceInfo> devices) {
        if (current != null && current.getType() != AudioDeviceInfo.TYPE_BUILTIN_EARPIECE
                && current.getType() != AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) return false;
        for (AudioDeviceInfo device : devices) {
            if (device.getType() == AudioDeviceInfo.TYPE_WIRED_HEADSET
                    || device.getType() == AudioDeviceInfo.TYPE_USB_HEADSET) return false;
        }
        return true;
    }

    private void restoreHeadset(List<AudioDeviceInfo> devices) {
        List<AudioDeviceInfo> headsets = new ArrayList<>();
        for (AudioDeviceInfo device : devices) {
            if (isBluetoothHeadset(device)) headsets.add(device);
        }
        AudioDeviceInfo headset = findPreferredHeadset(headsets);
        if (headset == null) {
            // Keep the preference, but never attempt to select a disconnected device.
            handler.removeCallbacks(retry);
            pendingDeviceId = null;
        } else {
            preferredHeadset = headset;
            requestRoute(headset);
        }
    }

    private AudioDeviceInfo findPreferredHeadset(List<AudioDeviceInfo> headsets) {
        if (preferredHeadset == null) return headsets.isEmpty() ? null : headsets.get(0);
        for (AudioDeviceInfo headset : headsets) {
            if (headset.getId() == preferredHeadset.getId()) return headset;
        }
        String address = preferredHeadset.getAddress();
        if (!address.isEmpty()) {
            for (AudioDeviceInfo headset : headsets) {
                if (address.equals(headset.getAddress())) return headset;
            }
        }
        return address.isEmpty() && headsets.size() == 1 ? headsets.get(0) : null;
    }

    private void requestRoute(AudioDeviceInfo device) {
        if ((pendingDeviceId != null && pendingDeviceId == device.getId()) || attempts >= MAX_ATTEMPTS) return;
        pendingDeviceId = device.getId();
        attempts++;
        boolean accepted;
        try {
            accepted = audioManager.setCommunicationDevice(device);
        } catch (IllegalArgumentException exception) {
            // Availability can change between enumeration and selection.
            accepted = false;
        }
        handler.removeCallbacks(retry);
        handler.postDelayed(retry, accepted ? REQUEST_TIMEOUT_MS : REJECTED_REQUEST_RETRY_MS);
    }

    private void cancelRequest() {
        handler.removeCallbacks(retry);
        pendingDeviceId = null;
        attempts = 0;
    }

    private boolean isBluetoothHeadset(AudioDeviceInfo device) {
        return device.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                || device.getType() == AudioDeviceInfo.TYPE_BLE_HEADSET;
    }
}
