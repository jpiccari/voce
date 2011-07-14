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

#include "global.h"
#include "bot.h"
#include "irc.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>


static int bot_loop(void);

/*
 * This is where it all starts. The first function of our actual bot.
 * Return value:
 *   None.
 */
static void *
bot_thread(void *bot_config)
{
	fd_set *m_sock_fds_t = malloc(sizeof(*m_sock_fds_t));
	struct bot_in *bot_t = (struct bot_in *)bot_config;
	struct socket_in *irc_t;
	
	/* A quick break for sanity checks. */
	if(m_sock_fds_t == NULL || bot_t == NULL)
		pthread_exit(NULL);
	
	/* Clear our descripto sets in prep. for adding our sockets. */
	FD_ZERO(m_sock_fds_t);
	
	/* XXX Add IRC connection creation and add to m_read_fds. */
	if(irc_connect(&irc_t, bot_t->irc_host, bot_t->irc_port, bot_t->irc_ssl) == 0)
		FD_SET(irc_t->fd, m_sock_fds_t);
	
	
	/* Set our thread specific stuffs. */
	pthread_setspecific(m_sock_fds, m_sock_fds_t);
	pthread_setspecific(bot, bot_t);
	pthread_setspecific(irc_s, irc_t);
	
	
	/*
	 * Call our bot's loop function.
	 * If we return -1 that means we should try to reestablish a connections. Otherwise
	 * just free our memroy and quit.
	 */
	switch(bot_loop())
	{
		default:
			vout(4, VOUT_FLOW_INBOUND, "BOT", "Something went seriously wrong.");
		case E_NONE:
			bot_destory_config(bot_t);
			break;
		case E_REWAIT:
			sleep(30);
			/* Fallthrough */
		case E_RECONN:
			bot_spawn(bot_t);
			break;
	}
	
	/* Free up our memory and exit. */
	free(m_sock_fds_t);
	
	pthread_exit(NULL);
}

/*
 * Main bot loop. From here everything will be done!
 * Return value:
 *   Returns one of E_NONE, E_RECONN, or E_REWAIT.
 */
static int
bot_loop(void)
{
	char *buf = NULL;
	fd_set sock_fds, *m_sock_fds_t = pthread_getspecific(m_sock_fds);
	struct socket_in *irc_t = pthread_getspecific(irc_s);
	
	while(1)
	{
		/*
		 * XXX Are these the same? Confirm which is better.
		 * memcpy(&sock_fds, m_sock_fds_t, sizeof(*m_sock_fds_t));
		 * sock_fds = *m_sock_fds_t;
		 */
		memcpy(&sock_fds, m_sock_fds_t, sizeof(*m_sock_fds_t));
		if(select(FD_SETSIZE, &sock_fds, NULL, NULL, NULL) == -1)
		{
			/*
			 * XXX Handle select() errors, see man page for more details. For
			 * we will just break out of the loop, but soon some actual
			 * error checking.
			 */
			printf("Oops!\n\n");
			break;
		}
		
		/* Check the IRC's socket for data. */
		if(FD_ISSET(irc_t->fd, &sock_fds))
		{
			if(socket_recv(irc_t, "\r\n") == -1)
			{
				switch(errno)
				{
					case EBADF:
					case ECONNRESET:
					case ENOTCONN:
					case ENOTSOCK:
					case EFAULT:
						return(-1);
				}
			}
			
			while((buf = socket_next_chunk(irc_t)) != NULL)
			{
				int ret = irc_parse(buf);
				if(buf != NULL)
					free(buf);
				
				if(ret != 0)
				{
					socket_close(irc_t);
					return(ret);
				}
			}
		}
		
		
		/* XXX Here we will check our other sockets from our modules. */
	}
	
	/* This should be unreachable. */
	return(0);
}

/*
 * Creates a new bot config struct and updates other essential bot
 * structs that rely on their up-to-dateness.
 * Return value:
 *   A new bot config, or NULL on error.
 */
struct bot_in *
bot_new_config(void)
{
	/* Allocate some memory then do sanity checks. */
	struct bot_in *config = calloc(1, sizeof(*config));
	if(config == NULL)
		return(NULL);
	
	/* Lock our bots mutex. */
	pthread_mutex_lock(&mtx_bots);
	
	/* Add our new bot config to the chain. */
	if(bots->b_last == NULL)
		bots->b_first = bots->b_last = config;
	
	else
	{
		config->prev = bots->b_last;
		bots->b_last->next = config;
		bots->b_last = config;
	}
	
	/* Add some appropriate info. */
	config->bot_id = ++bots->bot_ids;
	
	/* Set status of the bot. */
	config->bot_status = BOT_STATUS_STARTING;
	
	/* Finally unlock our mutex... I know it was a long time. */
	pthread_mutex_unlock(&mtx_bots);
	
	return(config);
}

