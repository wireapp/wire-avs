package com.waz.avs;


public class VideoCapturerInfo {		
	public static final int FACING_UNKNOWN = 0;
	public static final int FACING_BACK    = 1;
	public static final int FACING_FRONT   = 2;
		
	public int facing = FACING_UNKNOWN;
	public int orientation = -1;
	public int dev = -1;

	public VideoCapturerInfo() {
	}		
}

