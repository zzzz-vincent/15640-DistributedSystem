/**
 * @file mylib.c
 * @brief A RPC client interposition library.
 * This library transpose local file operation to remote procedure call on serverside.
 *
 * it support file operations including open, close, write, read, close, lseek, stat,
 * unlink, getdirentries, and dirtreenode. Call related variables is marshaled with
 * call specific order and send as rpc_frame with opcode and size information. It
 * receive rpc_resp from the server and unmarshal to get return value.
 *
 * Should any error happens on client side, it will be checked locally. For error on
 * the server size, client will receive errno from the server and set it to local.
 *
 * @author Zishen Wen <zishenw@andrew.cmu.edu>
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <err.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

#include "serde.h"

#define MAXMSGLEN 4096
#define BUFFERLEN 4096

int (*orig_close)(int fd);
ssize_t (*orig_write)(int fd, const void *buf, size_t count);
ssize_t (*orig_read)(int fd, void *buf, size_t count);

rpc_resp* send_request(const char *msg, size_t msg_sz);
void send_all(int sockfd, const void *data, size_t size);
int init_client();

int _sockfd;
int min_fd;
int opened_fd;

/**
 * @brief RPC call for remote read.
 *
 * read() attempts to read up to count bytes from
 * file descriptor fd into the buffer starting at buf.
 *
 * @param pathname pathname of the file
 * @param flags flags for read
 * @return On success, the number of bytes read is returned.
 * On error, -1 is returned, and errno is set to indicate the error.
 */
int open(const char *pathname, int flags, ...) {
    fprintf(stderr, "\nlib: open system call\n");
	mode_t m=0;
	if (flags & O_CREAT) {
		va_list a;
		va_start(a, flags);
		m = va_arg(a, mode_t);
		va_end(a);
	}

    struct rpc_frame* frame = malloc(sizeof(rpc_resp));
    frame->opcode = OP_OPEN;

    // build op message
    frame->payload = malloc(BUFFERLEN);
    frame->payload_size = call_open_marshal(frame->payload, pathname, flags, m);

    // build rpc frame
    char *buf = malloc(BUFFERLEN);
    size_t frame_size = marshal_frame(buf, frame);

    // send rpc frame
    fprintf(stderr, "lib: open system call - sending request size %zu\n", frame_size);
    rpc_resp * resp = send_request(buf, frame_size);

    // handle response
    int fd;
    int new_err = resp->err_no;
    mem_read_data(resp->data, 0, &fd, sizeof(int));

    // free resources
    free(resp->data);
    free(resp);
    free(buf);
    free(frame->payload);
    free(frame);

    fprintf(stderr, "lib: open system call - got fd from server %d\n", fd);
    if (fd >= 0) {
        min_fd = fd < min_fd? fd: min_fd;
        ++opened_fd;
        fprintf(stderr, "lib: open system call - min_fd [%d] opened_fd [%d]\n", min_fd, opened_fd);
    } else {
        fprintf(stderr, "lib: open system call - error: %s\n", strerror(new_err));
        errno = new_err;
    }
    return fd;
}

/**
 * @brief RPC call for remote close.
 *
 * close() closes a file descriptor, so that it no longer refers to
 * any file and may be reused.
 *
 * @param fd file descriptor
 * @return close() returns zero on success.  On error,
 * -1 is returned, and errno is set to indicate the error.
 */
