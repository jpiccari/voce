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

#ifndef _H_GLOBAL
#define _H_GLOBAL

/* Global included header files. */
#include "config.h"

#include <pthread.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bot.h"


/* Global constants. */
#define MAX_THREADS					15
#define BOT_VERSION_STRING			"O-bot v11.4"

#define VOUT_FLOW_INBOUND			1
#define VOUT_FLOW_OUTBOUND			2
#define VOUT_FLOW_NONE				3


/* Global structs and variables. */
pthread_t threads[MAX_THREADS];
int vlevel;
regex_t irc_mesg_re;
regex_t fs_caller_name_re;
regex_t fs_caller_num_re;
regex_t fs_conference_re;


/* Inline function for verbose output. */

/*
 * Print out verbose information.
 * Return value:
 *   None.
 */
static inline void
vout(int level, int direction, const char *endpoint, const char *str)
{
	static char flow[] = "---";
	struct bot_in *bot_t;
	
	if(vlevel < level)
		return;
	
	switch (direction) {
		case VOUT_FLOW_NONE:
			strcpy(flow, "<->");
			break;
		case VOUT_FLOW_INBOUND:
			flow[2] = '>';
			break;
		case VOUT_FLOW_OUTBOUND:
			flow[0] = '<';
			break;
		default:
			return;
	}
	
	bot_t = pthread_getspecific(bot);
	fprintf(stdout, "[%s %s %s] %s\n", endpoint, flow, bot_t->irc_nick, str);
}


/*
 * C implementation of the XPath normalize-space() function.
 * Return value:
 *   Returns a pointer to the normalized string or NULL on failure.
 */
static inline char *
normalize_space(const char *source)
{
	size_t len, offset = 0;
	char *dest, *part, *temp;
	
	/* Sanity checks. */
	if(source == NULL)
		return(NULL);
	
	if((temp = strdup(source)) == NULL)
		return(NULL);
	if((dest = calloc(strlen(source)+1, sizeof(*dest))) == NULL)
		return(NULL);
	
	for(len = strlen(temp), part = strtok(temp, "\r\n\t\v ");
		part != NULL;
		part = strtok(NULL, "\r\n\t\v "))
	{
		strcpy(dest+offset, part);
		offset += strlen(part)+1;
		
		if(offset < len)
			memset(dest+offset-1, ' ', 1);
	}
	free(temp);
	
	return(dest);
}

#endif /* _H_GLOBAL */
