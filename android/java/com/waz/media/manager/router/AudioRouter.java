/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
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
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
package com.waz.media.manager.router;


import android.content.Context;
import android.media.AudioManager;

import android.media.AudioManager.OnAudioFocusChangeListener;

import java.util.Arrays;

import java.util.HashSet;
import java.util.HashMap;

import java.util.Iterator;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.content.BroadcastReceiver;
import android.os.Process;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.os.Build;

import android.util.Log;

public class AudioRouter {
  private Context _context = null;
  private long _nativeMM = 0;
  private AudioManager _audio_manager = null;
    
  private boolean _shouldMuteIncomingSound = false;
  private boolean _shouldMuteOutgoingSound = false;

  private boolean _shouldPreferLoudSpeaker = false;
  private boolean _isAudioFocusRequested = false;
    
  private OnAudioFocusChangeListener _afListener = null;

    // Set to true to enable debug logs.
    private static final boolean DEBUG = false;
    
    // Bluetooth audio SCO states. Example of valid state sequence:
    // SCO_INVALID -> SCO_TURNING_ON -> SCO_ON -> SCO_TURNING_OFF -> SCO_OFF.
    private static final int STATE_BLUETOOTH_SCO_INVALID = -1;
    private static final int STATE_BLUETOOTH_SCO_OFF = 0;
    private static final int STATE_BLUETOOTH_SCO_ON = 1;
    private static final int STATE_BLUETOOTH_SCO_TURNING_ON = 2;
    private static final int STATE_BLUETOOTH_SCO_TURNING_OFF = 3;
    
    // wired HS defines
    private static final int STATE_WIRED_HS_INVALID = -1;
    private static final int STATE_WIRED_HS_UNPLUGGED = 0;
    private static final int STATE_WIRED_HS_PLUGGED = 1;
    
    // Audio Route defines
    public static final int ROUTE_INVALID = -1;
    public static final int ROUTE_EARPIECE = 0;
    public static final int ROUTE_SPEAKER = 1;
    public static final int ROUTE_HEADSET = 2;
    public static final int ROUTE_BT = 3;
    
    // Enabled during initialization if BLUETOOTH permission is granted.
    private boolean _HasBluetoothPermission = true;
    
    // Stores the audio states for a wired headset
    private int _WiredHsState = STATE_WIRED_HS_UNPLUGGED;
        
    // Broadcast receiver for Bluetooth SCO broadcasts.
    // Utilized to detect if BT SCO streaming is on or off.
    private BroadcastReceiver _BluetoothScoReceiver = null;
    private BroadcastReceiver _BluetoothHeadsetReceiver = null;
    private BroadcastReceiver _WiredHeadsetReceiver = null;
    
    // Stores the audio states related to Bluetooth SCO audio, where some
    // states are needed to keep track of intermediate states while the SCO
    // channel is enabled or disabled (switching state can take a few seconds).
    private int _BluetoothScoState = STATE_BLUETOOTH_SCO_INVALID;
    private int _BluetoothScoStateBeforeSpeakerOn = STATE_BLUETOOTH_SCO_INVALID;
    
    private boolean _hasBluetoothHeadset = false;
    