/*
 * Copies a bots config struct from orig to clone.
 * Return value:
 *   A cloned bot config, or NULL on error.
 */
struct bot_in *
bot_clone_config(const struct bot_in *orig)
{
	/* Get us some mem! */
	struct bot_in *clone = bot_new_config();
	
	/* Start copying over anything that isn't NULL. */
	clone->irc_ssl = orig->irc_ssl;
	
	if(orig->irc_host != NULL)
		clone->irc_host = strdup(orig->irc_host);
	if(orig->irc_port != NULL)
		clone->irc_port = strdup(orig->irc_port);
	
	if(orig->irc_nick != NULL)
		clone->irc_nick = strdup(orig->irc_nick);
	if(orig->irc_name != NULL)
		clone->irc_name = strdup(orig->irc_name);
	if(orig->irc_user != NULL)
		clone->irc_user = strdup(orig->irc_user);
	if(orig->irc_admins != NULL)
		clone->irc_admins = strdup(orig->irc_admins);
	
	/*
	 * This require a bit more work... Luckily we have a nice function.
	 */
	if(orig->irc_channels != NULL)
	{
		struct chan_list *cur = orig->irc_channels;
		
		for(; cur != NULL && cur->name != NULL; cur = cur->next)
		{
			bot_add_channel(clone, cur->name);
		}
	}
	
	return(clone);
}

/*
 * Remove a bot config from the bot chain and free memory.
 * Return value:
 *   Returns 0 on success, otherwise -1.
 */
int
bot_destory_config(struct bot_in *config)
{
	/* Sanity checks. */
	if(bots == NULL || bots->b_first == NULL || config == NULL)
		return(-1);
	
	
	/* Lock up our mutex. */
	pthread_mutex_lock(&mtx_bots);
	
	/* Pointer dance. */
	if(bots->b_first == config)
		bots->b_first = config->next;
	
	else
		config->prev->next = config->next;
	
	
	if(bots->b_last == config)
		bots->b_last = config->prev;
	
	
	/* Unlock now that we don't need it. */
	pthread_mutex_unlock(&mtx_bots);
	
	/* Free the memory from our bot config. */
	if(config != NULL)
	{
		if(config->irc_admins != NULL)
			free(config->irc_admins);
		
		if(config->irc_host != NULL)
			free(config->irc_host);
		
		if(config->irc_name != NULL)
			free(config->irc_name);
		
		if(config->irc_nick != NULL)
			free(config->irc_nick);
		
		if(config->irc_nick_temp != NULL)
			free(config->irc_nick_temp);
		
		if(config->irc_nspass != NULL)
			free(config->irc_nspass);
		
		if(config->irc_pass != NULL)
			free(config->irc_pass);
		
		if(config->irc_port != NULL)
			free(config->irc_port);
		
		if(config->irc_user != NULL)
			free(config->irc_user);
		
		/*
		 * Again slightly more complex...
		 * We don't use bot_remove_channel() because we don't want excesive
		 * mutex locking/unlocking >_<
		 */
		if(config->irc_channels != NULL)
		{
			struct chan_list *cur = config->irc_channels;
			
			/*
			 * Loop through and destory each channel object.
			 * This may look a bit odd but it seems the fastest way.
			 */
			while(cur != NULL)
			{
				if(cur->name != NULL)
					free(cur->name);
				
				if(cur->next != NULL)
				{
					cur = cur->next;
					free(cur->prev);
				}
				else
				{
					free(cur);
					break;
				}
			}
		}
		
		free(config);
	}
	
	return(0);
}

/*
 * Add a new channel(s) to our bot's channel list.
 * Return value:
 *   Returns 0 on success, otherwise returns -1.
 */