int close(int fd) {
    fprintf(stderr, "\nlib: close system call - (%d)\n", fd);

    if (fd < min_fd) {
        fprintf(stderr, "lib: close system call - using local close.\n");
        return orig_close(fd);
    }

    struct rpc_frame* frame = malloc(sizeof(rpc_resp));
    frame->opcode = OP_CLOSE;

    // build op message
    frame->payload = malloc(BUFFERLEN);
    frame->payload_size = call_close_marshal(frame->payload, fd);

    // build rpc frame
    char *buf = malloc(BUFFERLEN);
    size_t frame_size = marshal_frame(buf, frame);

    // send rpc frame
    fprintf(stderr, "lib: close system call - sending request size %zu\n", frame_size);
    rpc_resp * resp = send_request(buf, frame_size);

    // handle response
    int r;
    int new_err = resp->err_no;
    mem_read_data(resp->data, 0, &r, sizeof(int));

    // free resources
    free(resp->data);
    free(resp);
    free(buf);
    free(frame->payload);
    free(frame);

    if (r == 0) {
        --opened_fd;
        if (opened_fd == 0) {
            fprintf(stderr, "lib: close system call - closing socket\n");
            // close socket here? when close is succeeded and all fd closed
            orig_close(_sockfd);
            _sockfd = -1;
        }
    }
    fprintf(stderr, "lib: close system call - finish return %d\n", r);
    if (r < 0) {
        fprintf(stderr, "error in close %s\n", strerror(new_err));
        errno = new_err;
    }
    return r;
}

/**
 * @brief RPC call for remote read.
 *
 * read() attempts to read up to count bytes from file descriptor fd
 * into the buffer starting at buf.
 *
 * @param fd file descriptor
 * @param buf buf to store read data
 * @param count count for bytes of read
 * @return On success, the number of bytes read is returned
 * (zero indicates end of file), and the file position is advanced
 * by this number. On error, -1 is returned, and errno is set to
 * indicate the error.
 */
ssize_t read(int fd, void *buf, size_t count) {
    fprintf(stderr, "\nlib: read system call - (%d) (%zu)\n", fd, count);
    if (fd < min_fd) {
        fprintf(stderr, "lib: read system call - local read\n");
        return orig_read(fd, buf, count);
    }
    struct rpc_frame* frame = malloc(sizeof(rpc_resp));
    frame->opcode = OP_READ;

    // build op message
    frame->payload = malloc(BUFFERLEN);
    frame->payload_size = call_read_marshal(frame->payload, fd, count);

    // build rpc frame
    char *rpc_buf = malloc(BUFFERLEN);
    size_t frame_size = marshal_frame(rpc_buf, frame);

    // send rpc frame
    fprintf(stderr, "lib: read system call - sending request size %zu\n", frame_size);
    rpc_resp * resp = send_request(rpc_buf, frame_size);

    // handle response
    ssize_t r;
    int new_err = resp->err_no;
    size_t off = 0;
    off = mem_read_data(resp->data, off, &r, sizeof(ssize_t));
    if (r > 0) {
        mem_read_data(resp->data, off, buf, r);
    }

    // free resources
    free(resp->data);
    free(resp);
    free(rpc_buf);
    free(frame->payload);
    free(frame);

    fprintf(stderr, "read call finish: return %zd\n", r);
    if (r < 0) {
        fprintf(stderr, "error in read %s\n", strerror(new_err));
        errno = new_err;
    }
    return r;
}

/**
 * @brief RPC call for remote write.
 *
 * write() writes up to count bytes from the buffer starting at buf
 * to the file referred to by the file descriptor fd.
 *
 * @param fd file descriptor
 * @param count count for bytes to write
 * @return On success, the number of bytes written is returned.
 * On error, -1 is returned, and errno is set to indicate the error.
 */
