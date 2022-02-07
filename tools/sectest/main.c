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
#include "system.h"
#include "ccall_wrapper.h"
#include "ecall_wrapper.h"

#define CONV_ID      "0f5a30ee-c708-4589-a622-ce741811afcc"
#define ALICE_ID     "alice"
#define BOB_ID       "bob"

#define TEST(func) run_test(#func, func)
#define _TEST(func) (void)func

#define CHECK_NOT_NULL(x) if ((x)){printf("OK  %s is not NULL\n", #x);}else{printf("ERR %s is NULL\n", #x);passed=false;}
#define CHECK_STR_NOT_EQ(x,y) if (strcmp(x,y)==0){printf("ERR %s(\"%s\") == %s(\"%s\")\n", #x, x, #y, y);passed=false;}else{printf("OK  %s(\"%s\") != %s(\"%s\")\n", #x, x, #y, y);}
#define CHECK_MEM_NOT_EQ(x,y,sz) if (memcmp(x,y,sz)==0){printf("ERR %s(0x%02x%02x...) == %s(0x%02x%02x...)\n", #x, x[0], x[1], #y, y[0], y[1]);passed=false;}else{printf("OK  %s(0x%02x%02x...) != %s(0x%02x%02x...)\n", #x, x[0], x[1], #y, y[0], y[1]);}
#define CHECK_GTZ(x) if ((x) > 0){printf("OK  %s(%d) greater than 0\n", #x, x);}else{printf("ERR %s(%d) not greater than 0\n", #x, x);passed=false;}
#define CHECK_EQZ(x) if ((x) == 0){printf("OK  %s(%d) equals 0\n", #x, x);}else{printf("ERR %s(%d)  does not equal 0\n", #x, x);passed=false;}

typedef bool (testfunc)(void);
static int tests_failed = 0;
static int tests_passed = 0;

static void run_test(const char *name, testfunc *func)
{
	printf("STARTING TEST %s\n", name);
	if (func()) {
		tests_passed++;
		printf("TEST PASSED\n\n");
	}
	else {
		tests_failed++;
		printf("TEST FAILED\n\n");
	}
}


/*
   This test verifes that call and client IDs are unpredictable.
    * alice starts a call, extract the callid, end the call
    * alice starts a new call, extract the callid, end the call
    * assert that the callids are not the same
    * assert that the userids are not the same and not the real id
*/
static bool test_anonymous_ids(void)
{
	char *callid = NULL;
	char *userid = NULL;
	struct ccall_wrapper *alice = NULL;
	bool passed = true;
	int err = 0;

	/* Create a ccall for the first call and start the call */
	printf("    creating first call object and starting call\n");
	alice = init_ccall("alice", CONV_ID, false);
	if (!alice) {
		err = ENOMEM;
		goto out;
	}
	err = ICALL_CALLE(alice->icall, start, ICALL_CALL_TYPE_NORMAL, false);
	if (err)
		goto out;

	/* Capture the call user IDs from the first call */
	printf("    extracted call ID: %s\n", alice->callid);
	printf("    extracted user ID: %s\n", alice->userid);
	err = str_dup(&callid, alice->callid);
	if (err)
		goto out;

	err = str_dup(&userid, alice->userid);
	if (err)
		goto out;

	/* At the end of the call, the ccall object is destroyed */
	alice  = (struct ccall_wrapper*)mem_deref(alice);

	printf("    creating second call object and starting call\n");
	/* Create a ccall for the second call and start the call */
	alice = init_ccall("alice", CONV_ID, false);
	err = ICALL_CALLE(alice->icall, start, ICALL_CALL_TYPE_NORMAL, false);
	if (err)
		goto out;

	printf("    extracted call ID: %s\n", alice->callid);
	printf("    extracted user ID: %s\n", alice->userid);

	/* Compare user and client IDs and check they changed */
	CHECK_NOT_NULL(callid);
	CHECK_NOT_NULL(alice->callid);
	CHECK_STR_NOT_EQ(alice->callid, callid);


	/* Check that neither userid is alices real one and that they differ */
	CHECK_NOT_NULL(userid);
	CHECK_NOT_NULL(alice->userid);
	CHECK_STR_NOT_EQ(alice->userid, "alice");
	CHECK_STR_NOT_EQ(userid, "alice");
	CHECK_STR_NOT_EQ(alice->userid, userid);

out:
	alice  = (struct ccall_wrapper*)mem_deref(alice);
	callid = (char*)mem_deref(callid);
	userid = (char*)mem_deref(userid);

	return passed && (err == 0);
}

static void timer_end_test(void *arg)
{
	(void)arg;
	re_cancel();
}

