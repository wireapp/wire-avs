/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
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
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef __APPLE__
#include <unistd.h>
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include <re.h>
#include <avs.h>
#include <cbox.h>
#include <gtest/gtest.h>


struct peer {
	struct le le;
	struct device *device;   /* N instances (pointer) */
	CBoxSession *session;    /* N instances (one per peer) */
	char sid[4];
};

struct device {
	struct le le;
	struct list peerl;       /* List of peer devices/sessions */
	char name[64];
	char path[256];

	/* cryptobox states: */
	CBox *box;
	CBoxVec *prekey;
};


static uint8_t const hello_msg[] = "Hello Bob!";


static struct peer *device_find_peer(const struct device *device,
				     struct device *peer_device);


static void peer_destructor(void *data)
{
	struct peer *peer = (struct peer *)data;

	list_unlink(&peer->le);

	peer->device = NULL;
	if (peer->session)
		cbox_session_close(peer->session);
}


static void device_destructor(void *data)
{
	struct device *device = (struct device *)data;

	list_unlink(&device->le);

	list_flush(&device->peerl);

	if (device->prekey)
		cbox_vec_free(device->prekey);
	if (device->box)
		cbox_close(device->box);

	/* clean up the directory */
	if (str_isset(device->path))
		store_remove_pathf(device->path);
}


static int device_alloc(struct device **devicep, struct list *lst,
			const char *name)
{
	struct device *device;
	CBoxResult rc;
	char tmp[256], *dir;
	int err = 0;

	device = (struct device *)mem_zalloc(sizeof(*device),
					     device_destructor);
	if (!device)
		return ENOMEM;

	str_ncpy(device->name, name, sizeof(device->name));

	re_snprintf(tmp, sizeof(tmp), "/tmp/ztest_cbox_%s_XXXXXX", name);
	dir = mkdtemp(tmp);
	assert(dir != NULL);

	str_ncpy(device->path, dir, sizeof(device->path));

	rc = cbox_file_open(dir, &device->box);
	assert(rc == CBOX_SUCCESS);
	assert(device->box != NULL);

	rc = cbox_new_prekey(device->box, 1, &device->prekey);
	assert(rc == CBOX_SUCCESS);

	list_append(lst, &device->le, device);

#if 0
	re_printf("[ %s ] new device with prekey %zu bytes (%s)\n",
		  name, cbox_vec_len(device->prekey), device->path);
#endif

 out:
	if (err)
		mem_deref(device);
	else if (devicep)
		*devicep = device;

	return err;
}


static void device_connect(struct device *device, struct device *peer_device)
{
	struct peer *peer;

	ASSERT_TRUE(device != NULL);
	ASSERT_TRUE(peer_device != NULL);

#if 0
	re_printf("@@@ connect %s ----> %s\n",
		  device->name, peer_device->name);
#endif

	peer = device_find_peer(device, peer_device);
	if (peer) {
		re_printf("device %s already connected to %s\n",
			  device->name, peer_device->name);
	}
	ASSERT_TRUE(peer == NULL);


	peer = (struct peer *)mem_zalloc(sizeof(*peer), peer_destructor);
	ASSERT_TRUE(peer != NULL);

	peer->device = peer_device;

	rand_str(peer->sid, sizeof(peer->sid));

	list_append(&device->peerl, &peer->le, peer);
}


static struct peer *device_find_peer(const struct device *device,
				     struct device *peer_device)
{
	struct le *le;

	if (!device || !peer_device)
		return NULL;

	for (le = device->peerl.head; le; le = le->next) {

		struct peer *peer = (struct peer *)le->data;

		if (peer->device == peer_device)
			return peer;
	}

	return NULL;
}


static int device_new_session(struct device *device, struct device *other)
{
	struct peer *peer;
	const char *sid;
	CBoxResult rc;

	peer = device_find_peer(device, other);
	if (!peer)
		return ENOENT;

	sid = peer->sid;

	if (!peer->session) {

		rc = cbox_session_init_from_prekey(device->box,
					   sid,
					   cbox_vec_data(other->prekey),
					   cbox_vec_len(other->prekey),
					   &peer->session);
		assert(rc == CBOX_SUCCESS);
		assert(peer->session != NULL);

#if 0
		re_printf("[ %s ] new session with device %s (sid=%s)\n",
			  device->name, other->name, sid);
#endif
	}

	return 0;
}


