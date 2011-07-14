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

#ifndef _H_MOD_SO
#define _H_MOD_SO

/* Module included header files. */


/* Module constants. */
#define MOD_EAT_NONE		0
#define MOD_EAT_PLUGIN		1
#define MOD_EAT_ALL			2



/* Module structs and variables. */
struct mod_object
{
	void *dl_handler;
	char *filename;
	int (*irc_callback)(const char *from, const char *to,
						const char *command, const char *mesg);
	struct mod_object *prev;
	struct mod_object *next;
};



/* Functions used to interface with modules. */
void mod_init(void);
int mod_load(char *mod);
int mod_unload(const char *mod);
int mod_irc_callback(const char *from, const char *to,
					 const char *command, const char *mesg);
int mod_register_irc(struct mod_object *mh,
					 int (*callback)(const char *from, const char *to,
									 const char *command, const char *mesg));


#endif /* _H_MOD_SO */
