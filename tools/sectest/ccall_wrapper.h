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

struct ecall_wrapper;

struct ccall_wrapper {
	struct icall *icall;
	char *callid;
	char *userid;
	char *name;
	bool contact_sft;
	struct ccall_wrapper *conv_member;
	struct tmr call_timer;
	struct ecall_wrapper *eavesdropper;
	struct iflow_stats stats;
	struct tmr key_timer;
	bool attempt_force_key;
	uint32_t target_mls_key;
	uint32_t next_mls_key;
	uint32_t current_key_idx;
	uint8_t attempt_key[E2EE_SESSIONKEY_SIZE];
	uint8_t read_key[E2EE_SESSIONKEY_SIZE];
	uint64_t test_timeout;
};

struct ccall_wrapper *init_ccall(const char *name,
			         const char *convid,
				 bool contact_sft,
				 bool mls_call);

void ccall_wrapper_set_eavesdropper(struct ccall_wrapper *wrapper,
				    struct ecall_wrapper *eavesdropper);

void ccall_attempt_force_key(struct ccall_wrapper *wrapper, 
			     uint8_t set_key[E2EE_SESSIONKEY_SIZE]);

void ccall_set_target_mls_key(struct ccall_wrapper *wrapper, 
			      uint32_t mls_key);