ssize_t write(int fd, const void *buf, size_t count) {
    fprintf(stderr, "\nlib: write system call - (%d) (%zu)\n", fd, count);
    if (fd < min_fd) {
        fprintf(stderr, "lib: write system call - local write\n");
        return orig_write(fd, buf, count);
    }
    struct rpc_frame* frame = malloc(sizeof(rpc_resp));
    frame->opcode = OP_WRITE;

    // build op message
    frame->payload = malloc(count + sizeof(int) + sizeof(size_t));
    frame->payload_size = call_write_marshal(frame->payload, fd, buf, count);

    fprintf(stderr, "lib: write system call - frame payload size: %d\n", frame->payload_size);

    // build rpc frame
    char *rpc_buf = malloc(count + sizeof(rpc_frame));
    size_t frame_size = marshal_frame(rpc_buf, frame);

    // send rpc frame
    fprintf(stderr, "lib: write system call - sending request size %zu\n", frame_size);
    rpc_resp * resp = send_request(rpc_buf, frame_size);

    // handle response
    ssize_t r;
    int new_err = resp->err_no;
    mem_read_data(resp->data, 0, &r, sizeof(ssize_t));

    // free resources
    free(resp->data);
    free(resp);
    free(rpc_buf);
    free(frame->payload);
    free(frame);

    fprintf(stderr, "write call finish: return %zd\n", r);
    if (r < 0) {
        fprintf(stderr, "error in write: %s\n", strerror(new_err));
        errno = new_err;
    }
    return r;
}

/**
 * @brief RPC call for remote lseek.
 *
 * lseek() repositions the file offset of the open file description
 * associated with the file descriptor fd to the argument offset
 * according to the directive whence.
 *
 * @param fd file descriptor
 * @param offset offset bytes
 * @param whence condition of offset
 * @return Upon successful completion, lseek() returns the resulting
 * offset location as measured in bytes from the beginning of the file.
 * On error, the value (off_t) -1 is returned and errno is set to indicate
 * the error.
 */
off_t lseek(int fd, off_t offset, int whence) {
    fprintf(stderr, "\nlib: lseek system call - (%d) (-) (%d)\n", fd, whence);
    if (opened_fd == 0) {
        errno = EBADF;
        return -1;
    }
    struct rpc_frame* frame = malloc(sizeof(rpc_resp));
    frame->opcode = OP_LSEEK;

    // build op message
    frame->payload = malloc(BUFFERLEN);
    frame->payload_size = call_lseek_marshal(frame->payload, fd, offset, whence);

    // build rpc frame
    char *rpc_buf = malloc(BUFFERLEN);
    size_t frame_size = marshal_frame(rpc_buf, frame);

    // send rpc frame
    fprintf(stderr, "lib: lseek system call - sending request size %zu\n", frame_size);
    rpc_resp * resp = send_request(rpc_buf, frame_size);

    // handle response
    off_t r;
    int new_err = resp->err_no;
    mem_read_data(resp->data, 0, &r, sizeof(off_t));

    // free resources
    free(resp->data);
    free(resp);
    free(rpc_buf);
    free(frame->payload);
    free(frame);

    fprintf(stderr, "lseek call finish: return %ld\n", r);
    if (r < 0) {
        fprintf(stderr, "error in lseek %s\n", strerror(new_err));
        errno = new_err;
    }
    return r;
}

/**
 * @brief RPC call for remote state.
 *
 * These functions return information about a file, in the buffer
 * pointed to by statbuf.
 *
 * @param ver version
 * @param path file path
 * @param stat_buf buf to store data
 * @return On success, zero is returned.  On error, -1 is returned,
 * and errno is set to indicate the error.
 */
int __xstat(int ver, const char *path, struct stat *stat_buf) {
    fprintf(stderr, "\nlib: __xstat system call - (%d) (%s)\n", ver, path);
    struct rpc_frame* frame = malloc(sizeof(rpc_resp));
    frame->opcode = OP_STAT;

    // build op message
    frame->payload = malloc(BUFFERLEN);
    frame->payload_size = call_stat_marshal(frame->payload, ver, path);

    // build rpc frame
    char *rpc_buf = malloc(BUFFERLEN);
    size_t frame_size = marshal_frame(rpc_buf, frame);

    // send rpc frame
    fprintf(stderr, "lib: __xstat system call - sending request size %zu\n", frame_size);
    rpc_resp * resp = send_request(rpc_buf, frame_size);

    // handle response
    int r;
    int new_err = resp->err_no;
    size_t off = mem_read_int32(resp->data, 0, (u_int32_t *) &r);
    if (r >= 0) {
        mem_read_data(resp->data, off, stat_buf, sizeof(struct stat));
    }
    // free resources
    free(resp->data);
    free(resp);
    free(rpc_buf);
    free(frame->payload);
    free(frame);

    fprintf(stderr, "__xstat call finish: return %d\n", r);
    if (r < 0) {
        fprintf(stderr, "error in __xstat %s\n", strerror(new_err));
        errno = new_err;
    }
    return r;
}