    private static boolean runningOnJellyBeanOrHigher() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN;
    }
    
    private static boolean runningOnJellyBeanMR1OrHigher() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1;
    }
    
    private static boolean runningOnJellyBeanMR2OrHigher() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR2;
    }
    
    private boolean _inCall = false;
    
  public AudioRouter ( Context context, long nativeMM ) {
    this._context = context;
    this._nativeMM = nativeMM;
      
    if ( context != null ) {
      _audio_manager = (AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
    }
      
    this.subscribeToRouteUpdates();
    
    registerForBluetoothScoIntentBroadcast(); // Where should we Unregister ??
    registerForBluetoothHeadsetIntentBroadcast();
    registerForWiredHeadsetIntentBroadcast();
      
    _afListener = new OnAudioFocusChangeListener ( ) {
      public void onAudioFocusChange ( int focusChange ) { DoLog("DVA: On Audio Focus Change"); }
    };
  }

  public int EnableSpeaker(){
    DoLog("EnableSpeaker ");
    int route = GetAudioRoute();
    if(route == ROUTE_BT){
      stopBluetoothSco();
    }
    _audio_manager.setSpeakerphoneOn(true);
      
    return 0;
  }

  public int EnableHeadset(){
    DoLog("EnableHeadset ");
    int cur_route = GetAudioRoute();
    if(cur_route == ROUTE_BT){
        stopBluetoothSco();
    }
    _audio_manager.setSpeakerphoneOn(false);
    return 0;
  }

  public int EnableEarpiece(){
    DoLog("EnableEarpiece ");
    if(!hasEarpiece()){
        return -1;
    }
    int cur_route = GetAudioRoute();
    if(cur_route == ROUTE_HEADSET){
        // Cannot use Earpiece when a HS is plugged in
        return -1;
    }
    if(cur_route == ROUTE_BT){
       stopBluetoothSco();
    }
    _audio_manager.setSpeakerphoneOn(false);
    return 0;
  }
    
  public int EnableBTSco(){
    DoLog("EnableBTSco ");
      
    if(hasBluetoothHeadset()){
      startBluetoothSco();
      return 0;
    } else {
      return -1;
    }
  }
    
  public int GetAudioRoute(){
      int route = ROUTE_INVALID;
      if(_BluetoothScoState == STATE_BLUETOOTH_SCO_ON){
          route = ROUTE_BT;
          DoLog("GetAudioRoute() BT \n");
      }else if(_audio_manager.isSpeakerphoneOn()){
          route = ROUTE_SPEAKER;
          DoLog("GetAudioRoute() Speaker \n");
      }else if(_WiredHsState == STATE_WIRED_HS_PLUGGED){
          route = ROUTE_HEADSET;
          DoLog("GetAudioRoute() HEADSET \n");
      } else{
          if(hasEarpiece()){
              route = ROUTE_EARPIECE;
              DoLog("GetAudioRoute() EARPIECE \n");
          } else {
              route = ROUTE_SPEAKER; /* Goes here if a tablet where iSpaekerPhoneOn dosnt tell the truth  */
              DoLog("GetAudioRoute() Speaker \n");
          }
      }
      return route;
  }
    
  private void UpdateRoute(){
      int route;
      
      route = GetAudioRoute();
      
      // call route change callback
      nativeUpdateRoute(route, this._nativeMM);
  }
    
  public void OnStartingCall(){
      if(!_inCall){
          _inCall = true;
          _hasBluetoothHeadset = hasBluetoothHeadset();
          if(_hasBluetoothHeadset){
              DoLog("startBluetoothSco()");
              startBluetoothSco();
          } else {
              /* Enable earpiece */
              
          }
          _BluetoothScoStateBeforeSpeakerOn = STATE_BLUETOOTH_SCO_INVALID;
          UpdateRoute(); // Here the Mode is changed acoriding to audio route
      }
  }

  public void OnStoppingCall(){
      _inCall = false;
      if(_audio_manager.getMode() != AudioManager.MODE_NORMAL){
          _audio_manager.setMode(AudioManager.MODE_NORMAL);
          stopBluetoothSco();
          DoLog("Set AudioManager to MODE_NORMAL ");
      }
  }

  private void subscribeToRouteUpdates ( ) {
    // TODO: should somehow subscribe to OS route changed updates
  }

    private void registerForWiredHeadsetIntentBroadcast() {
        Context context = this._context;
        
        IntentFilter filter = new IntentFilter(Intent.ACTION_HEADSET_PLUG);
        
        /** Receiver which handles changes in wired headset availability. */
        _WiredHeadsetReceiver = new BroadcastReceiver() {
            private static final int STATE_UNPLUGGED = 0;
            private static final int STATE_PLUGGED = 1;
            private static final int HAS_NO_MIC = 0;
            private static final int HAS_MIC = 1;
            
            @Override
            public void onReceive(Context context, Intent intent) {
                int state = intent.getIntExtra("state", STATE_UNPLUGGED);
 
                int microphone = intent.getIntExtra("microphone", HAS_NO_MIC);
                String name = intent.getStringExtra("name");
                DoLog("WiredHsBroadcastReceiver.onReceive: a=" + intent.getAction()
                         + ", s=" + state
                         + ", m=" + microphone
                         + ", n=" + name
                         + ", sb=" + isInitialStickyBroadcast());
                
                AudioManager audioManager = (AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
                
                switch (state) {
                    case STATE_UNPLUGGED:
                        DoLog("Headset Unplugged");
                        _WiredHsState = STATE_WIRED_HS_UNPLUGGED;
                        nativeHeadsetConnected(false, _nativeMM);
                        break;
                    case STATE_PLUGGED:
                        DoLog("Headset plugged");
                        _WiredHsState = STATE_WIRED_HS_PLUGGED;
                        nativeHeadsetConnected(true, _nativeMM);
                        break;
                    default:
                        DoLog("Invalid state");
                        _WiredHsState = STATE_WIRED_HS_INVALID;
                        break;
                }
                UpdateRoute();
            }
        };
        
        // Note: the intent we register for here is sticky, so it'll tell us
        // immediately what the last action was (plugged or unplugged).
        // It will enable us to set the speakerphone correctly.
        context.registerReceiver(_WiredHeadsetReceiver, filter);
    }
    
    
    /**
     * Registers receiver for the broadcasted intent related to BT headset
     * availability or a change in connection state of the local Bluetooth
     * adapter. Example: triggers when the BT device is turned on or off.
     * BLUETOOTH permission is required to receive this one.
     */
    private void registerForBluetoothHeadsetIntentBroadcast() {
        Context context = this._context;
        
        IntentFilter filter = new IntentFilter(android.bluetooth.BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED);
        
        /** Receiver which handles changes in BT headset availability. */
        _BluetoothHeadsetReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                // A change in connection state of the Headset profile has
                // been detected, e.g. BT headset has been connected or
                // disconnected. This broadcast is *not* sticky.
                
                int profileState = intent.getIntExtra(
                                                      android.bluetooth.BluetoothHeadset.EXTRA_STATE,
                                                      android.bluetooth.BluetoothHeadset.STATE_DISCONNECTED);
                
                DoLog("BtBroadcastReceiver.onReceive: a=" + intent.getAction() +
                      ", s=" + profileState +
                      ", sb=" + isInitialStickyBroadcast());
                
                AudioManager audioManager = (AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
                
                switch (profileState) {
                    case android.bluetooth.BluetoothProfile.STATE_DISCONNECTED:
                        DoLog("STATE_DISCONNECTED");
                        nativeBTDeviceConnected(false, _nativeMM);
                        break;
                    case android.bluetooth.BluetoothProfile.STATE_CONNECTED:
                        DoLog("STATE_CONNECTED");
                        nativeBTDeviceConnected(true, _nativeMM);
                        break;
                    case android.bluetooth.BluetoothProfile.STATE_CONNECTING:
                        // Bluetooth service is switching from off to on.
                        DoLog("STATE_CONNECTING");
                        break;
                    case android.bluetooth.BluetoothProfile.STATE_DISCONNECTING:
                        // Bluetooth service is switching from on to off.
                        DoLog("STATE_DISCONNECTING");
                        break;
                    default:
                        DoLog("Invalid state!");
                        break;
                }
            }
        };
        
        context.registerReceiver(_BluetoothHeadsetReceiver, filter);
    }
    
    private void unregisterForBluetoothHeadsetIntentBroadcast() {
        Context context = this._context;
        _BluetoothHeadsetReceiver = null;
    }
    
    /**
     * Registers receiver for the broadcasted intent related the existence
     * of a BT SCO channel. Indicates if BT SCO streaming is on or off.
     */
    private void registerForBluetoothScoIntentBroadcast() {
        Context context = this._context;
        
        IntentFilter filter = new IntentFilter(
                                               AudioManager.ACTION_SCO_AUDIO_STATE_UPDATED);
        
        /** BroadcastReceiver implementation which handles changes in BT SCO. */
        _BluetoothScoReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                int state = intent.getIntExtra(
                                               AudioManager.EXTRA_SCO_AUDIO_STATE,
                                               AudioManager.SCO_AUDIO_STATE_DISCONNECTED);
        
                DoLog("ScoBroadcastReceiver.onReceive: a=" + intent.getAction() +
                      ", s=" + state +
                      ", sb=" + isInitialStickyBroadcast());

                switch (state) {
                    case AudioManager.SCO_AUDIO_STATE_CONNECTED:
                        DoLog("SCO_AUDIO_STATE_CONNECTED");
                        _BluetoothScoState = STATE_BLUETOOTH_SCO_ON;
                        break;
                    case AudioManager.SCO_AUDIO_STATE_DISCONNECTED:
                        DoLog("SCO_AUDIO_STATE_DISCONNECTED");
                        _BluetoothScoState = STATE_BLUETOOTH_SCO_OFF;
                        break;
                    case AudioManager.SCO_AUDIO_STATE_CONNECTING:
                        DoLog("SCO_AUDIO_STATE_CONNECTING");
                        // do nothing
                        break;
                    default:
                        DoLogErr("Invalid state!");
                }
                UpdateRoute();
            }
        };
        
        context.registerReceiver(_BluetoothScoReceiver, filter);
    }
    
    private void unregisterForBluetoothScoIntentBroadcast() {
        Context context = this._context;
        
        context.unregisterReceiver(_BluetoothScoReceiver);
        _BluetoothScoReceiver = null;
    }
    
    /**
     * Gets the current Bluetooth headset state.
     * android.bluetooth.BluetoothAdapter.getProfileConnectionState() requires
     * the BLUETOOTH permission.
     */
    private boolean hasBluetoothHeadset() {
        if (!_HasBluetoothPermission) {
            DoLogErr("hasBluetoothHeadset() requires BLUETOOTH permission!");
            return false;
        }
        
        Context context = this._context;
        
        // To get a BluetoothAdapter representing the local Bluetooth adapter,
        // when running on JELLY_BEAN_MR1 (4.2) and below, call the static
        // getDefaultAdapter() method; when running on JELLY_BEAN_MR2 (4.3) and
        // higher, retrieve it through getSystemService(String) with
        // BLUETOOTH_SERVICE.
        BluetoothAdapter btAdapter = null;
        if (runningOnJellyBeanMR2OrHigher()) {
            // Use BluetoothManager to get the BluetoothAdapter for
            // Android 4.3 and above.
            try {
                BluetoothManager btManager =
                (BluetoothManager)context.getSystemService(
                                                            Context.BLUETOOTH_SERVICE);
                btAdapter = btManager.getAdapter();
            } catch (Exception e) {
                DoLogErr("BluetoothManager.getAdapter exception");
                return false;
            }
        } else {
            // Use static method for Android 4.2 and below to get the
            // BluetoothAdapter.
            try {
                btAdapter = BluetoothAdapter.getDefaultAdapter();
            } catch (Exception e) {
                DoLogErr("BluetoothAdapter.getDefaultAdapter exception");
                return false;
            }
        }
        
        if(btAdapter == null){
            return false;
        }
        
        int profileConnectionState;
        try {
            profileConnectionState = btAdapter.getProfileConnectionState(
                                                                         android.bluetooth.BluetoothProfile.HEADSET);
        } catch (Exception e) {
            DoLogErr("BluetoothAdapter.getProfileConnectionState exception");
            profileConnectionState =
            android.bluetooth.BluetoothProfile.STATE_DISCONNECTED;
        }
        
        // Ensure that Bluetooth is enabled and that a device which supports the
        // headset and handsfree profile is connected.
        return btAdapter.isEnabled() && profileConnectionState ==
        android.bluetooth.BluetoothProfile.STATE_CONNECTED;
    }
    
    /** Enables BT audio using the SCO audio channel. */
    private void startBluetoothSco() {
        if (!_HasBluetoothPermission) {
            return;
        }
        if (_BluetoothScoState == STATE_BLUETOOTH_SCO_ON ||
            _BluetoothScoState == STATE_BLUETOOTH_SCO_TURNING_ON) {
            DoLog("BT allready turned on or turning on !! \n");
            // Unable to turn on BT in this state.
            return;
        }
        
        // Check if audio is already routed to BT SCO; if so, just update
        // states but don't try to enable it again.
        if (_audio_manager.isBluetoothScoOn()) {
            _BluetoothScoState = STATE_BLUETOOTH_SCO_ON;
            DoLog("BT allready on !! \n");
            return;
        }
        
         _BluetoothScoState = STATE_BLUETOOTH_SCO_TURNING_ON;
         DoLog("startBluetoothSco: turning BT SCO on...");
        try {
            _audio_manager.startBluetoothSco();
        } catch (NullPointerException e) {
            // TODO This is a temp workaround for Lollipop
            DoLogErr("startBluetoothSco() failed. no bluetooth device connected.");
        }
    }
    
  /** Disables BT audio using the SCO audio channel. */
  private void stopBluetoothSco() {
      if (!_HasBluetoothPermission) {
            return;
      }
      if (_BluetoothScoState != STATE_BLUETOOTH_SCO_ON &&
            _BluetoothScoState != STATE_BLUETOOTH_SCO_TURNING_ON) {
            // No need to turn off BT in this state.
            return;
        }
      
        if (!_audio_manager.isBluetoothScoOn()) {
            DoLogErr("Unable to stop BT SCO since it is already disabled!");
            return;
        }
        
        DoLog("stopBluetoothSco: turning BT SCO off...");
        _BluetoothScoState = STATE_BLUETOOTH_SCO_TURNING_OFF;
        _audio_manager.stopBluetoothSco();
  }

  private boolean hasEarpiece() {
      Context context = this._context;
      return(context.getPackageManager().hasSystemFeature(PackageManager.FEATURE_TELEPHONY));
  }
    
  private native void setPlaybackRoute(int route);
    
  final String logTag = "avs AudioRouter";
    
  private void DoLog(String msg) {
      if (DEBUG) {
          Log.d(logTag, msg);
      }
  }

  private void DoLogErr(String msg) {
      Log.e(logTag, msg);
  }
    
  private native void nativeUpdateRoute(int route, long nativeMM);
  private native void nativeHeadsetConnected(boolean connected, long nativeMM);
  private native void nativeBTDeviceConnected(boolean connected, long nativeMM);
}
