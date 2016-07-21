#ifdef __cplusplus
extern "C" {
#endif

struct video_renderer;

int video_renderer_alloc(struct video_renderer **vrp,  int w, int h,
			 bool rounded, void *arg);

int video_renderer_start(struct video_renderer *vr);
	
int video_renderer_handle_frame(struct video_renderer *vr,
				struct avs_vidframe *vf);

void video_renderer_set_should_fill(struct video_renderer *vr,
				    bool should_fill);

void *video_renderer_arg(struct video_renderer *vr);

#ifdef __cplusplus
}
#endif

