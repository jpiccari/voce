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

#define _SSL_STACK

#include "global.h"
#include "socket.h"


#ifdef OPENSSL_ENABLED

static pthread_mutex_t *mtx_ssl;

static BIO *bio_err = NULL;
static struct
{
	SSL_METHOD *meth;
	SSL_CTX *ctx;
} *ssl_master;

/*
 * In case we have a major SSL failure, print the error and exit.
 * Return value:
 *   None.
 */
void
berr_exit(char *string)
{
	if(bio_err != NULL)
	{
		BIO_printf(bio_err, "[ERROR] %s\n", string);
		ERR_print_errors(bio_err);
	}
	pthread_exit(NULL);
}

/*
 * Negotiate a new SSL connection.
 * Return value:
 *   Returns 0 on success, otherwise -1.
 */
int
ssl_start(struct socket_in *s)
{
	if(ssl_master == NULL || ssl_master->ctx == NULL)
		return(-1);
	
	if((s->ssl = SSL_new(ssl_master->ctx)) == NULL)
		return(-1);
	
	if(SSL_set_fd(s->ssl, s->fd) != 1)
		return(-1);
	
	while(SSL_connect(s->ssl) == SSL_ERROR_NONE);
	
	return(0);
}

/*
 * Send a string to an SSL connection.
 * Return value:
 *   The number of bytes actually sent.
 */
size_t
ssl_send(struct socket_in *s, const char *buf)
{
	size_t bytes = 0, len = strlen(buf);
	int status;
	
	do
	{
		status = SSL_write(s->ssl, buf+bytes, len-bytes);
		switch(SSL_get_error(s->ssl, status))
		{
			case SSL_ERROR_NONE:
				bytes += status;
				break;
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
			case SSL_ERROR_WANT_CONNECT:
				return bytes;
			default:
				break;//berr_exit("ssl_send(): An error occured while writing to SSL socket.");
		}
	}
	while(bytes < len);
	
	return(bytes);
}

/*
 * Receive some bytes from an SSL connection.
 * Return value:
 *   Returns the number of bytes received.
 */
ssize_t
ssl_recv(struct socket_in *s, const char *delim)
{
	int temp_bytes;
	ssize_t bytes = 0;
	struct socket_buf *b = s->buffer;
	
	bytes = (b->w_next-b->w_start);
	
	do
	{
		temp_bytes = SSL_read(s->ssl, b->w_next, SOCKET_WBUFSIZE-bytes);
		switch(SSL_get_error(s->ssl, temp_bytes))
		{
			case SSL_ERROR_NONE:
				bytes += temp_bytes;
				b->w_next += temp_bytes;
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				break;
			case SSL_ERROR_WANT_CONNECT:
				goto out;
			default:
				berr_exit("ssl_recv(): An error occured while reading from SSL socket.");
		}
	}
	while(strstr(b->w_start, delim) == NULL && bytes < SOCKET_WBUFSIZE);
	
out:
	return(bytes);
	/*
	int temp_bytes;
	ssize_t bytes = 0;
	struct socket_buf *b = &s->buffer;
	
	
	bytes = b->offset;
	do
	{
		temp_bytes = SSL_read(s->ssl, b->buf+bytes, (SOCKET_BUFSIZE-bytes)-1);
		switch(SSL_get_error(s->ssl, temp_bytes))
		{
			case SSL_ERROR_NONE:
				bytes += temp_bytes;
				break;
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
			case SSL_ERROR_WANT_CONNECT:
				goto out;
			default:
				berr_exit("ssl_recv(): An error occured while reading from SSL socket.");
		}
	}
	while(strstr(b->buf, delim) == NULL && bytes < SOCKET_BUFSIZE);
out:
	b->offset = bytes;
	
	return(bytes);
	*/
}

/*
 * Receive a certain number of bytes.
 * Return value:
 *   The number of bytes actually received.
 */
size_t
ssl_recv_bytes(struct socket_in *s, char *buf, size_t max_bytes)
{
	int temp_bytes;
	size_t bytes = 0;
	
	do
	{
		temp_bytes = SSL_read(s->ssl, buf+bytes, (max_bytes-bytes)-1);
		switch(SSL_get_error(s->ssl, temp_bytes))
		{
			case SSL_ERROR_NONE:
				bytes += temp_bytes;
				break;
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
			case SSL_ERROR_WANT_CONNECT:
				break;
			default:
				berr_exit("ssl_recv_bytes(): An error occured while reading from SSL socket.");
		}
	}
	while(bytes < max_bytes);
	
	return(bytes);
}

/*
 * Initialize, setup, and cleanup SSL.
 * Return value:
 *   Returns 0 on success, otherwise -1.
 */
int
ssl_init(void)
{
	if(mtx_ssl == NULL)
	{
		int i;
		
		/* Initialize our SSL mutex locks. */
		mtx_ssl = OPENSSL_malloc(CRYPTO_num_locks()*sizeof(*mtx_ssl));
		if(mtx_ssl == NULL)
			return(-1);
		
		for(i=0; i < CRYPTO_num_locks(); i++)
			pthread_mutex_init(&(mtx_ssl[i]), NULL);
		
		/* Setup static locks. */
		CRYPTO_set_locking_callback(ssl_locking_callback);
		CRYPTO_set_id_callback(ssl_threadid_callback);
	}
	
	/* Setup our BIO error stuffs. */
	if(bio_err == NULL)
		bio_err = BIO_new_fp(stderr, BIO_NOCLOSE);
	
	/* Make sure the lib is not already initialized. */
	if(ssl_master != NULL)
		return(-1);
	
	if((ssl_master = calloc(1, sizeof(*ssl_master))) == NULL)
		return(-1);
	
	/* Initialize OpenSSL and setup our context. */
	SSL_library_init();
	SSL_load_error_strings();
	
	ssl_master->meth = SSLv23_client_method();
	
	if((ssl_master->ctx = SSL_CTX_new(ssl_master->meth)) == NULL)
		return(-1);
	
	/* Fine tune our context. */
	SSL_CTX_set_options(ssl_master->ctx, SSL_OP_NO_SSLv2);
	SSL_CTX_set_mode(ssl_master->ctx, SSL_MODE_AUTO_RETRY);
	SSL_CTX_set_mode(ssl_master->ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
	
	/* Initialize our PRNG with random data from /dev/urandom. */
	RAND_load_file("/dev/urandom", 1024);
	
	return(0);
}

/*
 * Do mutex locking for OpenSSL.
 * Return:
 *   None.
 */
void
ssl_locking_callback(int mode, int n, const char *file, int line)
{
	if(mode & CRYPTO_LOCK)
		pthread_mutex_lock(&(mtx_ssl[n]));	
	else
		pthread_mutex_unlock(&(mtx_ssl[n]));
}

/*
 * Get the thread ID of the current thread. Used by OpenSSL.
 * Return value:
 *   Returns the current thread's ID.
 */
unsigned long
ssl_threadid_callback(void)
{
	unsigned long ret;
	ret = (unsigned long)pthread_self();
	return(ret);
}

#endif /* OPENSSL_ENABLE */
