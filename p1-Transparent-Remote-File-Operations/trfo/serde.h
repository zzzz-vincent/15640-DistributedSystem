/**
 * @file serde.h
 * @brief library for serializing and deserializing data structures.
 * This library provides serialize and deserialize methods for rpc_frame,
 * rpc_response, and explicit RPC calls.
 * It also provide memory write and read methods.
 *
 * @author Zishen Wen <zishenw@andrew.cmu.edu>
 */
#ifndef __SERDE_H__
#define __SERDE_H__

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../include/dirtree.h"

#define OP_OPEN    0x01
#define OP_WRITE   0x02
#define OP_CLOSE   0x03
#define OP_READ    0x04
#define OP_LSEEK   0x05
#define OP_STAT    0x06
#define OP_UNLINK  0x07
#define OP_GETDIR  0x08
#define OP_GETTRR  0x09

typedef struct rpc_frame {
    u_int32_t opcode;
    u_int32_t payload_size;
    char *payload;
} rpc_frame;

typedef struct rpc_resp {
    int err_no;
    u_int32_t size;
    char *data;
} rpc_resp;

// mem operator, return next offset after write/read
size_t mem_write_int32(char *data, size_t off, u_int32_t val);
size_t mem_write_int16(char *data, size_t off, u_int16_t val);
size_t mem_write_data(char *data, size_t off, const void *val, size_t size);

size_t mem_read_int32(const char *data, size_t off, u_int32_t *out);
size_t mem_read_int16(const char *data, size_t off, u_int16_t *out);
size_t mem_read_data(const char *data, size_t off, void *out, size_t size);

size_t mem_write_tree(const struct dirtreenode* tree, char *buf, size_t off);
size_t mem_read_tree(struct dirtreenode* tree, const char *buf, size_t off);

// rpc frame
bool read_frame(const char *in, struct rpc_frame* frame);
size_t marshal_frame(char *out, const struct rpc_frame *frame);

// rpc resp
void read_resp(const char *in, struct rpc_resp* resp);
size_t marshal_resp(char *out, const struct rpc_resp *resp);

// rpc operator
// int open(const char *pathname, int flags, ...)
size_t call_open_marshal(char *out, const char *pathname, u_int32_t flags, u_int16_t m);
bool call_open_unmarshal(const char* in, char *pathname, u_int32_t *flags, u_int16_t* m);

// int close(int fd)
size_t call_close_marshal(char *out, int fd);
bool call_close_unmarshal(const char *in, int *fd);

// ssize_t write(int fd, const void *buf, size_t count)
size_t call_write_marshal(char *out, int fd, const void *buf, size_t count);
char *call_write_unmarshal(const char *in, int *fd, size_t *count);

// ssize_t read(int fd, void *buf, size_t count)
size_t call_read_marshal(char *out, int fd, size_t count);
bool call_read_unmarshal(const char *in, int *fd, size_t *count);

// off_t lseek(int fd, off_t offset, int whence)
size_t call_lseek_marshal(char *out, int fd, off_t offset, int whence);
bool call_lseek_unmarshal(const char *in, int *fd, off_t *offset, int *whence);

//int __xstat(int ver, const char *path, struct stat *stat_buf)
size_t call_stat_marshal(char *out, int ver, const char *path);
bool call_stat_unmarshal(const char *in, int *var, char *path);

//int unlink(const char *pathname)
size_t call_unlink_marshal(char *out, const char *pathname);
bool call_unlink_unmarshal(const char *in, char *pathname);

// ssize_t getdirentries(int fd, char *buf, size_t nbytes, off_t *basep)
size_t call_getdirentries_marshal(char *out, int fd, size_t nbytes, off_t basep);
bool call_getdirentries_unmarshal(const char *in, int *fd, size_t *nbytes, off_t *basep);

// struct dirtreenode* getdirtree(const char *path)
size_t call_dirtreenode_marshal(char *out, const char *path);
bool call_dirtreenode_unmarshal(char *in, char *path);

#endif