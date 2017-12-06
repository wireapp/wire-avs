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
#include "avs.h"
#include "ecall.h"

static void read_u8(uint8_t **buf, size_t len, uint8_t out[])
{
    uint8_t *ptr = *buf;
    for(size_t i = 0; i < len; i++){
        out[i] = *ptr++;
    }
    *buf = ptr;
}

static uint32_t read_u32(uint8_t **buf)
{
    uint32_t out = 0;
    uint8_t *ptr = *buf;
    for(size_t i = 0; i < sizeof(uint32_t); i++){
        out = out << 8;
        out += *ptr++;
    }
    *buf = ptr;
    return out;
}

static void write_u32(uint8_t **buf, uint32_t in)
{
    uint8_t tmp8;
    uint8_t *ptr = *buf;
    for(size_t i = 0; i < sizeof(uint32_t); i++){
        tmp8 = in >> 24;
        *ptr++ = tmp8;
        in = in << 8;
    }
    *buf = ptr;
}


static void handle_file_start_recieved(struct user_data *usrd, uint8_t *data, size_t len)
{
        uint8_t *ptr = data;
    uint32_t nblocks = read_u32(&ptr);
    usrd->file_rcv.nblocks = ntohl(nblocks);
    memcpy(usrd->file_rcv.name, ptr, MAX_FILE_NAME_SIZE);
    ptr += MAX_FILE_NAME_SIZE;

    if (!str_isset(usrd->file_rcv.name)) {
        error("user_data: No file name recieved \n");
        return;
    }
    char buf[strlen(usrd->file_rcv.path) + strlen(usrd->file_rcv.name)];
    if (str_isset(usrd->file_rcv.path)) {
        sprintf(buf, "%s/%s", usrd->file_rcv.path, usrd->file_rcv.name);
    }
    else {
        sprintf(buf, "%s", usrd->file_rcv.name);
    }
    usrd->file_rcv.fp = fopen(buf,"wb");
    if(!usrd->file_rcv.fp){
        error("user_data: cannot open file %s \n", buf);
        return;
    }
    
    size_t data_size = len - (size_t)(ptr - data);
    
    fwrite(ptr, sizeof(uint8_t), data_size, usrd->file_rcv.fp);
    
    SHA256_Init(&usrd->file_rcv.sha256_hash);
    SHA256_Update(&usrd->file_rcv.sha256_hash, ptr, data_size);
    
    usrd->file_rcv.block_cnt = 1;
}

static void handle_file_chunk_recieved(struct user_data *usrd, uint8_t *data, size_t len){
    if(!usrd->file_rcv.fp){
        return;
    }
    
    fwrite(data, sizeof(uint8_t), len, usrd->file_rcv.fp);
    SHA256_Update(&usrd->file_rcv.sha256_hash, data, len);
    
    usrd->file_rcv.block_cnt++;
}

static bool handle_file_end_recieved(struct user_data *usrd, uint8_t *data, size_t len){
    if(!usrd->file_rcv.fp){
        return false;
    }
    
    uint8_t rcv_sha[SHA256_DIGEST_LENGTH];
    uint8_t *snd_sha = data;
    data += SHA256_DIGEST_LENGTH;
    size_t data_size = len - SHA256_DIGEST_LENGTH;
    
    fwrite(data, sizeof(uint8_t), data_size, usrd->file_rcv.fp);
    
    SHA256_Update(&usrd->file_rcv.sha256_hash, data, data_size);
    SHA256_Final(rcv_sha, &usrd->file_rcv.sha256_hash);
    
    usrd->file_rcv.block_cnt++;
    
    bool success = true;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++){
        if(rcv_sha[i] != snd_sha[i]){
            success = false;
            break;
        }
    }
    fclose(usrd->file_rcv.fp);
    usrd->file_rcv.fp = NULL;

    return success;
}

static bool handle_file_end_ack_recieved(struct user_data *usrd, uint8_t *data, size_t len){
    if(len < sizeof(uint32_t)){
        return false;
    }
    
    uint8_t *ptr = data;
    uint32_t block_cnt = ntohl(read_u32(&ptr));
    
    usrd->file_snd.active = false;
    tmr_cancel(&usrd->ft_tmr);

    struct ztime now;
    ztime_get(&now);
    uint64_t diff_ms = ztime_diff(&now , &usrd->file_snd.start_time);
    
    if(block_cnt == usrd->file_snd.block_cnt){
        if(diff_ms > 0){
        info("user_data: Send %u Bytes in %llu ms speeed = %llu kbps \n",
             usrd->file_snd.byte_cnt, diff_ms, (usrd->file_snd.byte_cnt*8)/diff_ms);
        }
        return true;
    } else {
        return false;
    }
}

