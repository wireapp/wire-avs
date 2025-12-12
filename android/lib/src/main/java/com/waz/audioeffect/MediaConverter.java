package com.waz.audioeffect;

import android.content.Context;
import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaExtractor;
import android.media.MediaMuxer;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;

import com.waz.avs.AVSystem;


class MediaConverter {
	private static String TAG = "MediaConverter";
	private static final int QUEUE_TIMEOUT = 10000;

        private int BUFFER_INPUT_SIZE = 524288; // 524288 Bytes = 0.5 MB
    
	private	MediaFormat mediaFormat = null;
	private String inPath;
	private String outPath;
	private int bitRate = 90000;
	private int sampleRate = 44100;
	private int channelCount;

	public MediaConverter(String inPath, String outPath) {
		this.inPath = inPath;
		this.outPath = outPath;
	}

	public MediaFormat getMediaFormat() {
		return mediaFormat;
	}

	public int decode(String pcmPath) {
		MediaExtractor extractor = new MediaExtractor();
		MediaCodec decoder = null;
		FileChannel fc = null;
		int ret = -1;
		boolean isBuffered = false;
	
		try {
		    fc = new FileOutputStream(pcmPath).getChannel();
			
		    extractor.setDataSource(inPath);

		    // select the first audio track in the file
		    int numTracks = extractor.getTrackCount();
		    boolean found = false;
		    String mimeType = null;
		    int i = 0;

		    while (i < numTracks && !found) {
			mediaFormat = extractor.getTrackFormat(i);
			try {
			    mimeType = mediaFormat.getString(MediaFormat.KEY_MIME);
			    if (mimeType.startsWith("audio/")) {
				found = true;
				isBuffered =
				    mimeType.contains("m4a")
				||  mimeType.contains("mpeg");
				Log.d(TAG, "decode: mime-type=" + mimeType + " isBuffered=" + isBuffered);
			    }
			    else {
				i++;
			    }
			}
			catch (Exception e) {
			    Log.w(TAG, "decode: no mime type");
			}
		    }
		
		    if (!found) {
			Log.e(TAG, "decode: no audio track");
			return -1;
		    }

		    try {
			bitRate = mediaFormat.getInteger(MediaFormat.KEY_BIT_RATE);
		    }
		    catch(Exception e) {
			Log.w(TAG, "decode: bitrate key exception: " + e);
		    }
		    try {
			sampleRate = mediaFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE);
		    }
		    catch(Exception e) {
			Log.w(TAG, "decode: samplerate key exception: " + e);
		    }
		    try {
			channelCount = mediaFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
		    }
		    catch(Exception e) {
			Log.w(TAG, "decode: channel count exception: " + e);
		    }

		    //Log.i(TAG, "decode: mime:" + mimeType + " bitRate=" + bitRate + " on track=" + i);
			
		    extractor.selectTrack(i);		
		    decoder = MediaCodec.createDecoderByType(mimeType);

		    boolean eos = false;
		    boolean inputDone = false;
		    boolean finished = false;

		    mediaFormat.setInteger(MediaFormat.KEY_PCM_ENCODING, AudioFormat.ENCODING_PCM_16BIT);
		    mediaFormat.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, BUFFER_INPUT_SIZE); 
		    decoder.configure(mediaFormat, null, null, 0);
		    decoder.start();

		    while(!finished) {
			if (!eos) {
			    int idx = decoder.dequeueInputBuffer(QUEUE_TIMEOUT);
			    //Log.i(TAG, "decode: idx=" + idx);
			    if (idx >= 0) {
				ByteBuffer inputBuffer = decoder.getInputBuffer(idx);
				if (inputBuffer == null) {
				    Log.i(TAG, "decode: no inputBuffer");
				    continue;
				}

				long sampleTime = 0;
				int result;
				int chunkSize = 0;
				long sampleSize = extractor.getSampleSize();

				inputDone = sampleSize <= 0;
				//Log.i(TAG, "decode: sampleSize=" + sampleSize + " isBuffered=" + isBuffered);

				while(chunkSize <= (BUFFER_INPUT_SIZE - sampleSize) && !inputDone) {
				    ByteBuffer tempBuffer = ByteBuffer.allocate((int)sampleSize);
				    result = extractor.readSampleData(tempBuffer, 0);
				    //Log.i(TAG, "decode: result=" + result + " sampleSize=" + sampleSize);
				    if (result < 0) {
					inputDone = true;
				    }
				    else {
					sampleTime += extractor.getSampleTime();
					inputBuffer.put(tempBuffer);
					chunkSize += result;
					extractor.advance();
					if (!isBuffered)
					    break;
					sampleSize = extractor.getSampleSize();
					if (sampleSize <= 0)
					    inputDone = true;
				    }
				}

				//Log.i(TAG, "decode: chunkSize=" + chunkSize + " sampleTime=" + sampleTime + " inputDone=" + inputDone);
				if (chunkSize > 0) {
				    decoder.queueInputBuffer(idx, 0, chunkSize, sampleTime, 0);
				}
				else if (inputDone) {
				    Log.i(TAG, "decode: done");

				    decoder.queueInputBuffer(idx, 0, 0, -1, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
				    eos = true;
				}
			    }
			}

			MediaCodec.BufferInfo bufInfo = new MediaCodec.BufferInfo();

			int idx = decoder.dequeueOutputBuffer(bufInfo, QUEUE_TIMEOUT);
			//Log.i(TAG, "decode: output idx=" + idx);
			if (idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
			    MediaFormat mf = decoder.getOutputFormat();
			    String mt = mf.getString(MediaFormat.KEY_MIME);
			    Log.i(TAG, "decode: output format=" + mt);
			}
			if (idx >= 0) {
			    ByteBuffer outBuf = decoder.getOutputBuffer(idx);
			    if (outBuf != null) {
				outBuf.rewind();
				//Log.i(TAG, "decode: output: outBuf len=" + outBuf.remaining());
				fc.write(outBuf);
				decoder.releaseOutputBuffer(idx, false);
				
				finished = (bufInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
				//Log.i(TAG, "decode: output: outBuf finished=" + finished);
			    }
			}
		    }

		    ret = 0;
		}
		catch (Exception e) {
		    Log.e(TAG, "decode: failed with exception: " + e);

		    mediaFormat = null;
		    ret = -1;
		}
		finally {
		    if (decoder != null) {
			decoder.stop();
			decoder.release();
		    }
		    if (extractor != null) {
			extractor.release();
		    }
		    if (fc != null) {
			try {
			    fc.close();
			}
			catch(Exception e) {
			}
			
		    }
		}

		return ret;
	}