int
bot_add_channel(struct bot_in *bot_config, const char *channel)
{
	struct chan_list *clist = NULL, *cur_clist = NULL, *new_clist = NULL;
	
	/* Sanity checks... */
	if(bot_config == NULL)
		return(-1);
	
	
	/* Build our new channels. */
	if(strchr(channel, ',') == NULL)
	{
		/* Build our new channel structure(s). First clean stuff up. */
		if((new_clist = calloc(1, sizeof(*new_clist))) == NULL)
			return(-1);
		
		if((new_clist->name = strdup(channel)) == NULL)
			return(-1);
	}
	else
	{
		char *chan, *channels;
		struct chan_list *temp_clist;
		
		if((channels = malloc(sizeof(*channels)*(strlen(channel)+1))) == NULL)
			return(-1);
		
		/* Make a copy of our channels buffer. */
		strcpy(channels, channel);
		
		for(chan = strtok(channels, ",");
			chan != NULL;
			chan = strtok(NULL, ","))
		{
			if((temp_clist = calloc(1, sizeof(*temp_clist))) == NULL)
				return(-1);
			
			if((temp_clist->name = strdup(chan)) == NULL)
				return(-1);
			
			if(cur_clist == NULL)
			{
				cur_clist = temp_clist;
				new_clist = cur_clist;
			}
			else
			{
				cur_clist->next = temp_clist;
				temp_clist->prev = cur_clist;
			}
			cur_clist = temp_clist;
		}
		
		/* A bit of clean up. */
		if(channels != NULL)
			free(channels);
	}
	
	
	/* Find the next available channel slot. */
	pthread_mutex_lock(&mtx_bots);
	
	/*
	 * Insanely crazy double for-loop...
	 * XXX I'm sure someone can optimize this but at the moment it is very
	 *     late and I'm just glad it works.
	 */
	for(cur_clist = new_clist;
		cur_clist != NULL;
		cur_clist = cur_clist->next)
	{
		for(clist = bot_config->irc_channels;
			clist != NULL;
			clist = clist->next)
		{
			/* If we are already in the channel then ignore. */
			if(strcmp(clist->name, cur_clist->name) == 0)
			{
				struct chan_list *tmp = cur_clist;
				
				/* Sanity checks and pointer dance. */
				if(tmp->prev != NULL)
					tmp->prev->next = tmp->next;
				if(tmp->next != NULL)
					tmp->next->prev = tmp->prev;
				
				/* Step back 1 entry so our loop doesn't get borked. */
				if(cur_clist->prev != NULL)
					cur_clist = cur_clist->prev;
				else
					cur_clist = new_clist = NULL;
				
				/* Free the duplicate channel entry. */
				free(tmp->name);
				free(tmp);
				break;
			}
		}
		
		/* Prevent dereferencing a NULL pointer. */
		if(cur_clist == NULL || cur_clist->next == NULL)
			break;
	}
	
	/* Make sure we still have stuff to add. */
	if(new_clist == NULL)
	{
		pthread_mutex_unlock(&mtx_bots);
		return(-1);
	}
	
	/* Put our new channel at the end of our list. */
	if(bot_config->irc_channels == NULL)
		bot_config->irc_channels = new_clist;
	
	else
	{
		if(clist == NULL)
		{
			/* Fill clist and advance to the end of the list. */
			for(clist = bot_config->irc_channels;
				clist != NULL && clist->next != NULL;
				clist = clist->next);
		}
		if(clist != NULL)
		{
			clist->next = new_clist;
			new_clist->prev = clist;
		}
	}
	
	/* Unlock our mutex. */
	pthread_mutex_unlock(&mtx_bots);
	
	return(0);
}

/*
 * Remove a channel from our bot's config.
 * Return value:
 *   Returns 0 on success, otherwise returns -1.
 */
int
bot_remove_channel(struct bot_in *bot_config, const char *channel_old)
{
	char *channel;
	struct chan_list *clist;
	
	/* Sanity checks... */
	if(channel_old == NULL ||
	   bot_config == NULL ||
	   bot_config->irc_channels == NULL)
		return(-1);
	
	/* Remove any funky characters before we parse anything. */
	channel = normalize_space(channel_old);
	
	/* Lock our precious mutex. */
	pthread_mutex_lock(&mtx_bots);
	
	/* Find the channel in question. */
	for(clist = bot_config->irc_channels;
		strcmp(clist->name, channel) != 0 && clist->next != NULL;
		clist = clist->next);
	
	if(clist == NULL)
	{
		pthread_mutex_unlock(&mtx_bots);
		return(-1);
	}
	
	/* Do the usual pointer dance, then free the channel struct. */
	if(clist->prev != NULL)
		clist->prev->next = clist->next;
	else if(clist->next == NULL)
		bot_config->irc_channels = NULL;
	else
		bot_config->irc_channels = clist->next;
	
	if(clist->next != NULL)
		clist->next->prev = clist->prev;
	
	/* NULL out name member and free memory. */
	clist->name = NULL;
	free(clist->name);
	free(clist);
	
	/* Unlock our mutex now that we're done. */
	pthread_mutex_unlock(&mtx_bots);
	
	return(0);
}

/*
 * Start a new thread with a bot config.
 * Return value:
 *   None.
 */
void
bot_spawn(struct bot_in *bot_config)
{
	/*
	 * XXX Could probably use some mutex locks here... ya.
	 * Start our new thread.
	 */
	pthread_create(&bot_config->thread_id, &thread_attr,
				   bot_thread, (void *)bot_config);
}
