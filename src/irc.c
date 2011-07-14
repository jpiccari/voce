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
#include "irc.h"
#include "mod_so.h"

#include <openssl/rand.h>


static void irc_respond(const char *from, const char *to,
						const char *command, const char *mesg);


/*
 * Connect to IRC.
 * Return value:
 *   Returns 0 on success and -1 on failure.
 */
int
irc_connect(struct socket_in **s, const char *host, const char *port, int ssl)
{
	/* Create our IRC socket. */
	if(socket_create(s, host, port, (ssl == 0 ? 0 : 1)) == -1)
		exit(-1);
	
	return(0);
}

/*
 * Parse IRC messages and execute the appropriate functions.
 * Return value:
 *   Returns -1 if the bot quit and should reconnect, otherwise 0.
 */
int
irc_parse(const char *buf)
{
	/* Add verbose output. */
	vout(2, VOUT_FLOW_INBOUND, "IRC", buf);
	
	/* Respond to PING with a PONG. */
	if(strncmp(buf, "PING :", 6) == 0)
	{
		irc_cmd(IRC_PONG, buf+6, NULL);
		return(0);
	}
	
	
	/* Split message into workable parts. */
	{
		int reg_err;
		size_t len;
		size_t num = irc_mesg_re.re_nsub+1;
		regmatch_t *preg = calloc(num, sizeof(*preg));
		char *from, *command, *to, *mesg;
		
		if((reg_err = regexec(&irc_mesg_re, buf, num, preg, 0)) != 0 && reg_err != REG_NOMATCH)
		{
			/* XXX Add error checking... */
			return(-1);
		}
		
		else if(reg_err == 0)
		{
			/* Get fields of the IRC message. */
			len = preg[1].rm_eo-preg[1].rm_so;
			if((from = malloc(sizeof(*from)*(len+1))) == NULL)
				goto no_mem_free_from;
			strncpy(from, buf+preg[1].rm_so, len);
			from[len] = '\0';
			
			len = preg[2].rm_eo-preg[2].rm_so;
			if((command = malloc(sizeof(*command)*(len+1))) == NULL)
				goto no_mem_free_command;
			strncpy(command, buf+preg[2].rm_so, len);
			command[len] = '\0';
			
			len = preg[3].rm_eo-preg[3].rm_so;
			if((to = malloc(sizeof(*to)*(len+1))) == NULL)
				goto no_mem_free_to;
			strncpy(to, buf+preg[3].rm_so, len);
			to[len] = '\0';
			
			len = preg[4].rm_eo-preg[4].rm_so;
			if((mesg = malloc(sizeof(*mesg)*(len+1))) == NULL)
				goto no_mem_free_mesg;
			strncpy(mesg, buf+preg[4].rm_so, len);
			mesg[len] = '\0';
			
			/* Send message off to be handled (or not) by irc_response(). */
			irc_respond(from, to, command, mesg);
			
			
			/* Free our memory and return. */
			no_mem_free_mesg:
				free(mesg);
			no_mem_free_to:
				free(to);
			no_mem_free_command:
				free(command);
			no_mem_free_from:
				free(from);
			
			return(0);
		}
	}

	/* Cleanup socket if it dies on us. */
	if(strncasecmp(buf, "ERROR :Closing Link:", 20) == 0)
	{
		/* We are going to need our bot info for this one... */
		struct bot_in *bot_t = pthread_getspecific(bot);
		
		if(bot_t->bot_status & BOT_STATUS_RESTARTING)
		{
			bot_t->bot_status = (bot_t->bot_status & ~BOT_STATUS_RESTARTING)|
								BOT_STATUS_STARTING;
			return(E_RECONN);
		}
		else if(strstr(buf, "(Throttled: Reconnecting too fast)") != NULL)
			return(E_REWAIT);
		else if(bot_t->bot_status & BOT_STATUS_QUITTING)
			return(E_NONE);
	}
	
	return(0);
}


/*
 * Sends a command to the IRC server.
 * Return value:
 *   Returns -1 if the command wasn't understood, 0 on success.
 */
