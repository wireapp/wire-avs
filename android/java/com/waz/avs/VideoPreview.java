package com.waz.avs;

import android.content.Context;

import android.util.Log;
import android.view.TextureView;



public class VideoPreview extends TextureView
{
	private float aspectRatio = 4.0f/3.0f;

	public VideoPreview(Context ctx) {
		super(ctx);
	}
	
	public void setAspectRatio(float ar) {
		this.aspectRatio = ar;
	}
	
	@Override
	protected void onMeasure(int wSpec, int hSpec) {
		final int width;
		final int height;
		int w;
		int h;

		width = MeasureSpec.getSize(wSpec);
		height = MeasureSpec.getSize(hSpec);
		
		if (wSpec > hSpec) {
			w = width;
			h = (int)((float)width/this.aspectRatio);
		}
		else {
			w = (int)(this.aspectRatio * (float)height);
			h = height;
		}

		setMeasuredDimension(w, h);
	}
}
