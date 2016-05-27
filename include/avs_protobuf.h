

/*
 * thin wrapper on top of protoc-generated .h files
 */


#ifdef __cplusplus
extern "C" {
#endif


#include "proto/messages.pb-c.h"


GenericMessage *generic_message_decode(size_t len, const uint8_t *data);
void generic_message_free(GenericMessage *msg);


#ifdef __cplusplus
}
#endif
