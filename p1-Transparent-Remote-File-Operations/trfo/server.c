/**
 * @file server.c
 * @brief server library for remote procedure call.
 * It starts the server and implements a serial of
 * handlers to handel client requests.
 *
 * @author Zishen Wen <zishenw@andrew.cmu.edu>
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include "serde.h"

#define MAXMSGLEN   4096
#define MAXTREESIZE 40960
#define FD_OFFSET   1000

void handle_session(int sessfd);
void send_all(int sessfd, const void *data, size_t size);
int pack_fd(int fd);
int unpack_fd(int fd);
rpc_resp * handle(const struct rpc_frame* frame);
rpc_resp * do_open(const rpc_frame* frame);
rpc_resp * do_close(const rpc_frame* frame);
rpc_resp * do_write(const rpc_frame* frame);
rpc_resp * do_read(const rpc_frame* frame);
rpc_resp * do_lseek(const rpc_frame* frame);
rpc_resp * do_stat(const rpc_frame* frame);
rpc_resp * do_unlink(const rpc_frame* frame);
rpc_resp * do_getdirentries(const rpc_frame *frame);
rpc_resp * do_dirtreenode(const rpc_frame *frame);

int main(int argc, char**argv) {
    fprintf(stderr, "-----rpc server-----\n");
	char *serverport;
	unsigned short port;
	int sockfd, sessfd, rv;
	struct sockaddr_in srv, cli;
	socklen_t sa_size;
	
	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (serverport) port = (unsigned short)atoi(serverport);
	else port=15440;
	
	// Create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
	if (sockfd<0) err(1, 0);			// in case of error
	
	// setup address structure to indicate server port
	memset(&srv, 0, sizeof(srv));			// clear it first
	srv.sin_family = AF_INET;			// IP family
	srv.sin_addr.s_addr = htonl(INADDR_ANY);	// don't care IP address
	srv.sin_port = htons(port);			// server port

	// bind to our port
	rv = bind(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
	if (rv<0) err(1,0);
	
	// start listening for connections
	rv = listen(sockfd, 5);
	if (rv<0) err(1,0);
    fprintf(stderr, "===== server started on port %d\n", port);
	// main server loop, handle clients one at a time, quit after 10 clients
	while(1) {
		// wait for next client, get session socket
		sa_size = sizeof(struct sockaddr_in);
        fprintf(stderr, "listening...\n");
		sessfd = accept(sockfd, (struct sockaddr *)&cli, &sa_size);
        fprintf(stderr, "\n===\nnew connection (%d)\n", sessfd);
		if (sessfd<0) err(1,0);
        rv = fork();
        if (rv == 0) { // child process
            fprintf(stderr, "fork child - handling request...\n");
            close(sockfd);
            handle_session(sessfd);
            close(sessfd);
            fprintf(stderr, "request end...\n");
            exit(0);
        }
        close(sessfd);
	}
	printf("server shutting down cleanly\n");

    // close socket
	close(sockfd);
	return 0;
}

int pack_fd(int fd) {
    if (fd < 0) {
        return fd;
    }
    return fd + FD_OFFSET;
}

int unpack_fd(int fd) {
    if (fd < 0) {
        return fd;
    }
    return fd - FD_OFFSET;
}

// send all bytes in data
void send_all(int sessfd, const void *data, size_t size) {
    int frame_size = (int)size;
    char buf[MAXMSGLEN];
    ssize_t rv;
    size_t pending = size;
    fprintf(stderr, "server send_all data [%zu]\n", size);

    // size of package first
    size_t off = mem_write_data(buf, 0, &frame_size, sizeof(int));
    while(pending > 0) {
        // actual data size to be sent in this round
        size_t len = pending > MAXMSGLEN - off? MAXMSGLEN - off: pending;
        // fill in buffer
        off = mem_write_data(buf, off, data + (size - pending), len);
        // send, may end up in multiple sends
        size_t sent = 0;
        while((rv = send(sessfd, buf + sent, off - sent, 0)) > 0) {
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
    fprintf(stderr, "server send_all finished\n");
}

void handle_session(int sessfd) {
    ssize_t rv;
    char buf[MAXMSGLEN];
    if (sessfd<0) err(1,0);

    // get messages and send replies to this client, until it goes away
    while ( (rv=recv(sessfd, buf, MAXMSGLEN, 0)) > 0) {
        fprintf(stderr, "server received new frame\n");

        int frame_size = 0;
        ssize_t received = rv;

        // receive bytes of size sizeof(int) to indicate size of frame
        while(received < sizeof(int)) {
            rv = recv(sessfd, buf + received, MAXMSGLEN - received, 0);
            if (rv < 0) err(1,0);
            received += rv;
        }

        memcpy(&frame_size, buf, sizeof(int));

        if (frame_size <= 0) {
            fprintf(stderr, "server error - invalid frame size? [%d]\n", frame_size);
            err(1,0);
        }

        // receive the rest of bytes until end
        char *data = malloc(frame_size);

        // copy residue bytes first from last recv
        size_t off = mem_write_data(data, 0, buf + sizeof(int), received - sizeof(int));

        while(off < frame_size) {
            rv = recv(sessfd, buf, MAXMSGLEN, 0);
            if (rv < 0) {
                err(1, 0);
            }
            off = mem_write_data(data, off, buf, rv);
        }
        fprintf(stderr, "server finished receiving frame: [%d]\n", frame_size);

        // unmarshal
        struct rpc_frame* frame = malloc(sizeof(rpc_frame));
        if(!read_frame(data, frame)) {
            free(frame);
            free(data);
            err(1,0);
        }

        // handle request
        rpc_resp * resp = handle(frame);

        // marshal resp
        char *out = malloc(resp->size + sizeof(rpc_resp));
        size_t len = marshal_resp(out, resp);

        // send response
        fprintf(stderr, "server response to client..[%zu]\n", len);
        send_all(sessfd, out, len);	// should check return value

        // free resource
        free(resp->data);
        free(resp);
        free(out);
        free(frame->payload);
        free(frame);
        free(data);
    }
    // either client closed connection, or error
    if (rv<0) err(1,0);
}



rpc_resp* handle(const struct rpc_frame* frame) {
    switch (frame->opcode) {
        case OP_OPEN:
            return do_open(frame);
        case OP_CLOSE:
            return do_close(frame);
        case OP_WRITE:
            return do_write(frame);
        case OP_READ:
            return do_read(frame);
        case OP_LSEEK:
            return do_lseek(frame);
        case OP_STAT:
            return do_stat(frame);
        case OP_UNLINK:
            return do_unlink(frame);
        case OP_GETDIR:
            return do_getdirentries(frame);
        case OP_GETTRR:
            return do_dirtreenode(frame);
        default:
            err(1, 0);
    }
}

rpc_resp * do_dirtreenode(const rpc_frame *frame) {
    fprintf(stderr, "do dirtreenode\n");
    char *path = malloc(MAXMSGLEN);
    rpc_resp *resp = malloc(sizeof(rpc_resp));
    fprintf(stderr, "frame size: [%d]\n", frame->payload_size);
    call_dirtreenode_unmarshal(frame->payload, path);
    struct dirtreenode* tree= getdirtree(path);
    resp->err_no = errno;
    size_t off = 0;
    resp->data = malloc(MAXTREESIZE);
    if (NULL != tree) {
        off = mem_write_tree(tree, resp->data, 0);
        freedirtree(tree);
    }
    resp->size = off;
    fprintf(stderr, "return dirtreenode size %zu\n", off);
    free(path);
    return resp;
}

rpc_resp* do_getdirentries(const rpc_frame* frame) {
    fprintf(stderr, "do getdirentries\n");
    int fd;
    size_t nbytes = 0;
    off_t basep = 0;
    rpc_resp *resp = malloc(sizeof(rpc_resp));
    fprintf(stderr, "frame size: [%d]\n", frame->payload_size);
    call_getdirentries_unmarshal(frame->payload, &fd, &nbytes, &basep);
    char *buf = malloc(nbytes);
    fd = unpack_fd(fd);

    ssize_t r = getdirentries(fd, buf, nbytes, &basep);
    resp->err_no = errno;
    resp->data = malloc(MAXMSGLEN);
    size_t off = mem_write_data(resp->data, 0, &r, sizeof(ssize_t));
    off = mem_write_data(resp->data, off, &basep, sizeof(off_t));
    if (r > 0) {
        off = mem_write_data(resp->data, off, buf, r);
    }
    resp->size = off;
    fprintf(stderr, "op: getdirentries return %zd\n", r);
    free(buf);
    return resp;
}

rpc_resp* do_unlink(const rpc_frame* frame) {
    fprintf(stderr, "do unlink\n");
    char *pathname = malloc(MAXMSGLEN);
    rpc_resp *resp = malloc(sizeof(rpc_resp));
    fprintf(stderr, "frame size: [%d]\n", frame->payload_size);
    call_unlink_unmarshal(frame->payload, pathname);
    int r = unlink(pathname);
    resp->err_no = errno;
    resp->data = malloc(MAXMSGLEN);
    size_t off = mem_write_int32(resp->data, 0, r);
    resp->size = off;
    fprintf(stderr, "op: unlink return %d\n", r);
    free(pathname);
    return resp;
}

rpc_resp* do_stat(const rpc_frame* frame) {
    fprintf(stderr, "do __xstat\n");
    int ver;
    char *path = malloc(MAXMSGLEN);
    struct stat stat_buf;
    rpc_resp *resp = malloc(sizeof(rpc_resp));
    fprintf(stderr, "frame size: [%d]\n", frame->payload_size);
    call_stat_unmarshal(frame->payload, &ver, path);
    int r = __xstat(ver, path, &stat_buf);
    resp->err_no = errno;
    resp->data = malloc(MAXMSGLEN);
    size_t off = mem_write_int32(resp->data, 0, r);
    if (r>= 0) {
        off = mem_write_data(resp->data, off, &stat_buf, sizeof(struct stat));
    }
    resp->size = off;
    fprintf(stderr, "op: __xstat return %d\n", r);
    free(path);
    return resp;
}

rpc_resp* do_lseek(const rpc_frame* frame) {
    fprintf(stderr, "do lseek\n");
    int fd;
    off_t offset;
    int whence;
    rpc_resp *resp = malloc(sizeof(rpc_resp));

    fprintf(stderr, "frame size: [%d]\n", frame->payload_size);
    call_lseek_unmarshal(frame->payload, &fd, &offset, &whence);
    fd = unpack_fd(fd);

    off_t r = lseek(fd, offset, whence);
    resp->err_no = errno;
    resp->size = sizeof(off_t);
    resp->data = malloc(resp->size);
    mem_write_data(resp->data, 0, &r, sizeof(off_t));
    //fprintf(stderr, "op: lseek return %lld\n", r);
    return resp;
}

rpc_resp* do_read(const rpc_frame* frame) {
    fprintf(stderr, "do read\n");
    int fd_in;
    size_t count;
    rpc_resp *resp = malloc(sizeof(rpc_resp));

    fprintf(stderr, "frame size: [%d]\n", frame->payload_size);
    call_read_unmarshal(frame->payload, &fd_in, &count);
    int fd = unpack_fd(fd_in);
    char *buf = malloc(count);
    ssize_t r = read(fd, buf, count);
    resp->err_no = errno;
    resp->data = malloc(r + sizeof(ssize_t));
    size_t off = 0;
    off = mem_write_data(resp->data, off, &r, sizeof(ssize_t));
    // skip if read error
    if (r > 0) {
        off = mem_write_data(resp->data, off, buf, r);
    }
    resp->size = off;
    fprintf(stderr, "op: read return %zd\n", r);
    free(buf);
    return resp;
}

rpc_resp* do_open(const rpc_frame* frame) {
    fprintf(stderr, "do open\n");
    char *pathname = malloc(MAXMSGLEN);
    u_int32_t flag;
    u_int16_t mode;
    rpc_resp *resp = malloc(sizeof(rpc_resp));

    fprintf(stderr, "frame size: [%d]\n", frame->payload_size);
    call_open_unmarshal(frame->payload, pathname, &flag, &mode);
    int fd = open(pathname, (int)flag, mode);
    int fd_out = pack_fd(fd);
    resp->err_no = errno;
    resp->size = sizeof(int);
    resp->data = malloc(resp->size);
    mem_write_data(resp->data, 0, &fd_out, resp->size);
    fprintf(stderr, "op: open return fd %d\n", fd_out);
    free(pathname);
    return resp;
}

rpc_resp* do_close(const rpc_frame* frame) {
    fprintf(stderr, "do close\n");
    int fd_in;
    rpc_resp *resp = malloc(sizeof(rpc_resp));

    call_close_unmarshal(frame->payload, &fd_in);
    int fd = unpack_fd(fd_in);
    int r = close(fd);
    resp->err_no = errno;
    resp->size = sizeof(int);
    resp->data = malloc(resp->size);
    mem_write_data(resp->data, 0, &r, resp->size);
    fprintf(stderr, "op: close return %d\n", r);
    return resp;
}

rpc_resp* do_write(const rpc_frame* frame) {
    fprintf(stderr, "do write\n");
    int fd_in;
    rpc_resp *resp = malloc(sizeof(rpc_resp));
    size_t count;

    char *buf = call_write_unmarshal(frame->payload, &fd_in, &count);
    int fd = unpack_fd(fd_in);
    ssize_t r = write(fd, buf, count);
    resp->err_no = errno;
    resp->size = sizeof(ssize_t);
    resp->data = malloc(resp->size);
    mem_write_data(resp->data, 0, &r, resp->size);
    fprintf(stderr, "op: write return %ld\n", r);
    free(buf);
    return resp;
}