static void resend_handler(void *arg)
{
    struct ecall *ecall = arg;
    
    if(!ecall || !ecall->dce || !ecall->usrd)
        return;
    
    struct user_data *usrd = ecall->usrd;
    
    if(usrd->channel_open && usrd->dce_ch && usrd->pending_msg_len > 0){
        int ret = dce_send(ecall->dce, usrd->dce_ch,
                           (void*)usrd->pending_msg_buf,
                           usrd->pending_msg_len);
        if(ret){
            info("user_data: failed to send blob \n");
            tmr_start(&usrd->tmr, 10, resend_handler, usrd);
        } else {
            usrd->pending_msg_len = 0;
            if(usrd->ready_h){
                usrd->ready_h(MAX_USER_DATA_SIZE, usrd->arg);
            }
        }
    }
}

static int ecall_user_data_send_internal(struct ecall *ecall, const void *data, size_t len, uint8_t type)
{
    if(!ecall || !ecall->dce || !ecall->usrd)
        return EINVAL;
    
    struct user_data *usrd = ecall->usrd;
    
    if(len > MAX_USER_DATA_SIZE){
        error("user_data: Maximum allowed data size is %d bytes \n", MAX_USER_DATA_SIZE);
        return -1;
    }
    
    if(usrd->channel_open && usrd->dce_ch){
        if(usrd->pending_msg_len > 0){
            error("user_data: cannot send user data. Other data is pending \n");
            return -1;
        } else {
            uint8_t *ptr = usrd->pending_msg_buf;
            *ptr++ = type;
            memcpy(ptr, data, len);
            usrd->pending_msg_len = len + 1;
            int ret = dce_send(ecall->dce, usrd->dce_ch,
                               (void*)&usrd->pending_msg_buf,
                               usrd->pending_msg_len);
            if(ret){
                info("user_data: failed to send blob \n");
                tmr_start(&usrd->tmr, 10, resend_handler, ecall);
            } else {
                usrd->pending_msg_len = 0;
                if(usrd->ready_h){
                    usrd->ready_h(MAX_USER_DATA_SIZE, usrd->arg);
                }
            }
        }
        return 0;
    } else {
        error("user_data: cannot send user data as channel is not open \n");
        return -1;
    }
}

static void send_end_recieved_ack(struct ecall *ecall)
{
    struct user_data *usrd = ecall->usrd;
    
    uint8_t buf[sizeof(uint32_t)];
    uint8_t *ptr = buf;
    
    write_u32(&ptr, htonl(usrd->file_rcv.block_cnt));
    
    ecall_user_data_send_internal(ecall, buf, sizeof(buf), USER_MESSAGE_FILE_END_ACK);
}

static void data_estab_handler(void *arg)
{
    struct ecall *ecall = arg;
    struct user_data *usrd = ecall->usrd;
    
    info("user_data(%p) datachan estab.\n", usrd);
}

static void data_channel_handler(int chid, uint8_t *data,
                                 size_t len, void *arg)
{
    struct ecall *ecall = arg;
    struct user_data *usrd = ecall->usrd;
    
    uint8_t type;
    
    uint8_t *ptr = data;
    read_u8(&ptr, 1, &type);
    len = len - 1;
    
    switch(type){
    
        case USER_MESSAGE_DATA:
        {
            if(usrd->rcv_h){
                usrd->rcv_h(ptr, len, usrd->arg);
            }
        }
        break;

        case USER_MESSAGE_FILE_START:
        {
            handle_file_start_recieved(usrd, ptr, len);
        }
        break;
            
        case USER_MESSAGE_FILE:
        {
            handle_file_chunk_recieved(usrd, ptr, len);
        }
        break;
            
        case USER_MESSAGE_FILE_END:
        {
            bool success = handle_file_end_recieved(usrd, ptr, len);
            if(success && usrd->f_rcv_h){
                char buf[strlen(usrd->file_rcv.path) + strlen(usrd->file_rcv.name)];
                sprintf(buf, "%s/%s", usrd->file_rcv.path, usrd->file_rcv.name);
                usrd->f_rcv_h(buf, usrd->arg);
                
                send_end_recieved_ack(ecall);
            }
        }
        break;
            
        case USER_MESSAGE_FILE_END_ACK:
        {
            bool success = handle_file_end_ack_recieved(usrd, ptr, len);
            if(usrd->f_snd_h){
                usrd->f_snd_h(usrd->file_snd.name, success, usrd->arg);
            }
        }
            
        default:
            
        break;
    }
}

static void data_channel_open_handler(int chid, const char *label,
                                      const char *protocol, void *arg)
{
    struct ecall *ecall = arg;
    struct user_data *usrd = ecall->usrd;
    
    info("user_data(%p): data channel opened with label %s \n", usrd, label);
    
    if (strcmp(label, "wire-user-data") == 0) {
        usrd->channel_open = true;
        if(usrd->ready_h){
            usrd->ready_h(MAX_USER_DATA_SIZE, usrd->arg);
        }
    }
    
}