/**
 * @brief RPC call for remote unlink.
 *
 * unlink() deletes a name from the filesystem.  If that name was the
 * last link to a file and no processes have the file open, the file
 * is deleted and the space it was using is made available for reuse.
 *
 * @param path file path
 * @return On success, zero is returned.  On error, -1 is returned,
 * and errno is set to indicate the error.
 */
int unlink(const char *pathname){
    fprintf(stderr, "\nmylib: unlink called for path %s \n", pathname);
    struct rpc_frame* frame = malloc(sizeof(rpc_resp));
    frame->opcode = OP_UNLINK;

    // build op message
    frame->payload = malloc(BUFFERLEN);
    frame->payload_size = call_unlink_marshal(frame->payload, pathname);

    // build rpc frame
    char *rpc_buf = malloc(BUFFERLEN);
    size_t frame_size = marshal_frame(rpc_buf, frame);

    // send rpc frame
    fprintf(stderr, "lib: unlink system call - sending request size %zu\n", frame_size);
    rpc_resp * resp = send_request(rpc_buf, frame_size);

    // handle response
    int r;
    int new_err = resp->err_no;
    mem_read_int32(resp->data, 0, (u_int32_t *) &r);

    free(resp->data);
    free(resp);
    free(rpc_buf);
    free(frame->payload);
    free(frame);

    fprintf(stderr, "unlink call finish: return %d\n", r);
    if (r < 0) {
        fprintf(stderr, "error in unlink %s\n", strerror(new_err));
        errno = new_err;
    }

    return r;
}

/**
 * @brief RPC call for remote getdirentries.
 *
 * Read directory entries from the directory specified by fd into buf.
 * At most nbytes are read. Reading starts at offset *basep, and *basep
 * is updated with the new position after reading.
 *
 * @param fd file descripter
 * @param buf data buf
 * @param nbytes read bytes
 * @param basep offset
 * @return getdirentries() returns the number of bytes read or zero when
 * at the end of the directory. If an error occurs, -1 is returned, and
 * errno is set to indicate the error.
 */
ssize_t getdirentries(int fd, char *buf, size_t nbytes, off_t *basep) {
    fprintf(stderr, "\nmylib: getdirentries called for path %d \n", fd);
    if (opened_fd == 0) {
        errno = EBADF;
        return -1;
    }
    struct rpc_frame* frame = malloc(sizeof(rpc_resp));
    frame->opcode = OP_GETDIR;

    // build op message
    frame->payload = malloc(BUFFERLEN);
    frame->payload_size = call_getdirentries_marshal(frame->payload, fd, nbytes, *basep);

    // build rpc frame
    char *rpc_buf = malloc(BUFFERLEN);
    size_t frame_size = marshal_frame(rpc_buf, frame);

    // send rpc frame
    fprintf(stderr, "lib: getdirentries system call - sending request size %zu\n", frame_size);
    rpc_resp * resp = send_request(rpc_buf, frame_size);

    // handle response
    ssize_t r;
    int new_err = resp->err_no;
    size_t off = mem_read_data(resp->data, 0, &r, sizeof(ssize_t));
    off = mem_read_data(resp->data, off, basep, sizeof(off_t));

    if (r > 0) {
        mem_read_data(resp->data, off, buf, r);
    }

    free(resp->data);
    free(resp);
    free(rpc_buf);
    free(frame->payload);
    free(frame);

    fprintf(stderr, "getdirentries call finish: return %zd\n", r);
    if (r < 0) {
        fprintf(stderr, "error in getdirentries %s\n", strerror(new_err));
        errno = new_err;
    }

    return r;
}

