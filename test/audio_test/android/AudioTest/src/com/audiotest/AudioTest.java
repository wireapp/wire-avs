/*
 * Copyright (C) 2014 Zeta
 *
 */

package com.audiotest;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.TextView;
import android.widget.ToggleButton;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.Spinner;
import android.widget.Toast;
import android.os.Handler;

import android.content.Context;
import android.net.Uri;
import android.media.AudioManager;

import android.util.Log;
import com.waz.call.FlowManager;
import com.waz.call.RequestHandler;

import com.waz.media.manager.player.SoundSource;
import com.waz.media.manager.MediaManager;

import org.json.JSONArray;
import org.json.JSONObject;
import org.json.JSONException;

class LoopbackTest implements Runnable {
    private boolean isRunning_ = false;
    private Context ctx_;
    
    LoopbackTest(Context context){
        ctx_ = context;
    }
    
    @Override
    public void run() {
        isRunning_ = true;
        Log.d("LoopbackTest java", "LoopbackTest not on UI thread tid = " + android.os.Process.myTid());
        Start(ctx_);
        isRunning_ = false;
    }
    
    public boolean GetisRunning() {
        return isRunning_;
    }
    
    public native void Start(Context context); // C++ function
}

class VoeConfTest implements Runnable {
    private boolean isRunning_ = false;
    private boolean useHwAec = false;
    private int numChannels = -1;
    private Context ctx_;
    
    
    VoeConfTest(Context context){
        ctx_ = context;
    }
    
    @Override
    public void run() {
        isRunning_ = true;
        Log.d("VoeConfTest java", "VoeConfTest not on UI thread tid = " + android.os.Process.myTid());
        Start(ctx_, useHwAec, numChannels);
        isRunning_ = false;
    }
    
    public boolean GetisRunning() {
        return isRunning_;
    }
    
    public void SetOptions(boolean use_hw_aec, int num_channels) {
        useHwAec = use_hw_aec;
        numChannels = num_channels;
    }
    
    public native void Start(Context context, boolean use_hw_aec, int num_channels); // C++ function
}

class StartStopStressTest implements Runnable {
    private boolean isRunning_ = false;
    private Context ctx_;
    private AudioManager audioManager;
    private final Handler handler1 = new Handler();
    private final Handler handler2 = new Handler();
    
    private SoundSource ss1;
    private SoundSource ss2;
    private MediaManager mm;
    
    StartStopStressTest(Context context){
        ctx_ = context;
        audioManager = (AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
        
        Uri myUri = Uri.parse("file:///sdcard/call_drop.wav");
        int stream = android.media.AudioManager.STREAM_VOICE_CALL;
        ss1 = new SoundSource("Test1",ctx_,myUri,stream);
        
        mm = MediaManager.getInstance(ctx_);
        
        JSONObject options = new JSONObject();
        
        try {
            options.put("eventId", "Test1");
            options.put("path", "");
            options.put("format", "");
            options.put("mixingAllowed", 1);
            options.put("incallAllowed", 0);
            options.put("loopAllowed", 0);
            options.put("requirePlayback", 1);
            options.put("requireRecording", 0);
        }
        catch ( JSONException e ) {
            e.printStackTrace();
        }
        
        mm.registerMedia("Test1", options, ss1 );

        myUri = Uri.parse("file:///sdcard/ready_to_talk.wav");
        stream = android.media.AudioManager.STREAM_VOICE_CALL;
        ss2 = new SoundSource("Test2",ctx_,myUri,stream);
        
        JSONObject options2 = new JSONObject();
        
        try {
            options.put("eventId", "Test2");
            options.put("path", "");
            options.put("format", "");
            options.put("mixingAllowed", 1);
            options.put("incallAllowed", 1);
            options.put("loopAllowed", 0);
            options.put("requirePlayback", 1);
            options.put("requireRecording", 1);
        }
        catch ( JSONException e ) {
            e.printStackTrace();
        }
        
        mm.registerMedia("Test2", options2, ss2 );
        
    }
    
    @Override
    public void run() {
        isRunning_ = true;
        Log.d("StartStopStressTest java", "StartStopStressTest not on UI thread tid = " + android.os.Process.myTid());
        
        // Start updating the UI at regular intervals
        handler1.postDelayed(playSoundOne, 100);

        handler2.postDelayed(playSoundTwo, 600);
        
        Start(ctx_);
        
        isRunning_ = false;
    }
    
    public boolean GetisRunning() {
        return isRunning_;
    }

    private Runnable playSoundOne = new Runnable() {
        public void run() {
            mm.playMedia("Test1");
            if(isRunning_){
                handler1.postDelayed(this, 1000); // 500 ms
            }
        }
    };

    private Runnable playSoundTwo = new Runnable() {
        public void run() {
            mm.playMedia("Test2");
            if(isRunning_){
                handler2.postDelayed(this, 1000); // 500 ms
            }
        }
    };

    private Runnable toggleAudioMode = new Runnable() {
        public void run() {
            if(audioManager.getMode() == AudioManager.MODE_IN_COMMUNICATION){
                audioManager.setMode(AudioManager.MODE_NORMAL);
                Log.d("StartStopStressTest java","Set Mode to MODE_NORMAL thread tid = " + android.os.Process.myTid());
            } else {
                audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
                Log.d("StartStopStressTest java","Set Mode to MODE_IN_COMMUNICATION thread tid = " + android.os.Process.myTid());
            }
            if(isRunning_){
                handler1.postDelayed(this, 1000); // 500 ms
            }
        }
    };

    public native void Start(Context context); // C++ function
}

public class AudioTest extends Activity implements RequestHandler {
    