	public int encode(String pcmPath) {
		MediaMuxer muxer = null;
		MediaCodec encoder = null;
		FileChannel fc = null;
		int ret = -1;
		
		try {
			muxer = new MediaMuxer(outPath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
			fc = new FileInputStream(pcmPath).getChannel();

			int trackId = muxer.addTrack(mediaFormat);			
			boolean finished = false;
			boolean eos = false;
			String mimeType = "audio/mp4a-latm";
			int profileId = MediaCodecInfo.CodecProfileLevel.AACObjectLC;

			try {
				mediaFormat.getString(MediaFormat.KEY_MIME);
			}
			catch (Exception e) {
				Log.w(TAG, "encode: no mime type");
			}

			try {
				mediaFormat.getInteger(MediaFormat.KEY_AAC_PROFILE);
			}
			catch (Exception e) {
				Log.w(TAG, "encode: no profile");
			}

			encoder = MediaCodec.createEncoderByType(mimeType);

			MediaFormat format = new MediaFormat();
			format.setString(MediaFormat.KEY_MIME, mimeType);
			format.setInteger(MediaFormat.KEY_BIT_RATE, bitRate);
			format.setInteger(MediaFormat.KEY_CHANNEL_COUNT, channelCount);
			format.setInteger(MediaFormat.KEY_SAMPLE_RATE, sampleRate);
			format.setInteger(MediaFormat.KEY_AAC_PROFILE, profileId);

			encoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
			muxer.start();
			encoder.start();
			long ts = 0;
			while(!finished) {
				// Process input buffer
				if (!eos) {
					int idx = encoder.dequeueInputBuffer(QUEUE_TIMEOUT);
					if (idx >= 0) {
						ByteBuffer inBuf = encoder.getInputBuffer(idx);
						if (inBuf == null) {
							Log.d(TAG, "encode: no inbuf");
						}
						else {
							long sampleTime = 0;
							int result;

							result = fc.read(inBuf);
							if (result < 0) {
								encoder.queueInputBuffer(idx, 0, 0, ts,
											 MediaCodec.BUFFER_FLAG_END_OF_STREAM);
								eos = true;
							}
							else {
								inBuf.rewind();
								encoder.queueInputBuffer(idx, 0, result, ts, 0);
								ts += (((result/ (2 * channelCount)) * 1000000L) / sampleRate);
							}
						}
					}
				}

				MediaCodec.BufferInfo bufInfo = new MediaCodec.BufferInfo();
				int idx = encoder.dequeueOutputBuffer(bufInfo, QUEUE_TIMEOUT);
				if (idx >= 0) {
					ByteBuffer outBuf = encoder.getOutputBuffer(idx);
					if (outBuf != null) {
						outBuf.rewind();

						finished = (bufInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
						if (finished) {
							bufInfo.presentationTimeUs = ts;
						}

						if ((bufInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
							Log.d(TAG, "encode<mux>: skipping codec config");
						}
						else {
							muxer.writeSampleData(trackId, outBuf, bufInfo);
						}
						encoder.releaseOutputBuffer(idx, false);
					}
				}
			}

			ret = 0;
		}
		catch (Exception e) {
			Log.e(TAG, "encode: failed with exception: " + e);

			mediaFormat = null;
			ret = -1;
		}
		finally {
			if (encoder != null) {
				encoder.stop();
				encoder.release();
			}
			if (muxer != null) {
				muxer.stop();
				muxer.release();
			}
			if (fc != null) {
				try {
					fc.close();
				}
				catch(Exception e) {
				}
			}
		}

	        return ret;
	}
}
