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

#include "../modules.h"
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include <curl/curl.h>
#include <libxml/xpath.h>
#include <libxml/HTMLparser.h>


#define PARSER_OPTIONS	HTML_PARSE_RECOVER|HTML_PARSE_NOERROR|		\
						HTML_PARSE_NOWARNING|HTML_PARSE_NOBLANKS|	\
						XML_PARSE_NOENT|XML_PARSE_NOCDATA

struct membuf
{
	size_t size;
	char *data;
};

/*
 * Variables used in this module.
 *   urlt_buffer	- buffer used for libcurl.
 *   urlt_curl		- holds the libcurl session structure.
 *   urlt_links_re	- used to find URLs in IRC messages.
 *   urlt_mh		- holds the module structure for this module.
 */
static CURL *urlt_curl;
static regex_t urlt_links_re;
static struct mod_object *urlt_mh;
static struct membuf *urlt_buffer;


/*
 * C implementation of the XPath normalize-space() function.
 * Return value:
 *   Returns a pointer to the normalized string or NULL on failure.
 */
static char *
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

/*
 * Called by libcurl when a page is fetched.
 * Return value:
 *   Returns the number of bytes handled.
 */
static size_t
writer(void *buf, size_t size, size_t nmemb, void *stream)
{
	size_t rsize = size * nmemb;
	struct membuf *mem = urlt_buffer;
	
	if(mem == NULL)
		return(rsize);
	
	if(mem->data == NULL)
		mem->data = malloc(rsize+1);
	else
		mem->data = realloc(mem->data, mem->size+rsize+1);
	
	if(mem->data != NULL)
	{
		memcpy(&(mem->data[mem->size]), buf, rsize);
		mem->size += rsize;
		mem->data[mem->size] = 0;		
	}
	
	return(rsize);
}

/*
 * Fetch an HTML page using libcurl and parse using libxml2.
 * Return value:
 *   Returns a pointer to an xmlDoc or NULL on error.
 */
static xmlDoc *
get_html(const char *url)
{
	xmlDoc *html = NULL;
	
	if(url == NULL)
		return(NULL);
	
	/* Set the URL and make a request. */
	curl_easy_setopt(urlt_curl, CURLOPT_URL, url);
	
	if(curl_easy_perform(urlt_curl) != 0)
		return(NULL);
	
	/* Parse HTML using libxml2. */
	html = htmlReadMemory(urlt_buffer->data, urlt_buffer->size,
						  NULL, NULL, PARSER_OPTIONS);
	
	free(urlt_buffer->data);
	urlt_buffer->data = NULL;
	urlt_buffer->size = 0;
	
	return(html);
}

/*
 * Does a quick XPath search on an XML-style document tree.
 * Return value:
 *   Returns a pointer to an xmlXPathObject or NULL on error.
 */
static xmlXPathObject *
quick_xpath(xmlDoc *doc, const char *query)
{
	xmlXPathContext *xpath_ctx;
	xmlXPathObject *xpath_obj;
	
	/* The usual sanity stuff. */
	if(doc == NULL || query == NULL)
		return(NULL);
	
	/* Create an XPath context. */
	if((xpath_ctx = xmlXPathNewContext(doc)) == NULL)
		return(NULL);
	
	/* Get an XPath object and return it. */
	xpath_obj = xmlXPathEvalExpression(query, xpath_ctx);
	xmlXPathFreeContext(xpath_ctx);
	
	return(xpath_obj);
}

/*
 * Get the title of a URL.
 * Return value:
 *   Returns a pointer to the title of the URL, or NULL on error.
 */
static unsigned char *
url_to_title(const char *url)
{
	unsigned char *title = NULL;
	xmlDoc *html;
	xmlXPathObject *xpath_obj;
	
	/* Sanity checks. */
	if(url == NULL || (html = get_html(url)) == NULL)
		goto err_not_found;
	
	if((xpath_obj = quick_xpath(html, "//title")) == NULL)
		goto err_xpath_fail;
	
	if(xpath_obj->nodesetval == NULL || xpath_obj->nodesetval->nodeTab == NULL ||
	   (title = xmlNodeGetContent(xpath_obj->nodesetval->nodeTab[0])) == NULL)
		goto err_no_title;
	
	title = strdup(title);
	
err_no_title:
	xmlXPathFreeObject(xpath_obj);
err_xpath_fail:
	xmlFreeDoc(html);
err_not_found:
	return(title);
}

