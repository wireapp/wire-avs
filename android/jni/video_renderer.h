#ifdef __cplusplus
extern "C" {
#endif

struct video_renderer;

int video_renderer_alloc(struct video_renderer **vrp,  int w, int h,
			 bool rounded,
			 const char *userid,
			 const char *clientid,
			 void *arg);
void video_renderer_detach(struct video_renderer *vr);

int video_renderer_start(struct video_renderer *vr);
	
int video_renderer_handle_frame(struct video_renderer *vr,
				struct avs_vidframe *vf);

int video_renderer_render_frame(struct video_renderer *vr);

void video_renderer_set_should_fill(struct video_renderer *vr,
				    bool should_fill);

void video_renderer_set_fill_ratio(struct video_renderer *vr,
				   float fill_ratio);

void video_renderer_set_dict(struct video_renderer *vr,
			     bool dict);
bool video_renderer_get_dict(struct video_renderer *vr);

const char *video_renderer_userid(struct video_renderer *vr);
const char *video_renderer_clientid(struct video_renderer *vr);
void *video_renderer_arg(struct video_renderer *vr);

#ifdef __cplusplus
}
#endif

