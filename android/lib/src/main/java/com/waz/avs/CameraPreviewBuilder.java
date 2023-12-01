/*
 * Wire
 * Copyright (C) 2023 Wire Swiss GmbH
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
package com.waz.avs;

import android.content.Context;
import android.graphics.Color;
import android.widget.LinearLayout;
import android.widget.LinearLayout.LayoutParams;

import androidx.camera.view.PreviewView;

public class CameraPreviewBuilder {

    private Context context;

    private int backgroundColor = Color.BLACK;
    private PreviewView.ScaleType scaleType = PreviewView.ScaleType.FIT_CENTER;

    public CameraPreviewBuilder(Context context) {
        this.context = context;
    }

    public CameraPreviewBuilder setBackgroundColor(int backgroundColor) {
        this.backgroundColor = backgroundColor;
        return this;
    }

    public CameraPreviewBuilder shouldFill(boolean shouldFill) {
        if (shouldFill) {
            scaleType = PreviewView.ScaleType.FILL_CENTER;
        }
        return this;
    }

    public PreviewView build() {
        PreviewView preview = new PreviewView(context);
	preview.setImplementationMode(PreviewView.ImplementationMode.PERFORMANCE);
	preview.setLayoutParams(new LinearLayout.LayoutParams(
		LinearLayout.LayoutParams.MATCH_PARENT,
		LinearLayout.LayoutParams.MATCH_PARENT));	
        preview.setBackgroundColor(backgroundColor);
        preview.setScaleType(scaleType);

        return preview;
    }

}