/*
 * Parse IRC messages looking for links or commands.
 * Return value:
 *   Returns either MOD_EAT_NONE or MOD_EAT_ALL.
 */
int
irc_callback(char *from, char *to, char *command, char *mesg)
{
	if(strcmp(command, "PRIVMSG") != 0)
		return(MOD_EAT_NONE);
	
	if(strncmp(COMMAND_PREFIX, mesg, strlen(COMMAND_PREFIX)) == 0)
	{
		xmlDoc *html = NULL;
		xmlXPathObject *xpath_obj = NULL;
		
		mesg += strlen(COMMAND_PREFIX);
		
		/* Do a google define search. */
		if(strncmp(mesg, "define ", 7) == 0 && strlen(mesg) > 7)
		{
			size_t url_len;
			char *url, *temp;
			
			mesg += 7;
			temp = curl_easy_escape(urlt_curl, mesg, 0);
			url_len = strlen(temp)+41;
			
			if((url = calloc(url_len, sizeof(*url))) == NULL)
				goto err_out;
			
			snprintf(url, url_len,
					 "http://www.google.com/search?q=define%%3A%s", temp);
			
			curl_free(temp);
			
			/* Get our page so we can parse it. */
			if((html = get_html(url)) == NULL)
				goto err_out;
			
			/* Get an XPath object for parsing. */
			if((xpath_obj = quick_xpath(html, "//ul[@class='std']/li")) == NULL)
				goto err_xpath_fail;
			
			else
			{
				int i, node_size;
				
				if(xpath_obj->nodesetval == NULL)
				{
					irc_cmd(IRC_PRIVMSG, to, "No definitions were found.");
					goto err_eek;
				}
				
				node_size = xpath_obj->nodesetval->nodeNr;
				
				for(i = 0; i < node_size && i < 3; ++i)
				{
					size_t mesg_len;
					xmlNode *cur = xpath_obj->nodesetval->nodeTab[i];
					unsigned char *temp, *content = xmlNodeGetContent(cur);
					
					mesg_len = strlen(content)+4;
					if((temp = calloc(mesg_len+1, sizeof(*temp))) == NULL)
						goto err_eek;
					
					snprintf(temp, mesg_len, "[%d] %s", i+1, content);
					
					irc_cmd(IRC_PRIVMSG, to, temp);
					free(temp);
					xmlFree(content);
				}
			}
		}
		
		/* Do a google search for a given keyword. */
		else if(strncmp(mesg, "google ", 7) == 0 && strlen(mesg) > 7)
		{
			size_t url_len;
			char *url, *temp;
			
			mesg += 7;
			temp = curl_easy_escape(urlt_curl, mesg, 0);
			url_len = strlen(temp)+32;
			
			if((url = calloc(url_len, sizeof(*url))) == NULL)
				goto err_out;
			
			snprintf(url, url_len,
					 "http://www.google.com/search?q=%s", temp);
			
			curl_free(temp);
			
			/* Get our page so we can parse it. */
			if((html = get_html(url)) == NULL)
				goto err_out;
			
			/* Get an XPath object for parsing. */
			if((xpath_obj = quick_xpath(html, "//a[@class='l']")) == NULL)
				goto err_xpath_fail;
			
			else
			{
				int i, node_size;
				
				if(xpath_obj->nodesetval == NULL)
				{
					irc_cmd(IRC_PRIVMSG, to, "No search results were found.");
					goto err_eek;
				}
				
				node_size = xpath_obj->nodesetval->nodeNr;
				
				for(i = 0; i < node_size && i < 3; ++i)
				{
					size_t link_len;
					xmlNode *cur = xpath_obj->nodesetval->nodeTab[i];
					unsigned char *link, *title = xmlNodeGetContent(cur),
										 *href = xmlGetProp(cur, "href");
					
					link_len = strlen(title)+strlen(href)+8;
					link = calloc(link_len+1, sizeof(*link));
					
					snprintf(link, link_len, "[ %s ] -- %s", title, href);
					irc_cmd(IRC_PRIVMSG, to, link);
					free(link);
					xmlFree(title);
					xmlFree(href);
				}
			}
		}
		
		/* Do an IMDB lookup, I love me some movies! */
		else if(strncmp(mesg, "imdb ", 4) == 0 && strlen(mesg) > 5)
		{
			size_t url_len;
			char *url, *temp;
			
			mesg += 5;
			temp = curl_easy_escape(urlt_curl, mesg, 0);
			url_len = strlen(temp)+34;
			
			if((url = calloc(url_len, sizeof(*url))) == NULL)
				goto err_out;
			
			snprintf(url, url_len,
					 "http://www.imdb.com/find?s=all&q=%s", temp);
			
			curl_free(temp);
			
			/* Get our page so we can parse it. */
			if((html = get_html(url)) == NULL)
				goto err_out;
			
			/* Get an XPath object for parsing. */
			xpath_obj = quick_xpath(html,
									"//div[@id='main']/table[2]/tr/td[3]/a"
									);
			if(xpath_obj == NULL)
				goto err_xpath_fail;
			
			else
			{
				int i, node_size;
				
				if(xpath_obj->nodesetval == NULL)
				{
					irc_cmd(IRC_PRIVMSG, to, "No movies were found.");
					goto err_eek;
				}
				
				node_size = xpath_obj->nodesetval->nodeNr;
				
				for(i = 0; i < node_size && i < 3; ++i)
				{
					size_t link_len;
					xmlNode *cur;
					unsigned char *link, *temp_link, *title, *href;
					
					/* Sanity checks. */
					if(xpath_obj->nodesetval->nodeTab[i] == NULL)
						continue;
					
					/* Setup some pointers. */
					cur = xpath_obj->nodesetval->nodeTab[i];
					title = xmlNodeGetContent(cur);
					href = xmlGetProp(cur, "href");
					
					link_len = strlen(title)+strlen(href)+28;
					temp_link = calloc(link_len+1, sizeof(*temp_link));
					
					snprintf(temp_link, link_len,
							 "[ %s ] -- http://www.imdb.com%s", title, href);
					if((link = normalize_space(temp_link)) != NULL)
					{
						irc_cmd(IRC_PRIVMSG, to, link);
						free(link);
					}
					xmlFree(title);
					xmlFree(href);
					free(temp_link);
				}
			}
		}
		
		/* Do a PHP lookup. */
		else if(strncmp(mesg, "php ", 4) == 0 && strlen(mesg) > 4)
		{
			size_t url_len;
			char *url, *temp;
			
			mesg += 4;
			temp = curl_easy_escape(urlt_curl, mesg, 0);
			url_len = strlen(temp)+20;
			
			if((url = calloc(url_len+1, sizeof(*url))) == NULL)
				goto err_out;
			
			snprintf(url, url_len,
					 "http://us2.php.net/%s", temp);
			
			curl_free(temp);
			
			/* Get our page so we can parse it. */
			if((html = get_html(url)) == NULL)
				goto err_out;
			
			/* Get an XPath object for parsing. */
			xpath_obj =
				quick_xpath(html,
							"//div[@id='content']/"
							"div[substring(@id,1,9)='function.']/"
							"div[@class='refsect1 description']/"
							"div[@class='methodsynopsis dc-description']"
							);
			if(xpath_obj == NULL)
				goto err_xpath_fail;
			
			else
			{
				if(xpath_obj->nodesetval == NULL ||
				   xpath_obj->nodesetval->nodeNr < 1)
				{
					irc_cmd(IRC_PRIVMSG, to, "Could not find PHP function.");
					goto err_eek;
				}
				
				{
					size_t mesg_len;
					xmlNode *cur = xpath_obj->nodesetval->nodeTab[0];
					unsigned char *mesg, *temp, *prototype = xmlNodeGetContent(cur);
					
					mesg_len = strlen(prototype)+strlen(url)+5;
					if((temp = calloc(mesg_len+1, sizeof(*mesg))) == NULL)
					{
						xmlFree(prototype);
						goto err_eek;
					}
					
					snprintf(temp, mesg_len, "%s -- %s", prototype, url);
					if((mesg = normalize_space(temp)) != NULL)
					{
						irc_cmd(IRC_PRIVMSG, to, mesg);
						free(mesg);
					}
					xmlFree(prototype);
					free(temp);
				}
			}
		}
		
		/* Return and allow other modules to process the message. */
		else
			return(MOD_EAT_NONE);
		
	err_eek:
		if(xpath_obj != NULL)
			xmlXPathFreeObject(xpath_obj);
	err_xpath_fail:
		if(html != NULL)
			xmlFreeDoc(html);
	err_out:
		return(MOD_EAT_ALL);
	}
	
	/* If nothing else, check for a URL in there. */
	else
	{
		size_t len, num = urlt_links_re.re_nsub+1;
		regmatch_t *preg = calloc(num, sizeof(*preg));
		
		if(regexec(&urlt_links_re, mesg, num, preg, 0) == 0)
		{
			char *url;
			
			len = preg[1].rm_eo-preg[1].rm_so;
			url = calloc(len+1, sizeof(*url));
			strncpy(url, mesg+preg[1].rm_so, len);
			
			if(url != NULL)
			{
				size_t mesg_len;
				unsigned char *out, *temp, *title;
				
				if((title = url_to_title(url)) == NULL)
					goto err_no_title;
				
				mesg_len = strlen(title)+5;
				if((temp = calloc(mesg_len, sizeof(*out))) == NULL)
					goto err_no_mem;
				
				snprintf(temp, mesg_len, "[ %s ]", title);
				if((out = normalize_space(temp)) != NULL)
				{
					irc_cmd(IRC_PRIVMSG, to, out);
					free(out);
				}
				
				free(temp);
			err_no_mem:
				free(title);
			err_no_title:
				free(url);
			}
			free(preg);
			
			return(MOD_EAT_ALL);
		}
	}
}

