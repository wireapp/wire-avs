#include <re.h>
#include "avs_protobuf.h"


static void *allocator_alloc(void *allocator_data, size_t size)
{
	return mem_alloc(size, NULL);
}


static void allocator_free(void *allocator_data, void *pointer)
{
	mem_deref(pointer);
}


static struct ProtobufCAllocator allocator = {
	.alloc = allocator_alloc,
	.free  = allocator_free
};


GenericMessage *generic_message_decode(size_t len, const uint8_t *data)
{
	return generic_message__unpack(&allocator, len, data);
}


void generic_message_free(GenericMessage *msg)
{
	generic_message__free_unpacked(msg, &allocator);
}