int
irc_cmd(int type, const char *arg1, const char *arg2)
{
	char send_buf[513];
	struct bot_in *bot_t = pthread_getspecific(bot);
	struct socket_in *irc_t = pthread_getspecific(irc_s);
	
	/* Get the correct type of message to send. */
	switch(type)
	{
		case IRC_PONG:
			snprintf(send_buf, 512, "PONG :%s\r\n", arg1);
			break;
		case IRC_PRIVMSG:
			snprintf(send_buf, 512, "PRIVMSG %s :%s\r\n", arg1, arg2);
			break;
		case IRC_ACTION:
			snprintf(send_buf, 512, "PRIVMSG %s :\1ACTION %s\1\r\n", arg1, arg2);
			break;
		case IRC_NOTICE:
			snprintf(send_buf, 512, "NOTICE %s :%s\r\n", arg1, arg2);
			break;
		case IRC_JOIN:
			snprintf(send_buf, 512, "JOIN %s\r\n", arg1);
			bot_add_channel(bot_t, arg1);
			break;
		case IRC_PART:
			snprintf(send_buf, 512, "PART %s\r\n", arg1);
			bot_remove_channel(bot_t, arg1);
			break;
		case IRC_NICK:
			snprintf(send_buf, 512, "NICK %s\r\n", arg1);
			break;
		case IRC_MODE:
			snprintf(send_buf, 512, "MODE %s :%s\r\n", arg1, arg2);
			break;
		case IRC_NICKSERV:
			snprintf(send_buf, 512, "PRIVMSG NickServ :%s %s\r\n", arg1, arg2);
			break;
		case IRC_USER:
			snprintf(send_buf, 512, "USER %s * 8 :%s\r\n", arg1, (arg2 == NULL ? BOT_VERSION_STRING : arg2));
			break;
		case IRC_QUIT:
			bot_t->bot_status |= BOT_STATUS_QUITTING;
			snprintf(send_buf, 512, "QUIT :%s\r\n", arg1);
			break;
		case IRC_RAW:
			snprintf(send_buf, 512, "%s\r\n", arg1);
			break;
		default:
			return(-1);
	}
	
	/* Send off our command. */
	socket_send(irc_t, send_buf);
	
	/* Send verbose output. */
	send_buf[strlen(send_buf)-2] = '\0';
	vout(2, VOUT_FLOW_OUTBOUND, "IRC", send_buf);
	
	return(0);
}

/*
 * Checks if the source of an IRC message is an admin of this bot or not.
 * Return value:
 *   Returns 0 on success or -1 on failure.
 */
int
irc_is_admin(const char *ident)
{
	struct bot_in *bot_t = pthread_getspecific(bot);
	char *end, *admin = bot_t->irc_admins;
	
	if(admin == NULL)
		return(-1);
	
	while((end = strchr(admin, ',')) != NULL)
	{
		if(strncmp(ident, admin, end-admin-1) == 0)
		   return(0);
		admin = end+1;
	}
	
	return(-1);
}


/*
 * Send responses to the IRC server--if any are required.
 * Return value:
 *   None.
 */
