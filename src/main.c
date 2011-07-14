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
#include "config_file.h"
#include "mod_so.h"
#include "socket.h"

#include <errno.h>
#include <regex.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/param.h>
#include <sys/stat.h>


int vlevel = 0;

static void regex_init(void);
static void daemonize(void);
static void usage(char *name);

int
main(int argc, char **argv)
{
	char config_file[PATH_MAX+1];
	
	/* Parse command line arguments. */
	{
		int ch, dflag = 0;
		opterr = 0;
		while((ch = getopt(argc, argv, "dvc:")) != -1)
		{
			switch(ch)
			{
				case 'd':
					dflag = 1;
					break;
					
				case 'v':
					if(++vlevel > 3)
						vlevel = 3;
					break;
				
				case 'c':
					strncpy(config_file, optarg, PATH_MAX);
					break;
					
				case '?':
				default:
					usage(argv[0]);
					exit(1);
					break;
			}
		}
		if(dflag == 1)
			daemonize();
	}
	
	/* Determine what configuration file to use. */
	if(strlen(config_file) > 0)
	{
		if(access(config_file, F_OK|R_OK) != 0)
		{
			switch(errno)
			{
				case ENOENT:
					if(vlevel > 0)
						fprintf(stderr, "[INFO] Can't find configuration file (%s).\n", config_file);
					break;
				
				case EACCES:
					if(vlevel > 0)
						fprintf(stderr, "[INFO] Can't access the file (%s).\n", config_file);
					break;
			}
			exit(1);
		}
	}
	else
	{
		char *homedir = getenv("HOME");
		
		snprintf(config_file, PATH_MAX, "%s/%s", homedir, CONFIG_FILENAME);
		if(access(config_file, F_OK|R_OK) != 0)
		{
			switch(errno)
			{
				case ENOENT:
					if(vlevel > 0)
						fprintf(stderr, "[INFO] Can't find configuration file (%s).\n", config_file);
					break;
					
				case EACCES:
					if(vlevel > 0)
						fprintf(stderr, "[INFO] Can't access the file (%s).\n", config_file);
					break;
			}
			if(vlevel > 0)
				fprintf(stderr, "[INFO] Attempting to use default configuration in %s.\n", CONFIG_DEFAULT_PATH);
			
			if(access(CONFIG_DEFAULT_PATH, F_OK|R_OK) != 0)
			{
				fprintf(stderr, "No configuration file was found.\n");
				exit(1);
			}
			strncpy(config_file, CONFIG_DEFAULT_PATH, PATH_MAX);
		}
	}
	
	/* Initialize our module system just before we read our configs. */
	mod_init();
	
	/* Get bot configurations and place in bots. */
	bots = calloc(1, sizeof(*bots));
	if(bots == NULL)
		return -1;
	read_config(config_file);
	
#ifdef OPENSSL_ENABLED
	/* If compiled with OpenSSL support, setup thread locking callbacks and locks. */
	ssl_init();
#endif /* OPENSSL_ENABLED */
	
	/* Initialize our regular expressions. */
	regex_init();
	
	
	/* XXX Remove this for release. */
	//mod_load("libmod_urltools.dylib");
	
	/* Create all nessesary threads. */
	{
		struct bot_in *next_bot = bots->b_first;
		
		/* Make our threads detachable. */
		pthread_attr_init(&thread_attr);
		pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
		
		/* Set some thread specific stuffs. */
		pthread_key_create(&m_sock_fds, NULL);
		pthread_key_create(&bot, NULL);
		pthread_key_create(&irc_s, NULL);
		
		/* Launch a new thread per bot. */
		for(; next_bot != NULL; next_bot = next_bot->next)
			bot_spawn(next_bot);
	}
	
	/* Exit the current thread and allow the others to continue. */
	pthread_exit(NULL);
	return(0);
}

/*
 *
 */
static void
regex_init(void)
{
	/* Initialize our regex for parsing IRC message. */
	if(regcomp(&irc_mesg_re, "^:([^ ]+) ([^ ]+) ?\\*? ([^ ]+) :([^[:cntrl:]]*)$", REG_EXTENDED) != 0)
	{
		/* XXX Handle exception... D: */
		exit(1);
	}
	
	/* Add some FreeSWITCH specific regex. */
	if(regcomp(&fs_caller_name_re, "^Caller-Caller-ID-Name: ([^[:cntrl:]]+)$", REG_EXTENDED|REG_ICASE|REG_NEWLINE) != 0)
	{
		/* XXX Handle exception... D: */
		exit(1);
	}
	
	if(regcomp(&fs_caller_num_re, "^Caller-Caller-ID-Number: (%2b1)?([0-9]{10})$", REG_EXTENDED|REG_ICASE|REG_NEWLINE) != 0)
	{
		/* XXX Handle exception... D: */
		exit(1);
	}
	
	if(regcomp(&fs_conference_re, "^Conference-Name: ([^[:cntrl:]]+)$", REG_EXTENDED|REG_ICASE|REG_NEWLINE) != 0)
	{
		/* XXX Handle exception... D: */
		exit(1);
	}
}

/*
 * Daemonize the bot...
 */
static void
daemonize(void)
{
	pid_t pid;
	
	/* Free the terminal by creating an orphan. */
	if((pid = fork()) == -1)
	{
		perror("daemonize(): fork");
		exit(1);
	}
	else if(pid != 0)
		exit(0);
	
	/* Becoming the process group session leader... */
	setsid();
	
	/* Kill the process group session leader so we can't get a new tty. */
	if((pid = fork()) == -1)
	{
		perror("daemonize(): fork");
		exit(1);
	}
	else if(pid != 0)
		exit(0);
	
	/*
	 * Change directory to / so we don't lock the file system.
	 * Also set our own umask since we don't want whatever the user's was.
	 */
	chdir("/");
	umask(0);
	
	/* Close stdin, stdout, stderr and open our own. */
	fclose(stdin);
	fclose(stdout);
	fclose(stderr);
	
	stdin = fopen("/dev/null", "r");
	stdout = fopen("/dev/null", "w");
	stderr = fopen("/dev/null", "w");
}

/*
 * Print the usage information to stdout.
 */
static void
usage(char *name)
{
	/* XXX Add the bot's usage information. */
	printf(
		   "Usage:\t"
		   "%s [-d] [-v[v[v]]] [-c file]\n",
		   basename(name)
	);
}
