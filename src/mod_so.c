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
#include "mod_so.h"
#include "irc.h"

#include <dlfcn.h>
#include <libgen.h>
#include <pthread.h>
#include <stdlib.h>


static struct mod_object *modules;
static pthread_mutex_t mtx_mod;


/*
 * Initialize our module subsystem.
 * Return value:
 *   None.
 */
void
mod_init(void)
{
	/* XXX Steup mutex and such. */
	
	/* Initialize our lock on module globals. */
	pthread_mutex_init(&mtx_mod, NULL);
}

/*
 * Load a module, scary...
 * Return value:
 *   Returns 0 on success, otherwise returns error code.
 */
int
mod_load(char *mod)
{
	struct mod_object *mhand, *mlist;
	
	void (*module_init)(struct mod_object *);
	int (**func_mod_load)(char *);
	int (**func_mod_unload)(const char *);
	int (**func_irc_cmd)(int, const char *, const char *);
	int (**func_mod_register_irc)(struct mod_object *,
								  int (*)(const char *, const char *,
										  const char *, const char *));
	
	/* Allocate memory and load our .so */
	if((mhand = calloc(1, sizeof(*mhand))) == NULL)
		goto not_enough_mem;
	
	if((mhand->dl_handler = dlopen(mod, RTLD_LAZY|RTLD_LOCAL)) == NULL)
		goto dlopen_error;
	
	else
	{
		char *file = basename(mod);
		if(file == NULL)
			return(-1);
		
		mhand->filename = strdup(file);
	}
	
	/* Lock our mutex while we add our new module. */
	pthread_mutex_lock(&mtx_mod);
	
	/* Get the last used module object in our chain. */
	if(modules == NULL)
		modules = mhand;
	
	else
	{
		for(mlist = modules; mlist->next != NULL; mlist = mlist->next)
		{
			if(mlist == mhand)
			{
				vout(3, VOUT_FLOW_INBOUND, "Modules", "Module already loaded.");
				goto dlopen_error;
			}
		}
		mlist->next = mhand;
	}
	
	/* Unlock our mutex, not necessary anymore. */
	pthread_mutex_unlock(&mtx_mod);
	
	/* Load our function pointers. */
	if((func_mod_load = dlsym(mhand->dl_handler, "mod_load")) != NULL)
		*func_mod_load = &mod_load;
	
	if((func_mod_unload = dlsym(mhand->dl_handler, "mod_unload")) != NULL)
		*func_mod_unload = &mod_unload;
	
	if((func_mod_register_irc = dlsym(mhand->dl_handler, "mod_register_irc")) != NULL)
		*func_mod_register_irc = &mod_register_irc;
	
	if((func_irc_cmd = dlsym(mhand->dl_handler, "irc_cmd")) != NULL)
		*func_irc_cmd = &irc_cmd;
	
	/* Runn our new plugin's module_init() function. */
	if((*(void **)(&module_init) = dlsym(mhand->dl_handler, "module_init")) != NULL)
		(*module_init)(mhand);
	
	return(0);
	
dlopen_error:
	free(mhand);
not_enough_mem:
	return(-1);
}

/*
 * Unload a module, and try not to break anything.
 * Return value:
 *   Returns 0 on success, otherwise returns error code.
 */
int
mod_unload(const char *mod)
{
	struct mod_object *mlist;
	
	if(modules == NULL || mod == NULL)
		return(-1); /* XXX More error handling requested. */
	
	/* Lock our mutex. */
	pthread_mutex_lock(&mtx_mod);
	
	/* Find the module before our module. */
	for(mlist = modules;
		mlist->next != NULL && mlist->next->filename == mod;
		mlist = mlist->next);
	
	if(mlist == NULL)
		return(-1);
	
	if(modules == mlist)
		modules = NULL;
	
	/* Yet another pointer dance. */
	{
		struct mod_object *temp = mlist->next;
		
		if(mlist->next != NULL)
		{
			mlist->next = mlist->next->next;
			mlist = temp;
		}
	}
	
	if(dlclose(mlist->dl_handler) != 0)
		return(-1);
	
	
	/* Unlock our mutex and free some memory. */
	pthread_mutex_unlock(&mtx_mod);
	
	/*
	 * Freeing memory memory.
	 * NOTE: don't free function pointers or dl_handler, bad things
	 *       bad things happen, ok.
	 */
	if(mlist != NULL)
	{
		if(mlist->filename != NULL)
			free(mlist->filename);
		
		free(mlist);
	}
	
	return(0);
}

/*
 * Call all module callback functions.
 * Return values:
 *   Returns 0 if more parsing should be done by the core parser,
 *   -1 on error, or greater than 0 otherwise.
 */
int
mod_irc_callback(const char *from, const char *to, const char *command, const char *mesg)
{
	int eat = MOD_EAT_NONE;
	struct mod_object *mlist = modules;
	
	if(mlist == NULL)
		return(eat);
	
	/* Lock our mutex so no module gets unloaded while we are using it. */
	pthread_mutex_lock(&mtx_mod);
	
	/*
	 * Loop through our list of loaded modules and call their irc_callback
	 * functions--if any.
	 */
	do
	{
		/* Skip any modules that don't use IRC callbacks. */
		if(mlist->irc_callback == NULL)
			continue;
		
		if((eat = (*mlist->irc_callback)(from, to, command, mesg)) != MOD_EAT_NONE)
			break;
	}
	while((mlist = mlist->next) != NULL);
	
	/* Unlock our mutex and return. */
	pthread_mutex_unlock(&mtx_mod);
	
	return(eat);
}

/*
 * Register a new module with a callback for IRC messages.
 * Return value:
 *   Returns 0 on success, otherwise -1 is returned.
 */
int
mod_register_irc(struct mod_object *mh,
					 int (*callback)(const char *from, const char *to,
									 const char *command, const char *mesg))
{
	if(mh == NULL || callback == NULL)
		return(-1);
	
	pthread_mutex_lock(&mtx_mod);
	mh->irc_callback = callback;
	pthread_mutex_unlock(&mtx_mod);
	
	return(0);
}
