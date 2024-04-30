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
			
			extractor.selectTrack(i);		
			decoder = MediaCodec.createDecoderByType(mimeType);

			boolean eos = false;
			boolean finished = false;

			mediaFormat.setInteger(MediaFormat.KEY_PCM_ENCODING, AudioFormat.ENCODING_PCM_16BIT);
			decoder.configure(mediaFormat, null, null, 0);
			decoder.start();
			while(!finished) {
				if (!eos) {
					int idx = decoder.dequeueInputBuffer(QUEUE_TIMEOUT);
					if (idx >= 0) {
						ByteBuffer inputBuffer = decoder.getInputBuffer(idx);
						if (inputBuffer == null)
							continue;

						long sampleTime = 0;
						int result;

						result = extractor.readSampleData(inputBuffer, 0);
						if (result < 0) {
							decoder.queueInputBuffer(idx, 0, 0, -1, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
							eos = true;
						}
						else {
							sampleTime = extractor.getSampleTime();
							decoder.queueInputBuffer(idx, 0, result, sampleTime, 0);
							extractor.advance();
						}
					}
				}

				MediaCodec.BufferInfo bufInfo = new MediaCodec.BufferInfo();

				int idx = decoder.dequeueOutputBuffer(bufInfo, QUEUE_TIMEOUT);
				if (idx >= 0) {
					ByteBuffer outBuf = decoder.getOutputBuffer(idx);
					if (outBuf != null) {
						outBuf.rewind();

						fc.write(outBuf);
						decoder.releaseOutputBuffer(idx, false);

						finished = (bufInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
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
