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

#include "FreeSWITCH.h"

#include <stdlib.h>


/*
 *
 */
void _int(void)
{
	/* XXX Register callbacks... */
}

/*
 * Connect to FreeSWITCH's mod_event_socket, send user password, and
 * register to recieve events.
 */
int fs_connect(struct socket_in **s, char *host, char *port)
{
	/* Get some memory for our struct. */
	*s = calloc(1, sizeof(**s));
		
	if(socket_create(*s, host, port, 0) == -1)
		return -1;
	
	return 0;
}

/*
 * Parse events and execute the appropriate functions.
 */
int fs_parse(char *buf)
{
	struct bot_in *bot_t = pthread_getspecific(bot);
	struct socket_in *fs_t = pthread_getspecific(fs_s);
	
	vout(3, "FS", "->", buf);
	
	/* If a user is added to/deleted from a conference, announce it. */
	if(strncmp(buf+strlen(buf)-20, "Action: add-member", 18) == 0 ||
	   strncmp(buf+strlen(buf)-20, "Action: del-member", 18) == 0)
	{
		char mesg[513], *caller, *conf;
		size_t len, num = fs_caller_name_re.re_nsub+1, num2 = fs_caller_num_re.re_nsub+1;
		regmatch_t *preg = calloc(num, sizeof(*preg)), *preg2 = calloc(num2, sizeof(*preg2));
		
		if(regexec(&fs_conference_re, buf, num, preg, 0)  != 0)
			return 0;
		
		len = preg[1].rm_eo-preg[1].rm_so;
		conf = calloc(len+1, sizeof(*conf));
		strncpy(conf, buf+preg[1].rm_so, len);
		
		if(regexec(&fs_caller_num_re, buf, num2, preg2, 0) == 0)
		{
			len = preg2[2].rm_eo-preg2[2].rm_so;
			caller = calloc(len+3, sizeof(*caller));
			
			/* Setup our mask. */
			memcpy(caller, "XXX-XXX-XXXX", 12);
			
			/* Copy in our real digits. */
			memcpy(caller, buf+preg2[2].rm_so, 3);
			memcpy(caller+4, buf+preg2[2].rm_so+3, 3);
			/*
			 * We keep the suffix masked for privacy. You can decide to
			 * show the last for digits of phone numbers by uncommenting the following.
			 * memcpy(caller+8, buf+preg2[2].rm_so+6, 4);
			 */
		}
		
		else if(regexec(&fs_caller_name_re, buf, num, preg, 0) == 0)
		{
			len = preg[1].rm_eo-preg[1].rm_so;
			caller = calloc(len+1, sizeof(*caller));
			strncpy(caller, buf+preg[1].rm_so, len);
		}
		
		else
		{
			caller = calloc(8, sizeof(*caller));
			strncpy(caller, "Unknown", 8);
		}
		
		/* XXX Use MySQL to get the name of the conference and the channel to say it in. */
		
		
		if(strncmp(buf+strlen(buf)-20, "Action: add-member", 18) == 0)
			snprintf(mesg, 512, "%s has joined the %s bridge (ext. %s).", caller, conf, conf);
		else
			snprintf(mesg, 512, "%s is leaving the %s bridge (ext. %s).", caller, conf, conf);
		
		/*
		 * XXX Replace #telconinja with the actual channel the info should go to.
		 * We will be using the MySQL stuff for this.. but for now, everthing to #tn
		 */
		irc_cmd(IRC_ACTION, "#telconinja", mesg);
		
		/* Do some cleanup. */
		free(caller);
		/*free(chan); */
		free(conf);
		free(preg);
		free(preg2);
		
		return 0;
	}
	
	if(strncmp(buf, "Content-Type: api/response", 26) == 0)
	{
		switch(bot_t->fs_last_api)
		{
			case FS_CONFLIST:
				irc_cmd(IRC_PRIVMSG, "#bots", strstr(buf, "\n\n")+2);
				break;
		}
		return 0;
	}
	
	/*
	 * If we need to authenticate then do so, and also send the event request for the
	 * conference notices.
	 * XXX This probably only happens at the start of the connection and therefore
	 * should appear closer to the end of this function.
	 */
	if(strcmp(buf, "Content-Type: auth/request") == 0)
	{
		/*
		 * XXX After we get the configuration stuff done, this should be changed
		 * to use the value in the bot's configuration file.
		 */
		sprintf(buf, "auth %s\n\nevent plain CUSTOM conference::maintenance\n\n", bot_t->fs_pass);
		socket_send(fs_t, buf);
		return 0;
	}
	
	return 0;
}

