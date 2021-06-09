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

#include <re.h>
#include <avs.h>
#include <gtest/gtest.h>
#include <sys/time.h>
#include "ztest.h"


#define MQUEUE_DCE_MESSAGE 0
#define MQUEUE_START_TEST 1

#define TEST_STEP_CONNECT               0
#define TEST_STEP_OPEN_CHANNELS         (TEST_STEP_CONNECT + 1)
#define TEST_STEP_SEND_MSG_FROM_A_C1    (TEST_STEP_OPEN_CHANNELS + 1)
#define TEST_STEP_SEND_MSG_FROM_B_C1    (TEST_STEP_SEND_MSG_FROM_A_C1 + 1)
#define TEST_STEP_CLOSE_C1              (TEST_STEP_SEND_MSG_FROM_B_C1 + 1)
#define TEST_STEP_SEND_MSG_FROM_A_C2    (TEST_STEP_CLOSE_C1 + 1)
#define TEST_STEP_CLOSE_C2              (TEST_STEP_SEND_MSG_FROM_A_C2 + 1)
#define TEST_STEP_FREE_DCE              (TEST_STEP_CLOSE_C2 + 1)


//#define DBG_TEST
#define DATA_SIZE 30

class Dce;


struct mq_data {
	uint8_t *pkt;
	size_t len;
	struct client *c;
};

struct channel_owner {
    int ch;
    struct dce_channel *dce_ch;
    char snd_data[DATA_SIZE];
    char rcv_data[DATA_SIZE];
    char label[DATA_SIZE];
    char protocol[DATA_SIZE];
    unsigned n_received;
    bool open;
};

struct client {
    Dce *fix;
    struct dce *dce;
    bool dtls_role;
    
    int packet_to_drop;
    int packet_cnt;
    
    unsigned n_established;
    
    struct channel_owner co[2];
};


/* prototypes */
static void test_command_handler(void *arg);


class Dce : public ::testing::Test {

public:

	virtual void SetUp() override
	{
		int err;

		log_set_min_level(LOG_LEVEL_WARN);
		log_enable_stderr(true);

		err = dce_init();
		ASSERT_EQ(0, err);

		err = mqueue_alloc(&mq, mq_handler, NULL);
		ASSERT_EQ(0, err);
	}

	virtual void TearDown() override
	{
		mem_deref(mq);
		dce_close();
	}

	static void mq_handler(int id, void *data, void *arg)
	{
		if (id == MQUEUE_DCE_MESSAGE) {
			struct mq_data *md = (struct mq_data *)data;
        
			// printf("mq_handler: handling data len=%u\n",
			// (uint32_t)md->len);
        
			dce_recv_pkt(md->c->dce, md->pkt, md->len);
            
			mem_deref(md);
		}
		else if ( id == MQUEUE_START_TEST ) {
			test_command_handler(data);
		}
		else {
			printf("unknown mqueeu id \n");
		}
	}

public:
	struct mqueue *mq = nullptr;

protected:
	int ncalls = 0;
};

// XXX move to the Fixture ?
static struct client A;
static struct client B;


struct test_control {
    int count;
    int wait_count;
    struct tmr tmr_command;
};

static void init_channel_owner(struct channel_owner *co)
{
    co->dce_ch = NULL;
    memset(co->rcv_data,0,sizeof(co->rcv_data));
    memset(co->snd_data,0,sizeof(co->snd_data));
    memset(co->label,0,sizeof(co->label));
    co->n_received = 0;
}

static void init_client(struct client *c, Dce *fix, bool dtls_role_active, int packet_to_drop)
{
	memset(c, 0, sizeof(*c));

	c->dce = NULL;
	c->dtls_role = dtls_role_active;
	c->packet_to_drop = packet_to_drop;
	c->packet_cnt = 0;
	c->n_established = 0;
	init_channel_owner(&c->co[0]);
	init_channel_owner(&c->co[1]);
	c->fix = fix;
}

