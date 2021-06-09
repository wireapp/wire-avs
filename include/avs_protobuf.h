

/*
 * thin wrapper on top of protoc-generated .h files
 */


#ifdef __cplusplus
extern "C" {
#endif


#include "proto/messages.pb-c.h"


/*
 * low-level API
 */

GenericMessage *generic_message_decode(size_t len, const uint8_t *data);
void generic_message_free(GenericMessage *msg);


/*
 * high-level API
 */

struct protobuf_msg {
	GenericMessage *gm;
};


int protobuf_encode_text(uint8_t *pbuf, size_t *pbuf_len,
			 const char *text_content);
int protobuf_encode_calling(uint8_t *pbuf, size_t *pbuf_len,
			    const char *content);
int protobuf_decode(struct protobuf_msg **msgp, const uint8_t *buf, size_t len);


#ifdef __cplusplus
}
#endif
