#ifdef __cplusplus
extern "C" {
#endif

bool msys_platform_is_active(void);

/* Internal: register the actual calling-loop thread before runtime publication. */
void msystem_set_mute_owner(void);
/* Accept/coalesce a media-thread reset for that loop. Close may cancel it.
 * ENOSYS/ECANCELED or a hard queue error means the request was not accepted. */
int msystem_request_mute_reset(void);

#ifdef __cplusplus
}
#endif