    private boolean useHwAec = false;
    int numChannels = 1;
    private Context ctx_;
    private VoeConfTest voeconftest_;
    private LoopbackTest loopbacktest_;
    private StartStopStressTest startstopstresstest_;
    
    private final Handler handler = new Handler();
    
    private FlowManager fm;
    
    private AudioManager audioManager;
    
    /** Called when the activity is first created. */
    @Override
    protected void onCreate(Bundle icicle) {
        super.onCreate(icicle);
        setContentView(R.layout.main);
        
        audioManager = (AudioManager)this.getSystemService(Context.AUDIO_SERVICE);
        
        ctx_ = this;
        voeconftest_ = new VoeConfTest(ctx_);
        loopbacktest_ = new LoopbackTest(ctx_);
        startstopstresstest_ = new StartStopStressTest(ctx_);

        fm = new FlowManager(ctx_, this);
        
        // Start updating the UI at regular intervals
        handler.postDelayed(sendUpdatesToUI, 1000);
        
        // initialize button click handlers
        ((Button) findViewById(R.id.start_voe_conf_test)).setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                if(!voeconftest_.GetisRunning()){
                    voeconftest_.SetOptions(useHwAec, numChannels);
                    
                    Thread t = new Thread(voeconftest_);
                    t.start();
                }
            }
        });

        ((Button) findViewById(R.id.start_voe_loop_back_test)).setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                if(!loopbacktest_.GetisRunning()){
                    Thread t = new Thread(loopbacktest_);
                    t.start();
                }
            }
        });
        
        // initialize button click handlers
        ((Button) findViewById(R.id.start_start_stop_stress_test)).setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                if(!startstopstresstest_.GetisRunning()){
                    Thread t = new Thread(startstopstresstest_);
                    t.start();
                }
            }
        });
        
        ((Button) findViewById(R.id.toggle_audio_mode)).setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                if(audioManager.getMode() == AudioManager.MODE_IN_COMMUNICATION){
                    audioManager.setMode(AudioManager.MODE_NORMAL);
                } else {
                    audioManager.setMode(AudioManager.MODE_IN_COMMUNICATION);
                }
            }
        });

        ((Button) findViewById(R.id.toggle_speaker)).setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                if(audioManager.isSpeakerphoneOn()){
                    audioManager.setSpeakerphoneOn(false);
                } else {
                    audioManager.setSpeakerphoneOn(true);
                }
            }
        });
        
        ((Button) findViewById(R.id.toggle_hw_aec)).setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                useHwAec = !useHwAec;
            }
        });
        
        ((Button) findViewById(R.id.num_channels)).setOnClickListener(new OnClickListener() {
            public void onClick(View view) {
                numChannels++;
                if(numChannels > 6){
                    numChannels = 1;
                }
            }
        });
        
    }
    
    /** Called when the activity is about to be destroyed. */
    @Override
    protected void onPause()
    {
        super.onPause();
    }

    /** Called when the activity is about to be destroyed. */
    @Override
    protected void onDestroy()
    {
        super.onDestroy();
    }
    
    /** Load jni .so on initialization */
    static {
        System.loadLibrary("audiotestnative");
        System.loadLibrary("avs");
    }
    
    private Runnable sendUpdatesToUI = new Runnable() {
        public void run() {
            updateUI();
            handler.postDelayed(this, 500); // 500 ms
        }
    };

    public int request(FlowManager manager, String path, String method, String ctype, byte[] content, long ctx){
        DoLog("request called with method " + method);
return 0;
    }

    private void updateUI() {
        
        TextView txtAudioMode = (TextView) findViewById(R.id.txtAudioMode);
        
        int mode = audioManager.getMode();
        if(mode == AudioManager.MODE_IN_COMMUNICATION){
            txtAudioMode.setText("MODE_IN_COMMUNICATION");
        }
        if(mode == AudioManager.MODE_IN_CALL){
            txtAudioMode.setText("MODE_IN_CALL");
        }
        if(mode == AudioManager.MODE_NORMAL){
            txtAudioMode.setText("MODE_NORMAL");
        }

        TextView txtIsSpeakerOn = (TextView) findViewById(R.id.txtIsSpeakerOn);
        if(audioManager.isSpeakerphoneOn()){
            txtIsSpeakerOn.setText("true");
        } else {
            txtIsSpeakerOn.setText("false");
        }

        TextView txtIsHwAecOnOn = (TextView) findViewById(R.id.txtIsHwAecOn);
        if(useHwAec){
            txtIsHwAecOnOn.setText("true");
        } else {
            txtIsHwAecOnOn.setText("false");
        }

        TextView txtNumChannels = (TextView) findViewById(R.id.txtNumChannels);
        txtNumChannels.setText(Integer.toString(numChannels));
    }

    final String logTag = "AudioTest java";

    private void DoLog(String msg) {
        Log.d(logTag, msg);
    }

}
