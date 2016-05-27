enum avs_vidframe_type {
	AVS_VIDFRAME_NV12 = 1,
	AVS_VIDFRAME_NV21,
	AVS_VIDFRAME_I420,
};
	
struct avs_vidframe {
	enum avs_vidframe_type type;
	uint8_t *y;
	uint8_t *u;
	uint8_t *v;
	size_t ys; /* y-stride */
	size_t us; /* u-stride */
	size_t vs; /* v-stride */
	int w; /* width */
	int h; /* height */
	int rotation;
	uint32_t ts;
};
