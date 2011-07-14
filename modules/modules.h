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

#ifndef _MODULES_H
#define _MODULES_H

/*
 * Include required files....
 */
#include "../include/config.h"

/*
 * The constants needed.
 */
#define MOD_EAT_NONE		0
#define MOD_EAT_PLUGIN		1
#define MOD_EAT_ALL			2

/* IRC specific */
#define IRC_ACTION				1
#define IRC_ME					IRC_ACTION
#define IRC_JOIN				2
#define IRC_MODE				3
#define IRC_NICK				4
#define IRC_NOTICE				5
#define IRC_NICKSERV			6
#define IRC_PART				7
#define IRC_PONG				8
#define IRC_PRIVMSG				9
#define IRC_QUIT				10
#define IRC_RAW					11
#define IRC_USER				12


/*
 * Variables and structures needed..
 */
struct mod_object
{
	void *dl_handler;
	char *filename;
	int (*irc_callback)(const char *from, const char *to,
						const char *command, const char *mesg);
	struct mod_object *prev;
	struct mod_object *next;
};


/*
 * The prototypes needed...
 */
int (*irc_cmd)(int type, char *arg1, char *arg2);
int (*mod_load)(char *mod);
int (*mod_unload)(const char *mod);
int (*mod_register_irc)(struct mod_object *mh,
					 int (*callback)(char *from, char *to, char *command, char *mesg));

#endif /* _MODULES_H */