/**
 * @brief RPC call for remote getdirtree.
 *
 * Recusively descend through directory hierarchy starting at directory
 * indicated by path string. Allocates and constructs directory tree data
 * structure representing all of the directories in the hierarchy.
 *
 * @param path file path
 * @return pointer to root node of directory tree structure or NULL if
 * there was en error (will set errno in this case)
 */
struct dirtreenode* getdirtree(const char *path) {
    fprintf(stderr, "\nmylib: getdirtree called for path %s \n", path);
    struct rpc_frame* frame = malloc(sizeof(rpc_resp));
    frame->opcode = OP_GETTRR;

    // build op message
    frame->payload = malloc(BUFFERLEN);
    frame->payload_size = call_dirtreenode_marshal(frame->payload, path);

    // build rpc frame
    char *rpc_buf = malloc(BUFFERLEN);
    size_t frame_size = marshal_frame(rpc_buf, frame);

    // send rpc frame
    fprintf(stderr, "lib: getdirtree system call - sending request size %zu\n", frame_size);
    rpc_resp * resp = send_request(rpc_buf, frame_size);

    // handle response
    struct dirtreenode* tree = malloc(sizeof(struct dirtreenode));
    int new_err = resp->err_no;
    u_int32_t r = resp->size;
    if (r > 0) {
        mem_read_tree(tree, resp->data, 0);
    }

    free(resp->data);
    free(resp);
    free(rpc_buf);
    free(frame->payload);
    free(frame);

    fprintf(stderr, "getdirtree call finished: \n");

    if (r == 0) {
        fprintf(stderr, "error in getdirtree %s\n", strerror(new_err));
        errno = new_err;
    }

    return tree;
}

/**
 * @brief RPC call for remote freedirtree (executed locally).
 *
 * Recursively frees the memory used to hold the directory tree structures,
 * including the name strings, pointer arrays, and dirtreenode structures.
 *
 * @param dt dirtreenode struct
 */
void freedirtree(struct dirtreenode* dt) {
    size_t i;
    for (i = 0; i < dt->num_subdirs; i++) {
        freedirtree(dt->subdirs[i]);
    }
    free(dt->name);
    free(dt->subdirs);
    free(dt);
}

/**
 * @brief init client for teach rpc call.
 * @return A -1 is returned if an error occurs, otherwise the return value
 * is a descriptor referencing the socket.
 */