static int set_channel_owner_snd_data(struct channel_owner *co, const char *snd_data)
{
	if(strlen(snd_data) < sizeof(co->snd_data)){
		memcpy(co->snd_data,snd_data,strlen(snd_data));
		return 0;
	} else {
		return -1;
	}
}

static void md_dtor(void *arg)
{
	struct mq_data *md = (struct mq_data *)arg;

	mem_deref(md->pkt);
}

#define WAIT_MS 100
#define MAX_WAIT 1000
static void test_command_handler(void *arg)
{
        struct test_control *ptc = (struct test_control *)arg;
	int err;

#ifdef DBG_TEST
    printf("tmr_command_handler count = %d \n", ptc->count);
#endif
    if(ptc->count > TEST_STEP_FREE_DCE){
        re_cancel();
        return;
    }

    if(ptc->count == TEST_STEP_CONNECT){
        ptc->wait_count = 0;
#ifdef DBG_TEST
        re_fprintf(stderr, "%H\n", dce_status, A.dce);
        re_fprintf(stderr, "%H\n", dce_status, B.dce);
#endif
        err = dce_connect(A.dce, A.dtls_role);
	ASSERT_EQ(0, err);
        err = dce_connect(B.dce, B.dtls_role);
	ASSERT_EQ(0, err);

        tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
    } else if(ptc->count == TEST_STEP_OPEN_CHANNELS){
#ifdef DBG_TEST
        re_fprintf(stderr, "%H\n", dce_status, A.dce);
        re_fprintf(stderr, "%H\n", dce_status, B.dce);
#endif
        if(A.n_established && B.n_established){
            int tmp;
            if(A.co[0].dce_ch){
                dce_open_chan(A.dce, A.co[0].dce_ch);
            }
            if(A.co[1].dce_ch){
                dce_open_chan(A.dce, A.co[1].dce_ch);
            }
            tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
        } else {
            info("Waiting for established state: A = %d B = %d \n",
		   A.n_established, B.n_established);

            tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
            return;
        }
    } else if(ptc->count == TEST_STEP_SEND_MSG_FROM_A_C1){
#ifdef DBG_TEST
        re_fprintf(stderr, "%H\n", dce_status, A.dce);
        re_fprintf(stderr, "%H\n", dce_status, B.dce);
#endif
        if( dce_is_chan_open(A.co[0].dce_ch) || ptc->wait_count > MAX_WAIT) {
            int ret = dce_send(A.dce, A.co[0].dce_ch, &A.co[0].snd_data[0], sizeof(A.co[0].snd_data));
            ptc->wait_count = 0;
            tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
        } else {
            info("Waiting for A to have an open channel wait_count = %d \n",
		 ptc->wait_count);
            ptc->wait_count++;
            tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
            return;
        }
    } else if(ptc->count == TEST_STEP_SEND_MSG_FROM_B_C1){
#ifdef DBG_TEST
        re_fprintf(stderr, "%H\n", dce_status, A.dce);
        re_fprintf(stderr, "%H\n", dce_status, B.dce);
#endif
        if( dce_is_chan_open(B.co[0].dce_ch) || ptc->wait_count > MAX_WAIT) {
            int ret = dce_send(B.dce, B.co[0].dce_ch, &B.co[0].snd_data[0], sizeof(B.co[0].snd_data));
            ptc->wait_count = 0;
            tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
        } else {
            info("Waiting for B to have an open channel wait_count = %d \n",
		 ptc->wait_count);
            ptc->wait_count++;
            tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
            return;
        }
    } else if(ptc->count == TEST_STEP_CLOSE_C1){
#ifdef DBG_TEST
        re_fprintf(stderr, "%H\n", dce_status, A.dce);
        re_fprintf(stderr, "%H\n", dce_status, B.dce);
#endif
        if((dce_snd_dry(A.dce) && dce_snd_dry(B.dce)) || ptc->wait_count > MAX_WAIT){
            dce_close_chan(A.dce, A.co[0].dce_ch);
            tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
        } else {
            info("waiting to recieve message A = %s B = %s wait_count = %d \n",
		 A.co[0].rcv_data, B.co[0].rcv_data, ptc->wait_count);
            ptc->wait_count++;
            tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
            return;
        }
    } else if(ptc->count == TEST_STEP_SEND_MSG_FROM_A_C2){
#ifdef DBG_TEST
        re_fprintf(stderr, "%H\n", dce_status, A.dce);
        re_fprintf(stderr, "%H\n", dce_status, B.dce);
#endif
        if(!A.co[1].dce_ch){
            ptc->wait_count = 0;
            tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
        } else {
            if( dce_is_chan_open(A.co[1].dce_ch) || ptc->wait_count > MAX_WAIT) {
                int ret = dce_send(A.dce, A.co[1].dce_ch, &A.co[1].snd_data[0], sizeof(A.co[1].snd_data));
                ret = dce_send(A.dce, A.co[0].dce_ch, &A.co[1].snd_data[0], sizeof(A.co[1].snd_data)); // this should fail
                ptc->wait_count = 0;
                tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
            } else {
                info("Waiting for A to have an open channel wait_count = %d \n",
		     ptc->wait_count);
                ptc->wait_count++;
                tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
                return;
            }
        }
    } else if(ptc->count == TEST_STEP_CLOSE_C2){
#ifdef DBG_TEST
        re_fprintf(stderr, "%H\n", dce_status, A.dce);
        re_fprintf(stderr, "%H\n", dce_status, B.dce);
#endif
        if(!A.co[1].dce_ch){
            ptc->wait_count = 0;
            tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
        } else {
            if((dce_snd_dry(A.dce) && dce_snd_dry(B.dce)) || ptc->wait_count > MAX_WAIT){
                dce_close_chan(A.dce, A.co[1].dce_ch);
                tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
            } else {
                info("waiting to recieve message A = %s B = %s wait_count = %d \n", A.co[1].rcv_data, B.co[1].rcv_data, ptc->wait_count);
                ptc->wait_count++;
                tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
                return;
            }
        }
    } else if(ptc->count == TEST_STEP_FREE_DCE){
#ifdef DBG_TEST
        re_fprintf(stderr, "%H\n", dce_status, A.dce);
        re_fprintf(stderr, "%H\n", dce_status, B.dce);
#endif
        mem_deref(A.dce);
        mem_deref(B.dce);
        tmr_start(&ptc->tmr_command, WAIT_MS, test_command_handler, arg);
    }
    
    ptc->count++;
}