static void data_channel_closed_handler(int chid, const char *label,
                                        const char *protocol, void *arg)
{
    struct ecall *ecall = arg;
    struct user_data *usrd = ecall->usrd;
    
    info("user_data(%p): data channel closed with label %s\n",
            usrd, label);
    
    if (strcmp(label, "wire-user-data") == 0) {
        usrd->channel_open = false;
    }
}

static void user_data_destructor(void *arg)
{
    struct user_data *usrd = arg;
    
    tmr_cancel(&usrd->tmr);
    tmr_cancel(&usrd->ft_tmr);
    
    if(usrd->file_snd.fp){
        fclose(usrd->file_snd.fp);
    }
    if(usrd->file_rcv.fp){
        fclose(usrd->file_rcv.fp); // Maybe delete it as it is not complete
    }
}

int ecall_add_user_data(struct ecall *ecall,
                         ecall_user_data_ready_h *ready_h,
                         ecall_user_data_rcv_h *rcv_h,
                         void *arg)
{
    if(!ecall)
        return EINVAL;
    
    if(ecall->usrd)
        return EINVAL;
   
    struct user_data *usrd = mem_zalloc(sizeof(*usrd), user_data_destructor);
    
    if (!usrd)
        return ENOMEM;
    
    usrd->ready_h = ready_h;
    usrd->rcv_h = rcv_h;
    usrd->arg = arg;
    
    ecall->usrd = usrd;
    
    return 0;
}

int ecall_add_user_data_channel(struct ecall *ecall, bool should_open)
{
    if (!ecall || !ecall->usrd || !ecall->dce)
        return EINVAL;
    
    int err = dce_channel_alloc(&ecall->usrd->dce_ch,
                                ecall->dce,
                                "wire-user-data",
                                "",
                                data_estab_handler,
                                data_channel_open_handler,
                                data_channel_closed_handler,
                                data_channel_handler,
                                ecall);
    
    if(err){
        error("user_data(%p) dce_channel_alloc failed err = %d \n", ecall->usrd, err);
        goto out;
    }
    
    if(should_open){
        err = dce_open_chan(ecall->dce, ecall->usrd->dce_ch);
        if (err) {
            warning("user_data: dce_open_chan failed (%m)\n", err);
        }
    }
    
out:
    return err;
}

int ecall_user_data_send(struct ecall *ecall, const void *data, size_t len)
{
    return ecall_user_data_send_internal(ecall, data, len, USER_MESSAGE_DATA);
}

static void handle_last_chunk(struct user_data *usrd)
{
    if(!usrd->file_snd.fp){
        return;
    }
    uint8_t *ptr = usrd->pending_msg_buf;
    *ptr++ = USER_MESSAGE_FILE_END;
    uint8_t *sha = ptr;
    ptr += SHA256_DIGEST_LENGTH;
    int cnt = fread( ptr, sizeof(uint8_t), usrd->ft_chunk_size, usrd->file_snd.fp);
    SHA256_Update(&usrd->file_snd.sha256_hash, ptr, cnt);
    SHA256_Final(sha, &usrd->file_snd.sha256_hash);
    ptr += cnt;
    usrd->pending_msg_len = (size_t)(ptr - usrd->pending_msg_buf);
    fclose(usrd->file_snd.fp);
    usrd->file_snd.fp = NULL;
    usrd->file_snd.byte_cnt += cnt;
}

static void handle_chunk(struct user_data *usrd)
{
    if(!usrd->file_snd.fp){
        return;
    }
    uint8_t *ptr = usrd->pending_msg_buf;
    *ptr++ = USER_MESSAGE_FILE;
    int cnt = fread( ptr, sizeof(uint8_t), usrd->ft_chunk_size, usrd->file_snd.fp);
    SHA256_Update(&usrd->file_snd.sha256_hash, ptr, cnt);
    ptr += cnt;
    usrd->pending_msg_len = (size_t)(ptr - usrd->pending_msg_buf);
    usrd->file_snd.byte_cnt += cnt;
}

static void wait_for_ft_ack_timeout(void *arg)
{
    struct ecall *ecall = arg;
    
    if(!ecall->usrd)
        return;
    
    struct user_data *usrd = ecall->usrd;
    
    warning("user_data: timeout waiting for ack \n");
    
    if(usrd->f_snd_h){
        usrd->f_snd_h(NULL, false, usrd->arg);
    }
    usrd->file_snd.active = false;
}