/*
 * Module initialization and library setup.
 * Return value:
 *   None.
 */
void
module_init(struct mod_object *module_handler)
{
	urlt_mh = module_handler;
	
	/* Setup libcurl for future use. */
	urlt_curl = curl_easy_init();
	
	/* Setup our curl options. */
	curl_easy_setopt(urlt_curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(urlt_curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(urlt_curl, CURLOPT_WRITEFUNCTION, writer);
	curl_easy_setopt(urlt_curl, CURLOPT_WRITEDATA, urlt_buffer);
	
	/* Tell cURL not to verify any SSL certs if available. */
	curl_easy_setopt(urlt_curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(urlt_curl, CURLOPT_SSL_VERIFYHOST, 0L);
	
	/*
	 * Set our User-Agent so we don't freak out some sites (IMDB).
	 * Apparently smarter sites and Google don't like it when you use the
	 * Googlebot User-Agent... so now we are spoofing Firefox 3.5 on FreeBSD.
	 */
	curl_easy_setopt(urlt_curl, CURLOPT_USERAGENT,
					 "Mozilla/5.0 (X11; U; FreeBSD i386; en-US; rv:1.9.1) "
					 "Gecko/20090703 Firefox/3.5");
	
	/* Initialize and check libxml2 version. */
	LIBXML_TEST_VERSION;
	
	/* Setup our regex patterns. */
	if(regcomp(&urlt_links_re, "(http://[^]),[:space:]]{4,})",
			   REG_EXTENDED|REG_ICASE) != 0)
		return;
	
	/* Get some RAM for our buffer. */
	if((urlt_buffer = calloc(1, sizeof(*urlt_buffer))) == NULL)
	{
		mod_unload(module_handler->filename);
		return;
	}
	
	/* Register our IRC callback function. */
	mod_register_irc(urlt_mh, &irc_callback);
}