static int dce_send_handler(uint8_t *pkt, size_t len, void *arg)
{
	struct client *c = (struct client *)arg;
	struct mq_data *md;
	
	//printf("dce wants to send packet on %p of len=%u\n",
	//       c->dce, (uint32_t)len);

	if(c->packet_to_drop == c->packet_cnt && c->packet_cnt > 0){
		c->packet_cnt++;
		return 0;
	}
	c->packet_cnt++;
    
	md = (struct mq_data *)mem_zalloc(sizeof(*md), md_dtor);
	md->pkt = (uint8_t *)mem_alloc(len, NULL);
	memcpy(md->pkt, pkt, len);
	md->len = len;
	
	if (c == &A) {
		//printf("sending to B\n");
		md->c = &B;
	}
	else if (c == &B) {
		//printf("sending to A\n");
		md->c = &A;
	}

	mqueue_push(c->fix->mq, MQUEUE_DCE_MESSAGE, (void *)md);
    
	return 0;
}


static void dce_estab_handler(void *arg)
{
	struct client *c = (struct client *)arg;
	++c->n_established;
}


static void dce_open_handler(int sid, const char *label, const char *protocol, void *arg)
{
	struct channel_owner *c = (struct channel_owner *)arg;
 
	if(strlen(label) < sizeof(c->label)){
		memcpy(c->label,label,strlen(label));
	}
    
	if(strlen(protocol) < sizeof(c->protocol)){
		memcpy(c->protocol,protocol,strlen(protocol));
	}
	c->ch = sid;
	c->open = true;
}

static void dce_close_handler(int chid, const char *label, const char *protocol, void *arg)
{
	struct channel_owner *c = (struct channel_owner *)arg;
	c->open = false;
}