static void send_file_chunk_handler(void *arg)
{
    struct ecall *ecall = arg;
    
    if(!ecall || !ecall->dce || !ecall->usrd)
        return;
    
    struct user_data *usrd = ecall->usrd;
    
    if(usrd->channel_open && usrd->dce_ch && usrd->pending_msg_len > 0){
        int ret = dce_send(ecall->dce, usrd->dce_ch,
                           (void*)&usrd->pending_msg_buf,
                           usrd->pending_msg_len);
        if(ret){
            info("user_data: failed to send blob \n");
        } else {
            usrd->pending_msg_len = 0;
            
            if(usrd->file_snd.fp){
                usrd->file_snd.block_cnt++;
                if(usrd->file_snd.block_cnt == usrd->file_snd.nblocks){
                    handle_last_chunk(usrd);
                } else {
                    handle_chunk(usrd);
                }
            }
        }
    }
    if(usrd->pending_msg_len > 0 ){
        tmr_start(&usrd->ft_tmr, 10, send_file_chunk_handler, ecall);
    } else {
        /* Wait for the ack from the recieving side */
        tmr_start(&usrd->ft_tmr, 5000, wait_for_ft_ack_timeout, ecall);
    }
}

int ecall_user_data_send_file(struct ecall *ecall, const char *file, const char *name, int speed_kbps)
{
    if(!ecall || !ecall->dce || !ecall->usrd)
        return EINVAL;
    
    struct user_data *usrd = ecall->usrd;
    
    if(usrd->file_snd.active){
        error("user_data: file transfer in progress \n");
        return -1;
    }
    usrd->file_snd.active = true;
    
    if(!(usrd->channel_open && usrd->dce_ch && usrd->pending_msg_len <= 0)){
        error("user_data: cannot start file transfer \n");
        return -1;
    }
    
    if(strlen(name) > MAX_FILE_NAME_SIZE){
        error("user_data: filename too long \n");
        return -1;
    }
    
    if(speed_kbps < 1){
        // Default is 10 mbps
        usrd->ft_chunk_size = MAX_USER_DATA_SIZE;
    } else {
        usrd->ft_chunk_size = (speed_kbps * 10)/8;
    }
    if(usrd->ft_chunk_size > MAX_USER_DATA_SIZE){
        usrd->ft_chunk_size = MAX_USER_DATA_SIZE;
    }
    if(usrd->ft_chunk_size < 100){
        usrd->ft_chunk_size = 100; // 80 kbps is minimum
    }
    
    FILE *fp = fopen(file,"rb");
    if(!fp){
        error("user_data: cannot open %s \n", file);
        usrd->file_snd.active = false;
        return -1;
    }
    fseek(fp, 0L, SEEK_END);
    int sz = ftell(fp);
    fclose(fp);
    
    usrd->file_snd.nblocks = sz / usrd->ft_chunk_size;
    if(usrd->ft_chunk_size * (int)usrd->file_snd.nblocks < sz){
        usrd->file_snd.nblocks += 1;
    }
    
    usrd->file_snd.fp = fopen(file,"rb");
    
    uint8_t *ptr = usrd->pending_msg_buf;
    *ptr++ = USER_MESSAGE_FILE_START;
    write_u32(&ptr, htonl(usrd->file_snd.nblocks));
    memcpy(usrd->file_snd.name, name, strlen(name));
    memcpy(ptr, name, strlen(name));
    ptr += MAX_FILE_NAME_SIZE;
    int cnt = fread( ptr, sizeof(uint8_t), usrd->ft_chunk_size, usrd->file_snd.fp);
    SHA256_Init(&usrd->file_snd.sha256_hash);
    SHA256_Update(&usrd->file_snd.sha256_hash, ptr, cnt);
    ptr += cnt;
    usrd->pending_msg_len = (size_t)(ptr - usrd->pending_msg_buf);
    usrd->file_snd.block_cnt = 1;
    if(cnt < usrd->ft_chunk_size){
        fclose(usrd->file_snd.fp);
        usrd->file_snd.fp = NULL;
    }
    tmr_start(&usrd->ft_tmr, 1, send_file_chunk_handler, ecall);
    
    ztime_get(&usrd->file_snd.start_time);
    
    return 0;
}

int ecall_user_data_register_ft_handlers(struct ecall *ecall,
                    const char *rcv_path,
                    ecall_user_data_file_rcv_h *f_rcv_h,
                    ecall_user_data_file_snd_h *f_snd_h)
{
    if(!ecall || !ecall->usrd)
        return EINVAL;
    
    struct user_data *usrd = ecall->usrd;
    
    usrd->f_rcv_h = f_rcv_h;
    usrd->f_snd_h = f_snd_h;
    
    if(strlen(rcv_path) > sizeof(usrd->file_rcv.path)){
        error("user_data: path too long \n");
        return -1;
    }
    memcpy(usrd->file_rcv.path, rcv_path, strlen(rcv_path));
    
    return 0;
}




