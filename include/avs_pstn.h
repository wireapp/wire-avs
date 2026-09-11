#ifdef __cplusplus
extern "C" {
#endif

typedef void (pstn_adm_h)(const char *convid,
			  void *adm,
			  bool added,
			  void *arg);
	
int   pstn_adm_handler_register(pstn_adm_h *admh, void *arg);
void  pstn_adm_handler_unregister(pstn_adm_h *admh, void *arg);
int   pstn_adm_register(const char *convid, void *adm);
int   pstn_adm_unregister(const char *convid);
void *pstn_adm_find(const char *convid);
	
void pstn_play_read(void *adm, int16_t *sampv, size_t sampc);
void pstn_rec_write(void *adm, const int16_t *sampv, size_t sampc);
	

#ifdef __cplusplus
}
#endif
	