static void
irc_respond(const char *from, const char *to, const char *command, const char *mesg)
{
	struct bot_in *bot_t = pthread_getspecific(bot);
	
	/* Sanity checking. */
	if(bot_t == NULL || from == NULL || to == NULL ||
	   command == NULL || mesg == NULL)
		return;
	
	/* Give our modules the first chance to hook some functions. */
	if(mod_irc_callback(from, to, command, mesg) == MOD_EAT_ALL)
		return;
	
	
	/* Check if there is a command to be run. */
	if(strncmp(mesg, COMMAND_PREFIX, strlen(COMMAND_PREFIX)) == 0)
	{
		mesg += strlen(COMMAND_PREFIX);
		
		/* User commands first, or something like that. */
		if(strncasecmp(mesg, "conf", 4) == 0)
		{
			/* Get the first argument. */
			int i = 0;
			for(; i++ < 5 && *(mesg+1) != '\0'; mesg++);
			
			if(strcasecmp(mesg, "list") == 0)
			{
				/*
				 * XXX Will implement soon...
				 * fs_api_call(FS_CONFLIST, NULL, NULL);
				 */
				irc_cmd(IRC_PRIVMSG, to, "Coming soon!");
			}
			return;
		}
		
		
		/* If it was a join request, chck if it came from an admin. */
		if(strncasecmp(mesg, "join ", 5) == 0 && irc_is_admin(from) == 0)
		{
			if(strlen(mesg) > 5)
				irc_cmd(IRC_JOIN, mesg+5, NULL);
			return;
		}
		
		/* If it was a join request, chck if it came from an admin. */
		if(strncasecmp(mesg, "part ", 5) == 0 && irc_is_admin(from) == 0)
		{
			if(strlen(mesg) > 5)
				irc_cmd(IRC_PART, mesg+5, NULL);
			return;
		}
		
		/* If it was a privmsg request, chck if it came from an admin. */
		if(strncasecmp(mesg, "say ", 4) == 0 && irc_is_admin(from) == 0)
		{
			if(strlen(mesg) > 4)
			{
				mesg += 4;
				if(*mesg == '#')
				{
					size_t chan_len = (strchr(mesg, ' ')-mesg)+1;
					char *chan = calloc(chan_len, sizeof(*chan));
					
					strncpy(chan, mesg, chan_len-1);
					chan[chan_len] = '\0';
					
					irc_cmd(IRC_PRIVMSG, chan, mesg+chan_len);
					free(chan);
				}
				else if(*to == '#')
					irc_cmd(IRC_PRIVMSG, to, mesg);
			}
			return;
		}
		
		/* If it was an action request, chck if it came from an admin. */
		if(strncasecmp(mesg, "me ", 3) == 0 && irc_is_admin(from) == 0)
		{
			if(strlen(mesg) > 4)
			{
				mesg += 3;
				if(*mesg == '#')
				{
					size_t chan_len = (strchr(mesg, ' ')-mesg)+1;
					char *chan = calloc(chan_len, sizeof(*chan));
					
					strncpy(chan, mesg, chan_len-1);
					chan[chan_len] = '\0';
					
					irc_cmd(IRC_ACTION, chan, mesg+chan_len);
					free(chan);
				}
				else if(*to == '#')
					irc_cmd(IRC_ACTION, to, mesg);
			}
			return;
		}
		
		/* If it was a nick change request, check if it came from an admin. */
		if(strncasecmp(mesg, "nick ", 5) == 0 && irc_is_admin(from) == 0)
		{
			if(strlen(mesg) > 5)
				irc_cmd(IRC_NICK, mesg+5, NULL);
			return;
		}
		
		/* If it was a raw IRC request, check if it came from an admin. */
		if(strncasecmp(mesg, "raw ", 4) == 0 && irc_is_admin(from) == 0)
		{
			if(strlen(mesg) > 4)
				irc_cmd(IRC_RAW, mesg+4, NULL);
			return;
		}
		
		/* If it was a quit request, check if it came from an admin. */
		if(strncasecmp(mesg, "quit", 4) == 0 && irc_is_admin(from) == 0)
		{
			irc_cmd(IRC_QUIT, (strlen(mesg) > 5 ? mesg+5 : BOT_VERSION_STRING), NULL);
			return;
		}
		
		/* If it was a restart request, check if it came from an admin. */
		if(strncasecmp(mesg, "reconnect", 9) == 0 && irc_is_admin(from) == 0)
		{
			irc_cmd(IRC_QUIT, (strlen(mesg) > 9 ? mesg+9 : BOT_VERSION_STRING), NULL);
			bot_t->bot_status |= BOT_STATUS_RESTARTING;
			return;
		}
		
		/* If it was a spawn request, check if it came from an admin. */
		if(strncasecmp(mesg, "spawn", 5) == 0 && irc_is_admin(from) == 0)
		{
			size_t s_len = strlen(mesg+6);
			char *n_nick;
			struct bot_in *n_bot;
			
			n_nick = calloc(s_len+1, sizeof(*n_nick));
			strncpy(n_nick, mesg+6, s_len);
			n_nick[s_len] = 0;
			
			/* Copy our bot config and make changes as nessesary. */
			n_bot = bot_clone_config(bot_t);
			if(n_bot->irc_nick != NULL)
				free(n_bot->irc_nick);
			
			n_bot->irc_nick = n_nick;
			
			/* Start our new thread. */
			bot_spawn(n_bot);
			
			return;
		}
		
		/* Loading and unloading modules... this is a first. */
		if(strncasecmp(mesg, "load ", 5) == 0 && irc_is_admin(from) == 0)
		{
			if(strlen(mesg) > 5)
				mod_load((char *)mesg+5);
			
			return;
		}
		
		if(strncasecmp(mesg, "unload ", 7) == 0 && irc_is_admin(from) == 0)
		{
			if(strlen(mesg) < 8)
				return;
			
			mod_unload(mesg+7);
			
			return;
		}
	}
	
	
	/* If our chosen nick is taken, create a new nick based on our old one. */
	if(strcmp(command, "433") == 0)
	{
		size_t nick_len = strlen(bot_t->irc_nick)+5;
#ifdef OPENSSL_ENABLED
		unsigned char rand_buf[2];
		RAND_bytes(rand_buf, 2);
#else
		int rand_buf[2];
		
		/*
		 * XXX Not even hardly psuedo-random, but doesn't need to be--I guess. I mean
		 * its not like two truely psuedo-random (or even random) bytes can produce much
		 * random data at only 2(2^8) bits. Long story short This isn't of any interest
		 * to me, but feel free to fix it if you start loosing sleep over it.
		 */
		srand(time(NULL)%65530);
		rand_buf[0] = rand();
		rand_buf[1] = rand();
#endif /* OPENSSL_ENABLED */
		
		bot_t->irc_nick_temp = calloc(nick_len, sizeof(char));
		snprintf(bot_t->irc_nick_temp, nick_len, "%s%02x%02x", bot_t->irc_nick,
				 rand_buf[0], rand_buf[1]);
		bot_t->irc_nick_temp[nick_len] = '\0';
		
		irc_cmd(IRC_NICK, bot_t->irc_nick_temp, NULL);
	}
	
	/*
	 * Check if we must idnet...
	 */
	if(strncmp(from, "NickServ!", 9) == 0 && strcmp(command, "NOTICE") == 0)
	{
		if(strncmp(mesg, "This nickname is registered", 27) == 0 &&
		   bot_t->irc_nspass != NULL)
		{
			irc_cmd(IRC_NICKSERV, "IDENTIFY", bot_t->irc_nspass);
		}
		
		/* NickServ freed up our nick for us so take it back. */
		else if(strcmp(mesg, "Ghost with your nick has been killed.") == 0)
		{
			irc_cmd(IRC_NICK, bot_t->irc_nick, NULL);
			free(bot_t->irc_nick_temp);
			bot_t->irc_nick_temp = NULL;
		}
		
		return;
	}
	
	/* End of MOTD, send our initial commands. */
	if(strcmp(command, "376") == 0 || strcmp(command, "422") == 0)
	{
		char *bnick = (bot_t->irc_nick_temp != NULL ? bot_t->irc_nick_temp : bot_t->irc_nick);
		struct chan_list *clist;
		
		/* If we think we own the nick, then try to take it over. */
		if(bot_t->irc_nick_temp != NULL)
		{
			if(bot_t->irc_nspass != NULL)
			{
				size_t buf_len = strlen(bot_t->irc_nick)+strlen(bot_t->irc_nspass)+3;
				char *buf = calloc(buf_len, sizeof(*buf));
				
				snprintf(buf, buf_len-1, "%s %s", bot_t->irc_nick, bot_t->irc_nspass);
				irc_cmd(IRC_NICKSERV, "GHOST", buf);
				
				free(buf);
			}
			else
			{
				free(bot_t->irc_nick);
				bot_t->irc_nick = bot_t->irc_nick_temp;
				
				/* Set temp nick to NULL since it isn't very 'temp' anymore. */
				bot_t->irc_nick_temp = NULL;
			}
		}
		
		/* If we have a NickServ password... then use it. */
		if(bot_t->irc_nspass != NULL)
			irc_cmd(IRC_NICKSERV, "IDENTIFY", bot_t->irc_nspass);
		
		irc_cmd(IRC_MODE, bnick, IRC_DEFAULT_MODES);
		
		/* Join all our channels. */
		for(clist = bot_t->irc_channels;
			clist != NULL;
			clist = clist->next)
		{
			irc_cmd(IRC_JOIN, clist->name, NULL);
		}
		
		bot_t->bot_status = (bot_t->bot_status & ~BOT_STATUS_STARTING)|
							BOT_STATUS_RUNNING;
		
		return;
	}
	
	/* Check if we are connected to the server. */
	if(strcasecmp(to, "AUTH") == 0 &&
	   (strstr(mesg, "Found your hostname") != NULL ||
		strstr(mesg, "Couldn't resolve your hostname") != NULL))
	{
		irc_cmd(IRC_USER, bot_t->irc_user, bot_t->irc_name);
		irc_cmd(IRC_NICK, bot_t->irc_nick, NULL);
		return;
	}
}