int init_client() {
    char *serverip;
    char *serverport;
    unsigned short port;
    int sockfd, rv;
    struct sockaddr_in srv;

    // Get environment variable indicating the ip address of the server
    serverip = getenv("server15440");
    if (serverip) fprintf(stderr, "Got environment variable server15440: %s\n", serverip);
    else {
        fprintf(stderr, "Environment variable server15440 not found.  Using 127.0.0.1\n");
        serverip = "127.0.0.1";
    }

    // Get environment variable indicating the port of the server
    serverport = getenv("serverport15440");
    if (serverport) fprintf(stderr, "Got environment variable serverport15440: %s\n", serverport);
    else {
        fprintf(stderr, "Environment variable serverport15440 not found.  Using 15440\n");
        serverport = "15440";
    }
    port = (unsigned short)atoi(serverport);

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
    if (sockfd<0) err(1, 0);			// in case of error

    // setup address structure to point to server
    memset(&srv, 0, sizeof(srv));			// clear it first
    srv.sin_family = AF_INET;			// IP family
    srv.sin_addr.s_addr = inet_addr(serverip);	// IP address of server
    srv.sin_port = htons(port);			// server port

    // actually connect to the server
    rv = connect(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
    if (rv<0) err(1,0);

    return sockfd;
}

/**
 * @brief get socket id from init_client().
 * @return A -1 is returned if an error occurs, otherwise the return value
 * is a descriptor referencing the socket.
 */
int get_socket_fd() {
    // reconnect if needed
    if (_sockfd < 0) {
        fprintf(stderr, ">> connect: init client<<\n");
        _sockfd = init_client();
    }
    return _sockfd;
}

/**
 * @brief send all data to server
 *
 * @param sockfd socket fd
 * @param data data to be sent
 * @param size size of data
 */
void send_all(int sockfd, const void *data, size_t size) {
    int frame_size = (int)size;
    char buf[MAXMSGLEN];
    ssize_t rv;
    size_t pending = size;
    fprintf(stderr, "client send_all data [%zu]\n", size);

    // size of package first
    size_t off = mem_write_data(buf, 0, &frame_size, sizeof(int));
    while(pending > 0) {
        // actual data size to be sent in this round
        size_t len = pending > MAXMSGLEN - off? MAXMSGLEN - off: pending;
        // fill in buffer
        off = mem_write_data(buf, off, data + (size - pending), len);
        // send, may end up in multiple sends
        size_t sent = 0;
        while((rv = send(sockfd, buf + sent, off - sent, 0)) > 0) {
            sent += rv;
            if (sent >= off) {
                break;
            }
        }
        if (rv < 0) err(1, 0);
        // update pending bytes and off
        pending -= len;
        off = 0;
    }
    fprintf(stderr, "client send_all finished\n");
}

/**
 * @brief send request to server.
 * @return A -1 is returned if an error occurs, otherwise the return value
 * is a descriptor referencing the socket.
 */
rpc_resp* send_request(const char *msg, size_t msg_sz) {
    int sockfd = get_socket_fd();
    if (sockfd<0) err(1,0);
    ssize_t rv;
    char buf[MAXMSGLEN];

    // send to server
    send_all(sockfd, msg, msg_sz);

    // receive response
    rv=recv(sockfd, buf, MAXMSGLEN, 0);
    fprintf(stderr, "client starts receiving response\n");

    int frame_size = 0;
    ssize_t received = rv;

    // receive bytes of size sizeof(int) to indicate size of frame
    while(received < sizeof(int)) {
        rv = recv(sockfd, buf + received, MAXMSGLEN - received, 0);
        if (rv < 0) err(1,0);
        received += rv;
    }

    memcpy(&frame_size, buf, sizeof(int));

    if (frame_size <= 0) {
        fprintf(stderr, "client error - invalid frame size? [%d]\n", frame_size);
        err(1,0);
    }
    fprintf(stderr, "client - frame size [%d], received size [%zd]\n", frame_size, received);

    // receive the rest of bytes until end
    char *data = malloc(frame_size);

    // copy residue bytes first from last recv
    size_t off = mem_write_data(data, 0, buf + sizeof(int), received - sizeof(int));

    while(off < frame_size) {
        rv = recv(sockfd, buf, MAXMSGLEN, 0);
        if (rv < 0) {
            free(data);
            err(1, 0);
        }
        off = mem_write_data(data, off, buf, rv);
    }
    fprintf(stderr, "client finished receiving resp frame: [%d]\n", frame_size);

    // unmarshal
    rpc_resp *resp =malloc(sizeof(rpc_resp));
    read_resp(data, resp);
    fprintf(stderr, "resp size: [%u]\n", resp->size);
    free(data);
    return resp;
}

/**
 * @brief init program.
 * This function is automatically called when program is started
 */
void _init(void) {
    orig_close = dlsym(RTLD_NEXT,"close");
    orig_write = dlsym(RTLD_NEXT,"write");
    orig_read = dlsym(RTLD_NEXT,"read");

    fprintf(stderr, "Init mylib\n");
    _sockfd = init_client();
    min_fd = INT_MAX;
    opened_fd = 0;
}


