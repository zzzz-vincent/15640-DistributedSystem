/**
 * @file serde.c
 * @brief library for serializing and deserializing data structures.
 * This library provides serialize and deserialize methods for rpc_frame,
 * rpc_response, and explicit RPC calls.
 * It also provide memory write and read methods.
 *
 * @author Zishen Wen <zishenw@andrew.cmu.edu>
 */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "serde.h"

/**
 * mem operator, return next offset after write/read
**/

size_t mem_write_int32(char *data, size_t off, u_int32_t val) {
    return mem_write_data(data, off, &val, sizeof(u_int32_t));
}

size_t mem_read_int32(const char *data, size_t off, u_int32_t *out) {
    return mem_read_data(data, off, out, sizeof(u_int32_t));
}

size_t mem_write_int16(char *data, size_t off, u_int16_t val) {
    return mem_write_data(data, off, &val, sizeof(u_int16_t));
}

size_t mem_read_int16(const char *data, size_t off, u_int16_t *out) {
    return mem_read_data(data, off, out, sizeof(u_int16_t));
}

size_t mem_write_data(char *data, size_t off, const void *val, size_t size) {
    memcpy(data + off, val, size);
    return off + size;
}

size_t mem_read_data(const char *data, size_t off, void *out, size_t size) {
    memcpy(out, data + off, size);
    return off + size;
}

size_t mem_write_tree(const struct dirtreenode* tree, char *buf, size_t off) {
    size_t name_len = strlen(tree->name) + 1;
    int num_subdirs = tree->num_subdirs;
    off = mem_write_int32(buf, off, num_subdirs);
    off = mem_write_data(buf, off, &name_len, sizeof(size_t));
    off = mem_write_data(buf, off, tree->name, name_len);
    size_t i;
    for (i = 0; i < num_subdirs; i ++) {
        off = mem_write_tree(tree->subdirs[i], buf, off);
    }
    return off;
}

size_t mem_read_tree(struct dirtreenode* tree, const char *buf, size_t off) {
    size_t name_len = 0;
    int num_subdirs = 0;
    off = mem_read_int32(buf, off, (u_int32_t *) &num_subdirs);
    off = mem_read_data(buf, off, &name_len, sizeof(size_t));
    tree->name = malloc(name_len);
    off = mem_read_data(buf, off, tree->name, name_len);
    tree->num_subdirs = num_subdirs;
    tree->subdirs = malloc(sizeof(struct dirtreenode *) * num_subdirs);
    size_t i;
    for (i = 0; i < num_subdirs; i++) {
        tree->subdirs[i] = malloc(sizeof(struct dirtreenode));
        off = mem_read_tree(tree->subdirs[i], buf, off);
    }
    return off;
}

/**
 * frame
**/

bool read_frame(const char *in, struct rpc_frame *frame) {
    size_t off = 0;
    off = mem_read_int32(in, off, &frame->opcode);
    off = mem_read_int32(in, off, &frame->payload_size);
    if (frame->payload_size <= 0) {
        return false;
    }
    frame->payload = malloc(frame->payload_size);
    mem_read_data(in, off, frame->payload, frame->payload_size);
    return true;
}

size_t marshal_frame(char *out, const struct rpc_frame *frame) {
    size_t off = 0;
    off = mem_write_int32(out, off, frame->opcode);
    off = mem_write_int32(out, off, frame->payload_size);
    off = mem_write_data(out, off, frame->payload, frame->payload_size);
    return off;
}

/**
 * resp
**/

void read_resp(const char *in, struct rpc_resp* resp) {
    size_t off = 0;
    off = mem_read_data(in, off, &resp->err_no, sizeof(int));
    off = mem_read_int32(in, off, &resp->size);
    resp->data = malloc(resp->size);
    mem_read_data(in, off, resp->data, resp->size);
}

size_t marshal_resp(char *out, const struct rpc_resp *resp) {
    size_t off = 0;
    off = mem_write_data(out, off, &resp->err_no, sizeof(int));
    off = mem_write_int32(out, off, resp->size);
    off = mem_write_data(out, off, resp->data, resp->size);
    return off;
}

/*
 * operator
 */

size_t call_open_marshal(char *out, const char *pathname, u_int32_t flags, u_int16_t mode) {
    size_t path_len = strlen(pathname) + 1;
    size_t off = 0;
    off = mem_write_int32(out, off, flags);
    off = mem_write_int16(out, off, mode);
    off = mem_write_int32(out, off, path_len);
    off = mem_write_data(out, off, pathname, path_len);
    return off;
}