static void send_message(struct device *from, struct device *to,
			const uint8_t *msg, size_t msg_len)
{
	struct peer *peer, *peer_inv;
	CBoxVec * cipher = NULL;
	CBoxVec * plain = NULL;
	CBoxResult rc;

	if (!from || !to || !msg || !msg_len)
		return;

	peer = device_find_peer(from, to);
	assert(peer);

	assert(peer->session);
	rc = cbox_encrypt(peer->session, msg, msg_len, &cipher);
	assert(rc == CBOX_SUCCESS);

#if 0
	re_printf("[ %s ] sending message to %s (%zu -> %zu bytes)\n",
		  from->name, to->name, msg_len, cbox_vec_len(cipher));
#endif

	peer_inv = device_find_peer(to, from);
	if (!peer_inv) {
		warning("no inverse session from %s to %s\n",
			to->name, from->name);
	}
	assert(peer_inv);

	if (peer_inv->session) {
		rc = cbox_decrypt(peer_inv->session,
				  cbox_vec_data(cipher),
				  cbox_vec_len(cipher),
				  &plain);
		assert(rc == CBOX_SUCCESS);
	}
	else {
		rc = cbox_session_init_from_message(to->box,
						    peer_inv->sid,
						    cbox_vec_data(cipher),
						    cbox_vec_len(cipher),
						    &peer_inv->session,
						    &plain);
		assert(rc == CBOX_SUCCESS);
		ASSERT_TRUE(peer_inv->session != NULL);
	}

#if 0
	re_printf("[ %s ] received message from %s (%zu -> %zu bytes)\n",
		  to->name, from->name,
		  cbox_vec_len(cipher),
		  cbox_vec_len(plain)
		  );
#endif

	ASSERT_EQ(msg_len, cbox_vec_len(plain));
	ASSERT_TRUE(0 == memcmp(msg, cbox_vec_data(plain), msg_len));

	cbox_vec_free(cipher);
	cbox_vec_free(plain);
}


class cryptoboxtest : public ::testing::Test {

public:
	virtual void SetUp() override
	{
		list_init(&devicel);
	}

	virtual void TearDown() override
	{
		list_flush(&devicel);
	}

	void add_devices(size_t num)
	{
		char name[8];

		for (size_t i=0; i<num; i++) {

			re_snprintf(name, sizeof(name), "%c", init_char++);

			err = device_alloc(NULL, &devicel, name);
			ASSERT_EQ(0, err);
		}
	}

	/* Link all the known devices together */
	void connect_devices()
	{
		struct le *le, *lep;

		for (le = devicel.head; le; le = le->next) {

			struct device *dev = (struct device *)le->data;

			for (lep = devicel.head; lep; lep = lep->next) {

				struct device *devp;

				devp = (struct device *)lep->data;

				if (dev == devp)
					continue;

				if (device_find_peer(dev, devp))
					continue;

				device_connect(dev, devp);
			}
		}
	}

	void debug()
	{
		struct le *le;

		re_printf("DEVICES (%u):\n", list_count(&devicel));

		for (le = devicel.head; le; le = le->next) {
			struct device *dev = (struct device *)le->data;

			re_printf("...device %s (peers = %u)\n",
				  dev->name, list_count(&dev->peerl));
		}
	}

	void send_all()
	{
		struct le *le;

		for (le = devicel.head; le; le = le->next) {

			struct device *dev = (struct device *)le->data, *other;
			struct le *next;

			next = le->next ? le->next : devicel.head;
			other = (struct device *)list_ledata(next);

			err = device_new_session(dev, other);
			ASSERT_EQ(0, err);

			send_message(dev, other, hello_msg, sizeof(hello_msg));
		}
	}

	void verify_devices()
	{
		struct le *le;
		uint32_t num;

		num = list_count(&devicel);

		for (le = devicel.head; le; le = le->next) {
			struct device *dev = (struct device *)le->data, *other;

			ASSERT_EQ(num-1, list_count(&dev->peerl));
		}
	}

protected:
	struct list devicel = LIST_INIT;
	char init_char = 'A';
	int err = 0;
};


TEST_F(cryptoboxtest, send_one_message)
{
	struct device *a, *b;

	add_devices(2);

	/* Connect the 2 devices */
	connect_devices();

	a = (struct device *)devicel.head->data;
	b = (struct device *)devicel.tail->data;

	err = device_new_session(a, b);
	ASSERT_EQ(0, err);

	send_message(a, b, hello_msg, sizeof(hello_msg));
}


TEST_F(cryptoboxtest, send_ten_messages)
{
	struct device *a, *b;

	add_devices(2);

	/* Connect the 2 devices */
	connect_devices();

	a = (struct device *)devicel.head->data;
	b = (struct device *)devicel.tail->data;

	err = device_new_session(a, b);
	ASSERT_EQ(0, err);

	for (int i=0; i<5; i++) {
		size_t len = rand_u16();
		uint8_t *msg = (uint8_t *)mem_alloc(len, NULL);

		rand_bytes(msg, len);

		send_message(a, b, msg, len);

		/* reverse direction */
		send_message(b, a, msg, len);

		mem_deref(msg);
	}
}


/* Test sending messages between 3 devices */
TEST_F(cryptoboxtest, three_devices)
{
	struct le *le;

	add_devices(3);

	/* Connect the devices */
	connect_devices();

	for (le = devicel.head; le; le = le->next) {

		struct device *dev = (struct device *)le->data, *other;
		struct le *next;

		next = le->next ? le->next : devicel.head;
		other = (struct device *)list_ledata(next);

		err = device_new_session(dev, other);
		ASSERT_EQ(0, err);

		send_message(dev, other, hello_msg, sizeof(hello_msg));
	}
}


TEST_F(cryptoboxtest, tmp)
{
#define NUM 3
	struct device *a;

	add_devices(NUM);

	connect_devices();

	//debug();

	/* number of peers is always N-1 */
	a = (struct device *)list_ledata(devicel.head);
	ASSERT_EQ(NUM-1, list_count(&a->peerl));
}


TEST_F(cryptoboxtest, add_send_then_add_send)
{
	add_devices(2);

	connect_devices();

	send_all();

	add_devices(1);

	connect_devices();

	send_all();

	verify_devices();
}
