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

#ifndef _H_BOT
#define _H_BOT

/* Bot included header files. */
#include <pthread.h>
#include <sys/types.h>


/* Bot constants. */
#define E_NONE				1
#define E_RECONN			2
#define E_REWAIT			3

/* Bot status bitmap. */
#define	BOT_STATUS_NORECONN		0x01
#define BOT_STATUS_RESTARTING	0x02
#define BOT_STATUS_RUNNING		0x04
#define BOT_STATUS_STARTING		0x08
#define	BOT_STATUS_QUITTING		0x10


/* Bot structs and variables. */
struct chan_list
{
	char *name;
	struct chan_list *prev;
	struct chan_list *next;
};

struct bot_in
{
	u_int bot_id;
	pthread_t thread_id;
	int bot_status;
	int irc_ssl;
	char *irc_admins;
	char *irc_host;
	char *irc_name;
	char *irc_nick;
	char *irc_nick_temp;
	char *irc_nspass;
	char *irc_pass;
	char *irc_port;
	char *irc_user;
	struct chan_list *irc_channels;
	struct bot_in *prev;
	struct bot_in *next;
};

struct
{
	u_int bot_ids;
	struct bot_in *b_first;
	struct bot_in *b_last;
} *bots;

pthread_attr_t thread_attr;
pthread_key_t m_sock_fds;
pthread_key_t bot;
pthread_key_t irc_s;
pthread_mutex_t mtx_bots;


/* Bot functions. */
struct bot_in *bot_new_config(void);
struct bot_in *bot_clone_config(const struct bot_in *orig);
int bot_destory_config(struct bot_in *config);
int bot_add_channel(struct bot_in *bot_config, const char *channel);
int bot_remove_channel(struct bot_in *bot_config, const char *channel);
void bot_spawn(struct bot_in *bot_config);


#endif /* _H_BOT */
