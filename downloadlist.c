/*  This file is part of "mirrorer" (TODO: find better title)
 *  Copyright (C) 2004 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <config.h>

#include <assert.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include "error.h"
#include "strlist.h"
#include "names.h"
#include "dirs.h"
#include "files.h"
#include "aptmethod.h"
#include "downloadlist.h"

struct downloaditem {
	struct download_upstream *upstream;
	struct downloaditem *nextinupstream;
	// todo: what is best, some tree, balanced tree, linear?
	struct downloaditem *next,*prev;
	char *filekey;
	char *origfile;
	char *md5sum;
};

struct download_upstream {
	struct downloadlist *list;
	struct download_upstream *next;
	char *method;
	char *config;
	struct downloaditem *items;
};
struct downloadlist {
	struct download_upstream *upstreams;
	filesdb filesdb;
	struct downloaditem *items;
};

filesdb downloadlist_filesdb(struct downloadlist *list) {
	return list->filesdb;
}


/* Initialize a new download session */
retvalue downloadlist_initialize(struct downloadlist **download,const char *dbdir,const char *pooldir) {
	struct downloadlist *list;
	retvalue r;

	list = calloc(1,sizeof(struct downloadlist));
	if( list == NULL )
		return RET_ERROR_OOM;

	r = files_initialize(&list->filesdb,dbdir,pooldir);
	if( RET_WAS_ERROR(r) ) {
		free(list);
		return r;
	}

	*download = list;
	return RET_OK;
}

/* free all memory, cancel all queued downloads */
retvalue downloadlist_free(struct downloadlist *list) {
	struct download_upstream *upstream;
	struct downloaditem *item;
	retvalue r;

	while( list->upstreams ) {
		upstream = list->upstreams;
		list->upstreams = upstream->next;
		free(upstream->method);
		free(upstream->config);
		while( upstream->items ) {
			item = upstream->items;
			upstream->items = item->nextinupstream;
			free(item->filekey);
			free(item->origfile);
			free(item->md5sum);
			free(item);
		}
		free(upstream);
	}
	r = files_done(list->filesdb);
	free(list);
	return r;
}

/* try to fetch and register all queued files */
retvalue downloadlist_run(struct downloadlist *list,const char *methoddir) {
	struct aptmethodrun *run;
	struct download_upstream *upstream;
	retvalue r;

	r = aptmethod_initialize_run(&run);
	if( RET_WAS_ERROR(r) )
		return r;
	for( upstream = list->upstreams ;upstream; upstream = upstream->next) {
		struct aptmethod *method;
		struct downloaditem *item;

		r = aptmethod_newmethod(run,upstream->method,upstream->config,&method);
		if( RET_WAS_ERROR(r) ) {
			aptmethod_shutdown(run);
			return r;
		}
		for( item=upstream->items;item;item=item->nextinupstream) {
			char *fullfilename;
			fullfilename = calc_dirconcat(list->filesdb->mirrordir,item->filekey);
			if( fullfilename == NULL ) {
				aptmethod_shutdown(run);
				return r;
			}
			(void)dirs_make_parent(fullfilename);
			r = aptmethod_queuefile(method,item->origfile,
					fullfilename,item->md5sum,item->filekey);
			free(fullfilename);
			if( RET_WAS_ERROR(r) ) {
				aptmethod_shutdown(run);
				return r;
			}
		}
	}
	r = aptmethod_download(run,methoddir,list->filesdb);
	aptmethod_shutdown(run);
	return r;
}

retvalue downloadlist_newupstream(struct downloadlist *list,
		const char *method,const char *config,struct download_upstream **upstream) {
	struct download_upstream *u;

	assert(list && method && upstream);

	u = calloc(1,sizeof(struct download_upstream));
	if( u == NULL )
		return RET_ERROR_OOM;
	if( config != NULL ) {
		u->config = strdup(config);
		if( u->config == NULL ) {
			free(u);
			return RET_ERROR_OOM;
		}
	} else
		u->config = NULL;
	u->method = strdup(method);
	if( u->method == NULL ) {
		free(u->config);
		free(u);
		return RET_ERROR_OOM;
	}
	u->list = list;
	u->next = list->upstreams;
	list->upstreams = u;
	*upstream = u;
	return RET_OK;
}

const struct downloaditem *searchforitem(const struct downloadlist *list,
					const char *filekey) {
	const struct downloaditem *item;

	//TODO: there is no doubt, so use better than brute force...
	for( item=list->items ; item ; item = item->next ) {
		if( strcmp(filekey,item->filekey) == 0 )
			return item;
	}
	return NULL;
}

/* queue a new file to be downloaded: 
 * results in RET_ERROR_WRONG_MD5, if someone else already asked
 * for the same destination with other md5sum created. */
retvalue downloadlist_add(struct download_upstream *upstream,const char *origfile,const char *filekey,const char *md5sum) {
	const struct downloaditem *i;
	struct downloaditem *item;
	retvalue r;

	r = files_check(upstream->list->filesdb,filekey,md5sum);
	if( r != RET_NOTHING )
		return r;

	i = searchforitem(upstream->list,filekey);
	if( i != NULL ) {
		if( strcmp(md5sum,i->md5sum) == 0 )
			return RET_NOTHING;
		// TODO: print error;
		return RET_ERROR_WRONG_MD5;
	}
	item = malloc(sizeof(struct downloaditem));
	if( item == NULL )
		return RET_ERROR_OOM;
	item->filekey = strdup(filekey);
	item->origfile = strdup(origfile);
	item->md5sum = strdup(md5sum);
	if( !item->filekey || !item->origfile || !item->md5sum ) {
		free(item->filekey);
		free(item->origfile);
		free(item);
		return RET_ERROR_OOM;
	}
	item->upstream = upstream;
	item->nextinupstream = upstream->items;
	upstream->items = item;
	item->next = upstream->list->items;
	upstream->list->items = item;
	return RET_OK;
}

retvalue downloadlist_addfiles(struct download_upstream *upstream,
		const struct strlist *origfiles,
		const struct strlist *filekeys,
		const struct strlist *md5sums) {
	retvalue result,r;
	int i;

	assert(origfiles && filekeys && md5sums
		&& origfiles->count == filekeys->count
		&& md5sums->count == filekeys->count);

	result = RET_NOTHING;
	
	for( i = 0 ; i < filekeys->count ; i++ ) {
		r = downloadlist_add(upstream,
			origfiles->values[i],
			filekeys->values[i],
			md5sums->values[i]);
		RET_UPDATE(result,r);
	}
	return result;
}