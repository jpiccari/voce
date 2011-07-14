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

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>


/*
 * Create a socket for use by either the FreeSWITCH client or
 * the IRC client.
 *
 * Return value:
 *   Returns 0 on success or -1 on failure. The socket file
 *   discriptor is returned in socket_fd.
 */
int
socket_create(struct socket_in **s, const char *addr, const char *port, int ssl)
{
	int status;
	struct addrinfo hints;
	struct addrinfo *servinfo;
	struct socket_in *sock;
	
	/* Get some memory for our socket structure. */
	if((*s = calloc(1, sizeof(**s))) == NULL)
		return(-1);
	
	sock = *s;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	if((status = getaddrinfo(addr, port, &hints, &servinfo)) != 0)
	{
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		return(-1);
	}
	
	if((sock->fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1)
	{
		perror("[ERROR] socket_create(): socket()");
		return(-1);
	}
	
	if(connect(sock->fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
	{
		perror("[ERROR] socket_create(): connect()");
		return(-1);
	}
	
#ifdef OPENSSL_ENABLED
	if(ssl == 1)
	{
		if(ssl_start(sock) < 1)
		{
			/* XXX We have an issue with SSL connectivity. */
		}
	}
#endif /* OPENSSL_ENABLED */
	
	{
		int optval = 1;
		socklen_t optsize = sizeof(optval);
		/* Set in non-blocking mode and turn on TCP Keep-Alive and disable Nagle aglorithm. */
		fcntl(sock->fd, F_SETFL, O_NONBLOCK);
		setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, &optval, optsize);
		setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, &optval, optsize);
	}
	
	/* Get some memory for our work buffer. */
	if((sock->buffer = calloc(1, sizeof(*sock->buffer))) == NULL)
		return(-1);
	
	if((sock->buffer->w_next = calloc(SOCKET_WBUFSIZE, sizeof(*sock->buffer->w_next))) == NULL)
		return(-1);
	
	sock->buffer->w_start = sock->buffer->w_next;
	sock->servinfo = servinfo;
	
	return(0);
}

/*
 * Send data in buf to the socket in socket_fd.
 * Return value:
 *   Returns the number of bytes sent, or -1 on failure.
 */
size_t
socket_send(struct socket_in *s, const char *buf)
{
	size_t bytes = 0;
	
#ifdef OPENSSL_ENABLED
	if(s->ssl != NULL)
		bytes = ssl_send(s, buf);
	
	else
#endif /* OPENSSL_ENABLED */
	
	{
		int temp_bytes;
		size_t len = strlen(buf);
		do
		{
			if((temp_bytes = send(s->fd, buf+bytes, len-bytes, 0)) == -1)
				return -1;
			bytes += temp_bytes;
		}
		while(bytes < len);
	}
	
	return(bytes);
}

/*
 * Recieve up to len bytes from the socket in socket_fd and
 * store the result in buf.
 * Return value:
 *   Returns the number of bytes recieved, or -1 on failure.
 */
ssize_t
socket_recv(struct socket_in *s, const char *delim)
{
	ssize_t bytes = 0;
	
	/* Sanity checks. */
	if(s == NULL || s->buffer == NULL)
		return(-1);
	
#ifdef OPENSSL_ENABLED
	if(s->ssl != NULL)
		bytes = ssl_recv(s, delim);
		
	else
#endif /* OPENSSL_ENABLED */
	
	{
		struct socket_buf *b = s->buffer;
		ssize_t temp_bytes;
		bytes = (b->w_next-b->w_start);
		
		do
		{
			if((temp_bytes = recv(s->fd, b->w_next, SOCKET_WBUFSIZE-bytes, 0)) == -1)
			{
				if(errno != EAGAIN && errno != EINTR)
					return(errno);
				
				break;
			}
			
			bytes += temp_bytes;
			b->w_next += sizeof(*b->w_next)*temp_bytes;
		}
		while(strstr(b->w_start, delim) == NULL && bytes < SOCKET_WBUFSIZE);
	}
	
	/* Split up the results into a linked-list. */
	socket_chunk(s, delim);
	
	return(bytes);
}

/*
 * Recieve up to len bytes from the socket in socket_fd and
 * store the result in buf.
 * Return value:
 *   Returns the number of bytes recieved, or -1 on failure.
 */
size_t
socket_recv_bytes(struct socket_in *s, char *buf, size_t max_bytes)
{
	size_t bytes = 0;
	
	if(buf == NULL)
		return 0;
	
#ifdef OPENSSL_ENABLED
	if(s->ssl != NULL)
	{
		/*
		 * XXX Add ssl ~ socket_recv_bytes()
		 */
	}
#endif /* OPENSSL_ENABLED */
	
	{
		int temp_bytes;
		do
		{
			if((temp_bytes = recv(s->fd, buf+bytes, max_bytes-bytes, MSG_WAITALL)) == -1)
			{
				if(errno == EAGAIN)
					continue;
				
				perror("socket_recv_bytes()");
				return -1;
			}
			bytes += temp_bytes;
		}
		while(bytes < max_bytes);
	}
	
	return(bytes);
}

/*
 * Parse work buffer into a linked-list of data, using delim.
 * Return value:
 *   Returns 0 if delim is NULL or not found, otherwise the number
 *    of bytes moved from the work buffer.
 */
size_t
socket_chunk(struct socket_in *s, const char *delim)
{
	size_t len, delim_len, bytes_handled = 0;
	char *raw_data, *delim_search;
	struct socket_buf *b;
	struct socket_line *buf_line;
	
	if(s == NULL || s->buffer == NULL || delim == NULL)
		return(0);
	
	b = s->buffer;
	delim_len = strlen(delim);
	raw_data = b->w_start;
	
	while(1)
	{
		if((delim_search = strstr(raw_data, delim)) == NULL)
			break;
		
		if((len = (delim_search-raw_data)) < 1)
			break;
		
		/* Allocate memory for out chunk struct. */
		if((buf_line = calloc(1, sizeof(*buf_line))) == NULL)
			break;
		if((buf_line->l_data = calloc(len+1, sizeof(*buf_line->l_data))) == NULL)
			break;
		
		/* Copy our data into the new chunk. */
		strncpy(buf_line->l_data, raw_data, len);
		
		raw_data += len+delim_len;
		bytes_handled += len+delim_len;
		
		/* Find the end of the list and add our chunk. */
		if(b->l_first == NULL)
			b->l_first = b->l_last = buf_line;
		else
			b->l_last = b->l_last->l_next = buf_line;
	}
	
	/* Remove processed data off of our work buffer and reset the pointers. */
	if(bytes_handled > 0)
	{
		len = b->w_next-raw_data;
		b->w_next -= raw_data-b->w_start;
		
		memmove(raw_data, b->w_start, len);
		memset(b->w_next, 0, SOCKET_WBUFSIZE-len);
	}
	
	return(bytes_handled);
}

/*
 * Get the next chunk of data from the socket buffer and put it in buf.
 * Return value:
 *   Returns a pointer to the next chunk, or NULL on error.
 */
char *
socket_next_chunk(struct socket_in *s)
{
	char *data;
	struct socket_buf *b;
	struct socket_line *next_chunk;
	
	/* Sanity checks. */
	if(s == NULL || s->buffer == NULL || s->buffer->l_first == NULL)
		return(NULL);
	
	b = s->buffer;
	
	/* Make 'em pointers dance a bit. */
	next_chunk = b->l_first;
	b->l_first = b->l_first->l_next;
	
	/*
	 * If we are popping off the last frame in the stack, then
	 * we should be able to safely set the last frame to NULL.
	 */
	if(next_chunk == b->l_last)
		b->l_last = NULL;
	
	/* Free our structure but keep our char* buffer for parsing. */
	data = next_chunk->l_data;
	free(next_chunk);
	
	return(data);
}

/*
 * Close the socket connection found in socket_fd.
 * Return value:
 *   Returns 0 on success or -1 on failure.
 */
int
socket_close(struct socket_in *s)
{
#ifdef OPENSSL_ENABLED
	if(s->ssl != NULL)
		SSL_shutdown(s->ssl);
#endif /* OPENSSL_ENABLED */
	
	if(close(s->fd) == -1)
	{
		perror("[ERROR] socket_close(): close()");
		return(-1);
	}
	
	
	/* Loop through any existing buffer chunks and free them. */
	if(s->buffer != NULL)
	{
		struct socket_buf *b = s->buffer;
		
		if(b->w_start != NULL)
			free(b->w_start);
		
		if(b->l_first != NULL)
		{
			struct socket_line *temp, *chunk = b->l_first;
			while(chunk != NULL)
			{
				temp = chunk;
				chunk = chunk->l_next;
				
				if(temp->l_data != NULL)
					free(temp->l_data);
				free(temp);
			}
		}
		
		/* Free our actual buffer struct. */
		free(s->buffer);
	}
	
#ifdef OPENSSL_ENABLED
	if(s->ssl != NULL)
		SSL_free(s->ssl);
#endif /* OPENSSL_ENABLED */
	freeaddrinfo(s->servinfo);
	free(s);
	
	return(0);
}