static void dce_rcv_data_handler(int chid, uint8_t *data, size_t len,
				 void *arg)
{
	struct channel_owner *c = (struct channel_owner *)arg;

	++c->n_received;

	if (len <= DATA_SIZE) {
		memcpy(c->rcv_data, data, len);
	}
}

static bool is_even(int x)
{
    return !(bool)(x & (int)1);
}

TEST_F(Dce, extra_init_and_close)
{
	int err;

	dce_close();
	dce_close();
	dce_close();

	err = dce_init();
	ASSERT_EQ(0, err);
	err = dce_init();
	ASSERT_EQ(0, err);

	/* verify clients */
	ASSERT_EQ(0, A.n_established);
	ASSERT_EQ(0, B.n_established);
	ASSERT_EQ(0, A.co[0].n_received);
	ASSERT_EQ(0, B.co[0].n_received);
}


TEST_F(Dce, open_close_send_msg)
{
    int err;
    struct test_control tc = {0};
    char label[] = "channel1";
    
    mqueue_push(mq, MQUEUE_START_TEST, &tc);
    
    init_client(&A, this, true, 0);
    set_channel_owner_snd_data(&A.co[0], "Hello from A on channel 1");
    err = dce_alloc(&A.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &A);
    ASSERT_EQ(0, err);

    err = dce_channel_alloc(&A.co[0].dce_ch,
			    A.dce,
			    label,
			    "",
			    NULL,
			    dce_open_handler,
			    dce_close_handler,
			    dce_rcv_data_handler,
			    &A.co[0]);
    ASSERT_EQ(0, err);
    
    init_client(&B, this, false, 0);
    set_channel_owner_snd_data(&B.co[0], "Hello from B on channel 1");
    err = dce_alloc(&B.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &B);
    ASSERT_EQ(0, err);

    err = dce_channel_alloc(&B.co[0].dce_ch,
			    B.dce,
			    label,
			    "",
			    NULL,
			    dce_open_handler,
			    dce_close_handler,
			    dce_rcv_data_handler,
			    &B.co[0]);
    ASSERT_EQ(0, err);
    
    err = re_main_wait(20000);
    ASSERT_EQ(0, err);
    
    /* Verify results after test is completed */
	ASSERT_EQ(1, A.n_established);
	ASSERT_EQ(1, B.n_established);
	ASSERT_EQ(1, A.co[0].n_received);
	ASSERT_EQ(1, B.co[0].n_received);

	ASSERT_EQ(0, strcmp(A.co[0].rcv_data, B.co[0].snd_data));
	ASSERT_EQ(0, strcmp(B.co[0].rcv_data, A.co[0].snd_data));
    
	ASSERT_EQ(0, strcmp(A.co[0].label, label));
	ASSERT_EQ(0, strcmp(B.co[0].label, label));
    
	ASSERT_EQ(is_even(A.co[0].ch), A.dtls_role);
	ASSERT_EQ(is_even(B.co[0].ch), A.dtls_role);
}

