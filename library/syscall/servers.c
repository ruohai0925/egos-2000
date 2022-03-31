/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang
 * Description: user friendly interfaces of kernel processes
 */

#include "egos.h"
#include "syscall.h"
#include "servers.h"
#include <string.h>

static int sender;
static char buf[SYSCALL_MSG_LEN];

int dir_lookup(int dir_ino, char* name) {
    struct dir_request req;
    req.type = DIR_LOOKUP;
    req.ino = dir_ino;
    strcpy(req.name, name);
    sys_send(GPID_DIR, (void*)&req, sizeof(req));

    sys_recv(&sender, buf, SYSCALL_MSG_LEN);
    if (sender != GPID_DIR) FATAL("dir_lookup: an error occurred");
    struct dir_reply *reply = (void*)buf;

    return reply->status == DIR_OK? reply->ino : -1;
}

int file_read(int file_ino, int offset, char* block) {
    struct file_request req;
    req.type = FILE_READ;
    req.ino = file_ino;
    req.offset = offset;
    sys_send(GPID_FILE, (void*)&req, sizeof(req));

    sys_recv(&sender, buf, SYSCALL_MSG_LEN);
    if (sender != GPID_FILE) FATAL("file_read: an error occurred");
    struct file_reply *reply = (void*)buf;
    memcpy(block, reply->block.bytes, BLOCK_SIZE);

    return reply->status == FILE_OK? 0 : -1;
}