/*
   This test verifies that client IDs presented by the SFT are the
    anonymised ones.
    * alice starts a call, extract the callid
    * eve joins the call with extracted callid (simulating someone guessing the callid)
    * eve gets alices userid from CONFPART
    * assert that alices userid is not the real id but the anonymous one
*/
static bool test_anonymous_ids_sft(void)
{
	struct ccall_wrapper *alice = NULL;
	struct ccall_wrapper *bob = NULL;
	struct ecall_wrapper *eve = NULL;
	struct tmr tmr;
	struct le *le;
	bool passed = true;
	int err = 0;

	/* Create ccalls for alice and bob */
	alice = init_ccall("alice", CONV_ID, true);
	bob = init_ccall("bob", CONV_ID, true);
	eve = init_ecall("eve", false);

	/* Connect alice and bob for fake Proteus message passing */
	alice->conv_member = bob;
	bob->conv_member = alice;

	/* Set eve as bobs eavesdropper to get eve to try to join once alice and bob
	   are in call */
	bob->eavesdropper = eve;

	printf("    alice starting call\n");
	/* alice starts the call */
	err = ICALL_CALLE(alice->icall, start, ICALL_CALL_TYPE_NORMAL, true);
	if (err)
		goto out;

	/* Set a timer to force end the test if something goes wrong */
	tmr_init(&tmr);
	tmr_start(&tmr, 20000, timer_end_test, NULL);

	/* Start run loop, this will start the call from alice and trigger the
	   events to get all clients in the call
	*/
	run_main_loop();

	LIST_FOREACH(&eve->partl, le) {
		struct econn_group_part *participant = le->data;
		CHECK_STR_NOT_EQ(participant->userid, "alice");
		CHECK_STR_NOT_EQ(participant->clientid, "alice");
		CHECK_STR_NOT_EQ(participant->userid, "bob");
		CHECK_STR_NOT_EQ(participant->clientid, "bob");
	}
out:
	alice  = (struct ccall_wrapper*)mem_deref(alice);
	bob  = (struct ccall_wrapper*)mem_deref(bob);
	eve = (struct ecall_wrapper*)mem_deref(eve);

	return passed && (err == 0);
}

/*
  This test verifies that unauthorised clients don't get media packets
    * alice starts a call, extract the callid
    * bob joins the call legitimately
    * eve joins the call with extracted callid (simulating someone guessing the callid)
    * alice and bob authorise each other (simulating them being in the same conversation)
    * assert that alice and bob do get media from each other
    * eve fakes her side of the auth (alice & bob will not auth eve since she is not in
      the conversation)
    * assert that eve does not get media from alice or bob
*/
static bool test_authorisation(void)
{
	struct ccall_wrapper *alice = NULL;
	struct ccall_wrapper *bob = NULL;
	struct ecall_wrapper *eve = NULL;
	struct tmr tmr;
	bool passed = true;
	int err = 0;

	/* Create ccalls for alice and bob */
	alice = init_ccall("alice", CONV_ID, true);
	bob = init_ccall("bob", CONV_ID, true);
	eve = init_ecall("eve", true);

	/* Connect alice and bob for fake Proteus message passing */
	alice->conv_member = bob;
	bob->conv_member = alice;

	/* Set eve as bobs eavesdropper to get eve to try to join once alice and bob
	   are in call */
	bob->eavesdropper = eve;

	printf("    alice starting call\n");
	/* alice starts the call */
	err = ICALL_CALLE(alice->icall, start, ICALL_CALL_TYPE_NORMAL, true);
	if (err)
		goto out;

	/* Set a timer to force end the test if something goes wrong */
	tmr_init(&tmr);
	tmr_start(&tmr, 20000, timer_end_test, NULL);

	/* Start run loop, this will start the call from alice and trigger the
	   events to get all clients in the call
	*/
	run_main_loop();

	CHECK_GTZ(alice->stats.apkts_recv);
	CHECK_GTZ(bob->stats.apkts_recv);
	CHECK_EQZ(eve->stats.apkts_recv);
out:
	alice  = (struct ccall_wrapper*)mem_deref(alice);
	bob  = (struct ccall_wrapper*)mem_deref(bob);
	eve = (struct ecall_wrapper*)mem_deref(eve);

	return passed && (err == 0);
}

/*
  This test verifies that clients not in the call cannot set the media key
    * alice starts a call
    * bob joins the call legitimately
    * eve (who is in the conversation but not the call) sends the CONFKEY message to try to
      force the key to a known value
    * assert that alice did not set the next key to eves key
    * assert that bob did not set the next key to eves key
*/
static bool test_force_key(void)
{
	uint8_t fake_key[E2EE_SESSIONKEY_SIZE] = "fake session key fake session  ";
	struct ccall_wrapper *alice = NULL;
	struct ccall_wrapper *bob = NULL;
	struct tmr tmr;
	bool passed = true;
	int err = 0;

	/* Create ccalls for alice and bob */
	alice = init_ccall("alice", CONV_ID, true);
	bob = init_ccall("bob", CONV_ID, true);

	/* Connect alice and bob for fake Proteus message passing */
	alice->conv_member = bob;
	bob->conv_member = alice;

	ccall_attempt_force_key(alice, fake_key);
	ccall_attempt_force_key(bob, fake_key);
	printf("    alice starting call\n");
	err = ICALL_CALLE(alice->icall, start, ICALL_CALL_TYPE_NORMAL, true);
	if (err)
		goto out;

	/* Set a timer to force end the test if something goes wrong */
	tmr_init(&tmr);
	tmr_start(&tmr, 20000, timer_end_test, NULL);

	/* Start run loop, this will start the call from alice and trigger the
	   events to get all clients in the call
	*/
	run_main_loop();

	CHECK_MEM_NOT_EQ(alice->attempt_key, alice->read_key, E2EE_SESSIONKEY_SIZE);
	CHECK_MEM_NOT_EQ(bob->attempt_key, bob->read_key, E2EE_SESSIONKEY_SIZE);
out:
	alice  = (struct ccall_wrapper*)mem_deref(alice);
	bob  = (struct ccall_wrapper*)mem_deref(bob);

	return passed && (err == 0);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("usage %s <sft url>\n", argv[0]);
		return -1;
	}
	init_system(argv[1]);

	TEST(test_anonymous_ids);
	TEST(test_anonymous_ids_sft);
	TEST(test_authorisation);
	TEST(test_force_key);

	printf("%d tests passed, %d tests failed\n", tests_passed, tests_failed);
	return tests_failed == 0 ? 0 : -1;
}