/*
 * Make a FreeSWITCH API call and parse the response.
 * Return value:
 *   On success 0 is returned, otherwise -1.
 */
int fs_api_call(int type, char *arg1, char *arg2)
{
	size_t len = 1;
	char *buf;
	struct bot_in *bot_t = pthread_getspecific(bot);
	struct socket_in *fs_t = pthread_getspecific(fs_s);
	
	if(arg1 != NULL)
		len += strlen(arg1);
	if(arg2 != NULL)
		len += strlen(arg2);
	
	switch(type)
	{
		case FS_CONFLIST:
			socket_send(fs_t, "api conference list\n\n");
			break;
		case FS_KICK:
			len += 24;
			buf = malloc(sizeof(*buf)*len+1);
			snprintf(buf, len, "api conference %s kick %s\n\n", arg1, arg2);
			socket_send(fs_t, buf);
			free(buf);
			break;
		case FS_RAW:
			len += 6;
			buf = malloc(sizeof(*buf)*len+1);
			snprintf(buf, len, "api %s\n\n", arg1);
			socket_send(fs_t, buf);
			free(buf);
			break;
		default:
			return 1;
	}
	
	bot_t->fs_last_api = type;
	
	return 0;
}

/*
 * FreeSWITCH specific wrapper around socket_recv().
 * Return value:
 *   Returns 0 on success, or -1 on fatal socket error.
 */
int fs_recv(char **buf)
{
	char *temp;
	fd_set *m_sock_fds_t = pthread_getspecific(m_sock_fds);
	struct socket_in *fs_t = pthread_getspecific(fs_s);
	
	if(socket_recv(fs_t, *buf, "\n\n") == -1)
	{
		switch(errno)
		{
			case EBADF:
			case ECONNRESET:
			case ENOTCONN:
			case ENOTSOCK:
			case EFAULT:
				return -1;
		}
	}
	
	do
	{
		size_t buf_len, content_len = 0;
		char *cbuf;
		
		/*
		 * Default copy address of buf to temp. If we find content-length
		 * then we'll free it and overwrite.
		 */
		temp = *buf;
		
		/* If there is a content-len header, get the full message. */
		if((cbuf = strstr(temp, "Content-Length: ")) != NULL)
		{
			struct socket_buf *b = &fs_t->buffer;
			
			content_len = atoi(cbuf+16);
			buf_len = strlen(*buf)+2;
			
			/* Create some memory and get the rest of the buffer. */
			temp = calloc(buf_len+content_len+b->offset+1, sizeof(*temp));
			memcpy(temp, *buf, buf_len-2);
			memset(temp+buf_len-2, '\n', 2);
			memcpy(temp+buf_len, b->buf, b->offset);
			
			socket_recv_bytes(fs_t, temp+buf_len+b->offset, content_len);
			
			/* Zero out our buffer. */
			memset(b, 0, sizeof(*b));
		}
		
		if(fs_parse(temp) == -1)
		{
			/* The socket had a fatal error so we close it down. */
			FD_CLR(fs_t->fd, m_sock_fds_t);
			socket_close(fs_t);
			return -1;
		}
		if(content_len > 0)
			free(temp);
	}
	while(socket_next_chunk(fs_t, *buf, "\n\n") != 0);
	
	return 0;
}
