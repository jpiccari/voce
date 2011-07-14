/*-
 * Copyright (c) 2009 Joshua Piccari
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _H_SOCKET
#define _H_SOCKET

/* Socket included header files. */
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>

/* OpenSSL included header files. */
#ifdef WITH_SSL
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#endif /* WITH_SSL */

/* Socket constants. */
#define SOCKET_WBUFSIZE		4096
#define E_BUFTOOSMALL		0x01

#ifdef WITH_SSL
#if (OPENSSL_VERSION_NUMBER < 0x0090600fL)
#warning Must use OpenSSL 0.9.6 or later... disabling OpenSSL support.
#else /* OpenSSL version check */
#define OPENSSL_ENABLED
#endif /* OpenSSL version check */
#endif /* WITH_SSL */

/* Socket structs and variables. */
struct socket_line
{
	char *l_data;
	struct socket_line *l_next;
};
struct socket_buf
{
	char *w_start;
	char *w_next;
	struct socket_line *l_first;
	struct socket_line *l_last;
};
struct socket_in
{
	int fd;
	struct addrinfo *servinfo;
	struct socket_buf *buffer;
#ifdef OPENSSL_ENABLED
	SSL *ssl;
#endif /* OPENSSL_ENABLED */
};


/* Socket functions. */
int socket_create(struct socket_in **s, const char *addr, const char *port, int ssl);
size_t socket_send(struct socket_in *s, const char *buf);
ssize_t socket_recv(struct socket_in *s, const char *delim);
size_t socket_recv_bytes(struct socket_in *s, char *buf, size_t max_bytes);
size_t socket_chunk(struct socket_in *s, const char *delim);
char *socket_next_chunk(struct socket_in *s);
int socket_close(struct socket_in *s);


/* OpenSSL Specifics... bastard functions, and others. */
#ifdef OPENSSL_ENABLED

/* OpenSSL functions. */
int ssl_init(void);
void ssl_locking_callback(int mode, int n, const char *file, int line);
unsigned long ssl_threadid_callback(void);


#ifdef _SSL_STACK


void berr_exit(char *string);
int ssl_start(struct socket_in *s);
size_t ssl_send(struct socket_in *s, const char *buf);
ssize_t ssl_recv(struct socket_in *s, const char *delim);
size_t ssl_recv_bytes(struct socket_in *s, char *buf, size_t max_bytes);

#endif /* _SSL_STACK */
#endif /* OPENSSL_ENABLED */


#endif /* _H_SOCKET */