TEST_F(Dce, open_close_send_msg_reverse_dtls_roles)
{
	int err;
	struct test_control tc = {0};
	char label[] = "channel1";
    
	mqueue_push(mq, MQUEUE_START_TEST, &tc);
    
	init_client(&A, this, false, 0);
	set_channel_owner_snd_data(&A.co[0], "Hello from A on channel 1");
	err = dce_alloc(&A.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &A);
	ASSERT_EQ(0, err);
    
	err = dce_channel_alloc(&A.co[0].dce_ch,
                            A.dce,
                            label,
                            "",
                            NULL,
                            dce_open_handler,
                            dce_close_handler,
                            dce_rcv_data_handler,
                            &A.co[0]);
	ASSERT_EQ(0, err);
    
	init_client(&B, this, true, 0);
	set_channel_owner_snd_data(&B.co[0], "Hello from B on channel 1");
	err = dce_alloc(&B.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &B);
	ASSERT_EQ(0, err);
    
	err = dce_channel_alloc(&B.co[0].dce_ch,
                            B.dce,
                            label,
                            "",
                            NULL,
                            dce_open_handler,
                            dce_close_handler,
                            dce_rcv_data_handler,
                            &B.co[0]);
	ASSERT_EQ(0, err);
    
	err = re_main_wait(20000);
	ASSERT_EQ(0, err);
    
	/* Verify results after test is completed */
	ASSERT_EQ(1, A.n_established);
	ASSERT_EQ(1, B.n_established);
	ASSERT_EQ(1, A.co[0].n_received);
	ASSERT_EQ(1, B.co[0].n_received);
    
	ASSERT_EQ(0, strcmp(A.co[0].rcv_data, B.co[0].snd_data));
	ASSERT_EQ(0, strcmp(B.co[0].rcv_data, A.co[0].snd_data));
    
	ASSERT_EQ(0, strcmp(A.co[0].label, label));
	ASSERT_EQ(0, strcmp(B.co[0].label, label));
    
	ASSERT_EQ(is_even(A.co[0].ch), A.dtls_role);
	ASSERT_EQ(is_even(B.co[0].ch), A.dtls_role);
}

TEST_F(Dce, open_close_send_msg_two_channels)
{
    int err;
    struct test_control tc = {0};
    char label1[] = "channel1";
    char label2[] = "channel2";
    
	/* we dont want to see the warnings.. */
	log_set_min_level(LOG_LEVEL_ERROR);

    mqueue_push(mq, MQUEUE_START_TEST, &tc);
    
    init_client(&A, this, true, 0);
    set_channel_owner_snd_data(&A.co[0], "Hello from A on channel 1");
    set_channel_owner_snd_data(&A.co[1], "Hello from A on channel 2");
    err = dce_alloc(&A.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &A);
    ASSERT_EQ(0, err);
    
    err = dce_channel_alloc(&A.co[0].dce_ch,
			    A.dce,
			    label1,
			    "",
			    NULL,
			    dce_open_handler,
			    dce_close_handler,
			    dce_rcv_data_handler,
			    &A.co[0]);
    ASSERT_EQ(0, err);
    
    err = dce_channel_alloc(&A.co[1].dce_ch,
			    A.dce,			    
			    label2,
			    "",
			    NULL,
			    dce_open_handler,
			    dce_close_handler,
			    dce_rcv_data_handler,
			    &A.co[1]);
    ASSERT_EQ(0, err);

    init_client(&B, this, false, 0);
    set_channel_owner_snd_data(&B.co[0], "Hello from B on channel 1");
    err = dce_alloc(&B.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &B);
    ASSERT_EQ(0, err);
    
    err = dce_channel_alloc(&B.co[0].dce_ch,
			    B.dce,
			    label1,
			    "",
			    NULL,
			    dce_open_handler,
			    dce_close_handler,
			    dce_rcv_data_handler,
			    &B.co[0]);
    ASSERT_EQ(0, err);
    
    err = dce_channel_alloc(&B.co[1].dce_ch,
			    B.dce,
			    label2,
			    "",
			    NULL,
			    dce_open_handler,
			    dce_close_handler,
			    dce_rcv_data_handler,
			    &B.co[1]);
    ASSERT_EQ(0, err);
    
    err = re_main_wait(20000);
    ASSERT_EQ(0, err);
    
    /* Verify results after test is completed */
    ASSERT_EQ(1, A.n_established);
    ASSERT_EQ(1, B.n_established);
    ASSERT_EQ(1, A.co[0].n_received);
    ASSERT_EQ(1, B.co[0].n_received);
    
    ASSERT_EQ(0, strcmp(A.co[0].rcv_data, B.co[0].snd_data));
    ASSERT_EQ(0, strcmp(B.co[0].rcv_data, A.co[0].snd_data));
    
    ASSERT_EQ(0, strcmp(A.co[0].label, label1));
    ASSERT_EQ(0, strcmp(A.co[1].label, label2));
    
    ASSERT_EQ(0, A.co[1].n_received);
    ASSERT_EQ(1, B.co[1].n_received);
    
    ASSERT_EQ(0, strcmp(B.co[1].rcv_data, A.co[1].snd_data));

    ASSERT_EQ(0, strcmp(B.co[0].label, label1));
    ASSERT_EQ(0, strcmp(B.co[1].label, label2));

    ASSERT_EQ(is_even(A.co[0].ch), A.dtls_role);
    ASSERT_EQ(is_even(A.co[1].ch), A.dtls_role);
    ASSERT_EQ(is_even(B.co[0].ch), A.dtls_role);
    ASSERT_EQ(is_even(B.co[1].ch), A.dtls_role);
}