bool call_open_unmarshal(const char *in, char *pathname, u_int32_t *flags, u_int16_t *mode) {
    size_t off = 0;
    u_int32_t path_len = 0;
    off = mem_read_int32(in, off, flags);
    off = mem_read_int16(in, off, mode);
    off = mem_read_int32(in, off, &path_len);
    mem_read_data(in, off, pathname, path_len);
    return true;
}

size_t call_close_marshal(char *out, int fd) {
    size_t off = 0;
    off = mem_write_int32(out, off, fd);
    return off;
}

bool call_close_unmarshal(const char *in, int *fd) {
    mem_read_int32(in, 0, (u_int32_t *) fd);
    return true;
}

size_t call_write_marshal(char *out, int fd, const void *buf, size_t count) {
    size_t off = 0;
    off = mem_write_int32(out, off, fd);
    off = mem_write_data(out, off, &count, sizeof(size_t));
    off = mem_write_data(out, off, buf, count);
    return off;
}

char* call_write_unmarshal(const char *in, int *fd, size_t *count) {
    size_t off = 0;
    off = mem_read_int32(in, off, (u_int32_t *) fd);
    off = mem_read_data(in, off, count, sizeof(size_t));
    char* buf = malloc(*count);
    mem_read_data(in, off, buf, *count);
    return buf;
}

size_t call_read_marshal(char *out, int fd, size_t count) {
    size_t off = 0;
    off = mem_write_int32(out, off, fd);
    off = mem_write_data(out, off, &count, sizeof(size_t));
    return off;
}

bool call_read_unmarshal(const char *in, int *fd, size_t *count) {
    size_t off = 0;
    off = mem_read_int32(in, off, (u_int32_t *) fd);
    mem_read_data(in, off, count, sizeof(size_t));
    return true;
}

// lseek(int fd, off_t offset, int whence)
size_t call_lseek_marshal(char *out, int fd, off_t offset, int whence) {
    size_t off = 0;
    off = mem_write_int32(out, off, fd);
    off = mem_write_data(out, off, &offset,sizeof(off_t));
    off = mem_write_data(out, off, &whence, sizeof(int));
    return off;
}

bool call_lseek_unmarshal(const char *in, int *fd, off_t *offset, int *whence) {
    size_t off = 0;
    off = mem_read_int32(in, off, (u_int32_t *) fd);
    off = mem_read_data(in, off, offset, sizeof(off_t));
    mem_read_data(in, off, whence, sizeof(int));
    return true;
}

size_t call_stat_marshal(char *out, int ver, const char *path) {
    size_t off = 0;
    size_t path_len = strlen(path) + 1;
    off = mem_write_int32(out, off, ver);
    off = mem_write_data(out, off, &path_len, sizeof(size_t));
    off = mem_write_data(out, off, path, path_len);
    return off;
}
bool call_stat_unmarshal(const char *in, int *var, char *path) {
    size_t off = 0;
    size_t path_len = 0;
    off = mem_read_int32(in, off, (u_int32_t *) var);
    off = mem_read_data(in, off, &path_len, sizeof(size_t));
    mem_read_data(in, off, path, path_len);
    return true;
}

size_t call_unlink_marshal(char *out, const char *pathname) {
    size_t path_len = strlen(pathname) + 1;
    size_t off = 0;
    off = mem_write_int32(out, off, path_len);
    off = mem_write_data(out, off, pathname, path_len);
    return off;
}

bool call_unlink_unmarshal(const char *in, char *pathname) {
    size_t off = 0;
    u_int32_t path_len = 0;
    off = mem_read_int32(in, off, &path_len);
    mem_read_data(in, off, pathname, path_len);
    return true;
}

size_t call_getdirentries_marshal(char *out, int fd, size_t nbytes, off_t basep) {
    size_t off = 0;
    off = mem_write_int32(out, off, fd);
    off = mem_write_data(out, off, &basep, sizeof(off_t));
    off = mem_write_data(out, off, &nbytes, sizeof(size_t));
    return off;
}

bool call_getdirentries_unmarshal(const char *in, int *fd, size_t *nbytes, off_t *basep) {
    size_t off = 0;
    off = mem_read_int32(in, off, (u_int32_t *) fd);
    off = mem_read_data(in, off, basep, sizeof(off_t));
    mem_read_data(in, off, nbytes, sizeof(size_t));
    return true;
}

size_t call_dirtreenode_marshal(char *out, const char *path) {
    size_t off = 0;
    u_int32_t path_len = strlen(path) + 1;
    off = mem_write_int32(out, off, path_len);
    off = mem_write_data(out, off, path, path_len);
    return off;
}
bool call_dirtreenode_unmarshal(char *in, char *path) {
    size_t off = 0;
    u_int32_t path_len = 0;
    off = mem_read_int32(in, off, &path_len);
    mem_read_data(in, off, path, path_len);
    return true;
}
