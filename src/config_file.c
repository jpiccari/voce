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
#include "config_file.h"

#include <errno.h>
#include <regex.h>
#include <stdlib.h>
#include <unistd.h>


static char *get_line(FILE *fp);

/*
 * Parses the configuration file.
 * Return value:
 *   Returns 0 on success or -1 on failure.
 */
int
read_config(char *path)
{
	FILE *fp;
	char *line, err[512];
	regex_t re;
	
	if((fp = fopen(path, "r")) == NULL)
	{
		if(errno == ENOENT)
			fprintf(stderr, "[ERROR] No configuration file (%s).\n", path);
		else
			fprintf(stderr, "[ERROR] %s\n", strerror(errno));
		exit(1);
	}
	
	if(regcomp(&re, "^([^=]+)=([^[:cntrl:]]+)", REG_EXTENDED) != 0)
	{
		/* XXX Error in regcomp(), do error checking. */
		return(-1);
	}
	
	{
		int reg_err;
		char *key, *value;
		size_t key_len, value_len;
		size_t num = re.re_nsub+1;
		struct bot_in *curr_bot = NULL;
		regmatch_t *pmatch = calloc(num, sizeof(*pmatch));
		if(pmatch == NULL)
			return(-1);
		
		while((line = get_line(fp)) != NULL)
		{
			/* XXX Do per line parsing. */
			
			if(strncmp(line, "[bot]", 5) == 0)
			{
				/* Add a new bot. */
				if((curr_bot = bot_new_config()) == NULL)
					return(-1);
				
				free(line);
				continue;
			}
			
			if((reg_err = regexec(&re, line, num, pmatch, 0)) != 0 && reg_err != REG_NOMATCH)
			{
				/* Error in regexec(), do error checking. */
				regerror(reg_err, &re, err, 512);
				printf("%s\n", err);
				return(-1);
			}
			
			key_len = pmatch[1].rm_eo-pmatch[1].rm_so;
			value_len = pmatch[2].rm_eo-pmatch[2].rm_so;
			
			key = calloc(key_len+1, sizeof(char));
			value = calloc(value_len+1, sizeof(char));
			
			strncpy(key, line, key_len);
			strncpy(value, line+pmatch[2].rm_so, value_len);
			key[key_len] = '\0';
			value[value_len] = '\0';
			
			/* Bit of sanity checking... */
			if(curr_bot == NULL)
				continue;
			
			if(strcmp(key, "irc_host") == 0)
			{
				curr_bot->irc_host = value;
			}
			else if(strcmp(key, "irc_port") == 0)
			{
				curr_bot->irc_port = value;
			}
			else if(strcmp(key, "irc_ssl") == 0)
			{
				curr_bot->irc_ssl = (strcmp(value, "yes") == 0 ? 1 : 0);
			}
			else if(strcmp(key, "irc_pass") == 0)
			{
				curr_bot->irc_pass = value;
			}
			else if(strcmp(key, "irc_nick") == 0)
			{
				curr_bot->irc_nick = value;
			}
			else if(strcmp(key, "irc_nspass") == 0)
			{
				curr_bot->irc_nspass = value;
			}
			else if(strcmp(key, "irc_user") == 0)
			{
				curr_bot->irc_user = value;
			}
			else if(strcmp(key, "irc_name") == 0)
			{
				curr_bot->irc_name = value;
			}
			else if(strcmp(key, "irc_channels") == 0)
			{
				bot_add_channel(curr_bot, value);
			}
			else if(strcmp(key, "irc_admin") == 0)
			{
				size_t offset = (curr_bot->irc_admins != NULL ? strlen(curr_bot->irc_admins) : 0);
				size_t len = (offset+1)+strlen(value)+2;
				
				if(curr_bot->irc_admins != NULL)
					curr_bot->irc_admins = realloc(curr_bot->irc_admins, sizeof(char)*len);
				else
					curr_bot->irc_admins = calloc(len, sizeof(char));
				
				snprintf(curr_bot->irc_admins+offset, len, "%s,", value);
				
				/* Free value since we are using our own memory. */
				free(value);
			}
			else
			{
				/* Free our value pairs that aren't going to be used. */
				free(value);
			}
			
			/* Free our key and line after each loop. */
			free(key);
			free(line);
		}
		/* Free our matches struct. */
		free(pmatch);
	}
	
	regfree(&re);
	return(0);
}

/*
 * Get a line from a file descriptor.
 * Return value:
 *   Returns a pointer to the line from the file.
 */
static char *
get_line(FILE *fp)
{
	int i = 2;
	char *temp, *buf = malloc(sizeof(*buf)*CONFIG_LINE_SIZE);
	
	if(!buf)
		return(NULL);
	
	if(fgets(buf, CONFIG_LINE_SIZE, fp) == NULL)
	{
		free(buf);
		return(NULL);
	}
	
	while((temp = strstr(buf, "\n")) == NULL)
	{
		temp = realloc(buf, CONFIG_LINE_SIZE*i);
		
		if(temp == NULL)
		{
			free(buf);
			return(NULL);
		}
		
		buf = temp;
		
		if(fgets(buf+CONFIG_LINE_SIZE*i++, CONFIG_LINE_SIZE, fp) == NULL)
			return(buf);
	}
	
	*temp = '\0';
	return(buf);
}