TEST_F(Dce, open_close_send_msg_lossy)
{
    int err;
    struct test_control tc = {0};
    char label[] = "channel1";
    
    mqueue_push(mq, MQUEUE_START_TEST, &tc);
    
    init_client(&A, this, true, 6);
    set_channel_owner_snd_data(&A.co[0], "Hello from A on channel 1");
    err = dce_alloc(&A.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &A);
    ASSERT_EQ(0, err);
    
    err = dce_channel_alloc(&A.co[0].dce_ch,
			    A.dce,
			    label,
			    "",
			    NULL,
			    dce_open_handler,
			    dce_close_handler,
			    dce_rcv_data_handler,
			    &A.co[0]);
    ASSERT_EQ(0, err);
    
    init_client(&B, this, false, 0);
    set_channel_owner_snd_data(&B.co[0], "Hello from B on channel 1");
    err = dce_alloc(&B.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &B);
    ASSERT_EQ(0, err);
    
    err = dce_channel_alloc(&B.co[0].dce_ch,
			    B.dce,
			    label,
			    "",
			    NULL,
			    dce_open_handler,
			    dce_close_handler,
			    dce_rcv_data_handler,
			    &B.co[0]);
    ASSERT_EQ(0, err);
    
    err = re_main_wait(20000);
    ASSERT_EQ(0, err);
    
    /* Verify results after test is completed */
    ASSERT_EQ(1, A.n_established);
    ASSERT_EQ(1, B.n_established);
    ASSERT_EQ(1, A.co[0].n_received);
    ASSERT_EQ(1, B.co[0].n_received);
    
    ASSERT_EQ(0, strcmp(A.co[0].rcv_data, B.co[0].snd_data));
    ASSERT_EQ(0, strcmp(B.co[0].rcv_data, A.co[0].snd_data));
    
    ASSERT_EQ(0, strcmp(A.co[0].label, label));
    ASSERT_EQ(0, strcmp(B.co[0].label, label));
}

TEST_F(Dce, alloc)
{
    int err;
    
    init_client(&A, this, true, 0);
    init_client(&B, this, false, 0);
    
    err = dce_alloc(&A.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &A);
    ASSERT_EQ(0, err);

    err = dce_alloc(&B.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &B);
    ASSERT_EQ(0, err);
    
    err = re_main_wait(100);
    
    mem_deref(A.dce);
    mem_deref(B.dce);
    
    /* Verify results after test is completed */
    ASSERT_EQ(0, A.n_established);
    ASSERT_EQ(0, B.n_established);
    ASSERT_EQ(0, A.packet_cnt);
    ASSERT_EQ(0, B.packet_cnt);
    ASSERT_EQ(0, A.co[0].n_received);
    ASSERT_EQ(0, B.co[0].n_received);
    
}

TEST_F(Dce, alloc_after_close)
{
    int err;
    
    dce_close();
    
    init_client(&A, this, true, 0);
    init_client(&B, this, false, 0);

    err = dce_alloc(&A.dce,
                    dce_send_handler,
                    dce_estab_handler,
                    &A);
    
    ASSERT_EQ(EINVAL, err);

    /* Verify results after test is completed */
	ASSERT_EQ(0, A.n_established);
	ASSERT_EQ(0, B.n_established);
	ASSERT_EQ(0, A.co[0].n_received);
	ASSERT_EQ(0, B.co[0].n_received);

}
