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

import java.lang.Thread;

import java.util.Arrays;

import java.util.HashSet;
import java.util.HashMap;

import java.util.Iterator;

import java.util.List;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothHeadset;
import android.bluetooth.BluetoothA2dp;
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
    private static final boolean DEBUG = true;
    
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
    

    private BluetoothHeadset  bluetoothHeadset = null;
    private BluetoothA2dp  bluetoothA2dp = null;
    private static BluetoothDevice bluetoothDevice = null;

	private boolean btScoConnected = false;
	//private int _BluetoothScoState = STATE_BLUETOOTH_SCO_INVALID;
	
    // Stores the audio states for a wired headset
    private int _WiredHsState = STATE_WIRED_HS_UNPLUGGED;
        
    
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

    Log.i(logTag, "AudioRouter: incall=" + _inCall);
    
    if ( context != null ) {
      _audio_manager = (AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
    }
      
    this.subscribeToRouteUpdates();
    
    setupBluetooth();
    registerForBluetoothScoIntentBroadcast(); // Where should we Unregister ??    
    registerForWiredHeadsetIntentBroadcast();
      
    _afListener = new OnAudioFocusChangeListener ( ) {
      public void onAudioFocusChange ( int focusChange ) { DoLog("DVA: On Audio Focus Change"); }
    };
  }

	private void btHeadsetService(BluetoothHeadset btHeadset) {
		List<BluetoothDevice> devices;

		bluetoothHeadset = btHeadset;

		devices = bluetoothHeadset.getConnectedDevices();
		if (!devices.isEmpty())
			bluetoothDevice = devices.get(0);
		if (bluetoothDevice != null)
			nativeBTDeviceConnected(true, _nativeMM);
		
		Context context = this._context;
		IntentFilter filter = new IntentFilter(android.bluetooth.BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED);
        
		/** Receiver which handles changes in BT headset availability. */
		BroadcastReceiver btReceiver = new BroadcastReceiver() {
			@Override
			public void onReceive(Context context, Intent intent) {
				int state = intent.getIntExtra(android.bluetooth.BluetoothHeadset.EXTRA_STATE,
							       android.bluetooth.BluetoothHeadset.STATE_DISCONNECTED);
				if (state == android.bluetooth.BluetoothHeadset.STATE_CONNECTED) {
					bluetoothDevice = intent.getParcelableExtra(android.bluetooth.BluetoothDevice.EXTRA_DEVICE);
					Log.i(logTag, "bluetoothHeadset: connected: btdev=" + bluetoothDevice);
					nativeBTDeviceConnected(true, _nativeMM);
				}
				else if (state == android.bluetooth.BluetoothHeadset.STATE_DISCONNECTED) {
					Log.i(logTag, "bluetoothHeadset: disconnected");
					bluetoothDevice = null;
					int route = GetAudioRoute();
					if (route == ROUTE_SPEAKER)
						EnableSpeaker();
					nativeBTDeviceConnected(false, _nativeMM);					
				}
			}
		};

		context.registerReceiver(btReceiver, filter);
	}

	private void btA2dpService(BluetoothA2dp btA2dp) {
		List<BluetoothDevice> devices;
		
		bluetoothA2dp = btA2dp;

		devices = bluetoothA2dp.getConnectedDevices();
		if (!devices.isEmpty())
			bluetoothDevice = devices.get(0);
		
		Context context = this._context;
		IntentFilter filter = new IntentFilter(android.bluetooth.BluetoothA2dp.ACTION_CONNECTION_STATE_CHANGED);
        
		/** Receiver which handles changes in BT a2dp availability. */
		BroadcastReceiver btReceiver = new BroadcastReceiver() {
			@Override
			public void onReceive(Context context, Intent intent) {
				int state = intent.getIntExtra(android.bluetooth.BluetoothA2dp.EXTRA_STATE,
							       android.bluetooth.BluetoothA2dp.STATE_DISCONNECTED);
				if (state == android.bluetooth.BluetoothA2dp.STATE_CONNECTED) {
					bluetoothDevice = intent.getParcelableExtra(android.bluetooth.BluetoothDevice.EXTRA_DEVICE);
					Log.i(logTag, "bluetoothA2dp: connected: btdev=" + bluetoothDevice);
					nativeBTDeviceConnected(true, _nativeMM);
				}
				else if (state == android.bluetooth.BluetoothA2dp.STATE_DISCONNECTED) {
					Log.i(logTag, "bluetootA2dp: disconnected");
					bluetoothDevice = null;
					nativeBTDeviceConnected(false, _nativeMM);
				}
			}
		};

		context.registerReceiver(btReceiver, filter);
	}

	private void setupBluetooth() {
		// Get the default adapter
		BluetoothAdapter btAdapter = BluetoothAdapter.getDefaultAdapter();

		BluetoothProfile.ServiceListener profileListener = new BluetoothProfile.ServiceListener() {				
			public void onServiceConnected(int profile, BluetoothProfile proxy) {
				Log.i(logTag, "bluetooth: service connected for profile: " + profile);
				if (profile == BluetoothProfile.HEADSET) {
					Log.i(logTag, "bluetooth: HEADSET connected");
					btHeadsetService((BluetoothHeadset)proxy);
				}
				if (profile == BluetoothProfile.A2DP) {
					Log.i(logTag, "bluetooth: A2DP connected");
					bluetoothA2dp = (BluetoothA2dp)proxy;
				}
			}
			public void onServiceDisconnected(int profile) {
				Log.i(logTag, "bluetooth: service disconnected for profile: " + profile);
				if (profile == BluetoothProfile.HEADSET) {
					Log.i(logTag, "bluetooth: HEADSET disconnected");
					bluetoothHeadset = null;
				}
				if (profile == BluetoothProfile.A2DP) {
					Log.i(logTag, "bluetooth: A2DP disconnected");
					bluetoothA2dp = null;
				}
			}
		};
		btAdapter.getProfileProxy(this._context, profileListener, BluetoothProfile.HEADSET);
		btAdapter.getProfileProxy(this._context, profileListener, BluetoothProfile.A2DP);		
	}


	private int startBluetooth() {

		if (bluetoothDevice == null) {
			return -1;
		}
		else {
			if (!btScoConnected) {
				int n = 5;
				
				_audio_manager.startBluetoothSco();
				while(!btScoConnected && n > 0) {
					try {
						Thread.sleep(1000);
					}
					catch (Exception e) {
					}
					n--;
				}
			}
			Log.i(logTag, "startBluetooth: scoConnected=" + btScoConnected);
			_audio_manager.setBluetoothScoOn(true);
			
			return 0;
		}
	}
	private void stopBluetooth() {
		_audio_manager.stopBluetoothSco();
		_audio_manager.setBluetoothScoOn(false);
		
	}
  public int EnableSpeaker(){
    DoLog("EnableSpeaker ");

    _audio_manager.setSpeakerphoneOn(true);
      
    return 0;
  }

  public int EnableHeadset(){
    DoLog("EnableHeadset ");

    if (_audio_manager.isBluetoothScoOn())
	    _audio_manager.setBluetoothScoOn(false);

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
    if (bluetoothDevice != null)
	    _audio_manager.setBluetoothScoOn(true);
    else
	    _audio_manager.setSpeakerphoneOn(false);

    return 0;
  }

	public int EnableBTSco() {
		Log.i(logTag, "EnableBTSco: incall=" + _inCall);
		if (_inCall)
			return startBluetooth();
		else
			return -1;
	}
	
    
  public int GetAudioRoute() {
	  int route = ROUTE_INVALID;

	  if (_audio_manager.isBluetoothScoOn()) {
		  route = ROUTE_BT;
		  DoLog("GetAudioRoute() BT");
	  }
	  else if(_audio_manager.isSpeakerphoneOn()) {
		  route = ROUTE_SPEAKER;
		  DoLog("GetAudioRoute() Speaker");
	  }
	  else if(_WiredHsState == STATE_WIRED_HS_PLUGGED) {
		  route = ROUTE_HEADSET;
		  DoLog("GetAudioRoute() Headset");
	  }
	  else {
		  if(hasEarpiece()) {
			  route = ROUTE_EARPIECE;
			  DoLog("GetAudioRoute() Earpiece");
		  }
		  else {
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
	  Log.i(logTag,"OnStartingCall: incall=" + _inCall);
	  if(!_inCall) {
		  Log.i(logTag, "OnStartingCall btdev=" + bluetoothDevice);
		  _inCall = true;
		  if(bluetoothDevice != null) {
			  Log.i(logTag,"OnStartingCall: startingBluetooth: scoOn: " + _audio_manager.isBluetoothScoOn());
			  startBluetooth();
		  }
		  else {
			  // Enable earpiece
		  }
	  }
  }

  public void OnStoppingCall() {
	  Log.i(logTag, "OnStoppingCall incall=" + _inCall);
	  
	  _inCall = false;
	  if(_audio_manager.getMode() != AudioManager.MODE_NORMAL){
		  _audio_manager.setMode(AudioManager.MODE_NORMAL);
	  }
	  stopBluetooth();
  }

  private void subscribeToRouteUpdates ( ) {
    // TODO: should somehow subscribe to OS route changed updates
  }

    private void registerForWiredHeadsetIntentBroadcast() {
        Context context = this._context;
        
        IntentFilter filter = new IntentFilter(Intent.ACTION_HEADSET_PLUG);
        
        /** Receiver which handles changes in wired headset availability. */
        BroadcastReceiver hsReceiver = new BroadcastReceiver() {
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

		int route = GetAudioRoute();
		
                switch (state) {
                    case STATE_UNPLUGGED:
                        DoLog("Headset Unplugged");
                        _WiredHsState = STATE_WIRED_HS_UNPLUGGED;
			if (btScoConnected) {
				_audio_manager.setBluetoothScoOn(true);
			}
			if (route == ROUTE_SPEAKER)
				EnableSpeaker();
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
        context.registerReceiver(hsReceiver, filter);
    }
    
    
    /**
     * Registers receiver for the broadcasted intent related the existence
     * of a BT SCO channel. Indicates if BT SCO streaming is on or off.
     */
    private void registerForBluetoothScoIntentBroadcast() {
        Context context = this._context;
        
        IntentFilter filter = new IntentFilter(AudioManager.ACTION_SCO_AUDIO_STATE_UPDATED);
        
        /** BroadcastReceiver implementation which handles changes in BT SCO. */
        BroadcastReceiver btReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                int state = intent.getIntExtra(
			     AudioManager.EXTRA_SCO_AUDIO_STATE,
			     AudioManager.SCO_AUDIO_STATE_DISCONNECTED);
        
                Log.i(logTag, "ScoBroadcastReceiver.onReceive: a=" + intent.getAction() +
                      ", s=" + state +
                      ", sb=" + isInitialStickyBroadcast());

                switch (state) {
		case AudioManager.SCO_AUDIO_STATE_CONNECTED:
			Log.i(logTag, "SCO_AUDIO_STATE_CONNECTED");
			btScoConnected = true;
			break;
			    
		case AudioManager.SCO_AUDIO_STATE_DISCONNECTED:
			Log.i(logTag, "SCO_AUDIO_STATE_DISCONNECTED");
			btScoConnected = false;
			break;

		case AudioManager.SCO_AUDIO_STATE_CONNECTING:
			Log.i(logTag, "SCO_AUDIO_STATE_CONNECTING");
			break;
			    
		default:
                        DoLogErr("Invalid state!");
                }
                //UpdateRoute();
            }
        };
        
        context.registerReceiver(btReceiver, filter);
    }

	/*
    private void unregisterForBluetoothScoIntentBroadcast() {
        Context context = this._context;
        
        context.unregisterReceiver(_BluetoothScoReceiver);
        _BluetoothScoReceiver = null;
	}
	*/
    
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
