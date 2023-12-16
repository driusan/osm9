#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <json.h>
#include <stdio.h>

#include "readfile.h"
#include "proxyfd.h"

#include "osm.h"

typedef enum {
	SEARCH,
	QUERY
} FileType;

typedef struct {
	FileType file;
} FileAux;

typedef struct{
	char *server;
	char *q;
	vlong lastq;
} nclient;

nclient c = {
	.server = "nominatim.openstreetmap.org",
	.q = "",
};

JSON* webget(char *url) {
	int fd = open("/mnt/web/clone", ORDWR);
	char buf[512];
	int conn;
	JSON* json;
	read(fd, buf, 512);
	conn = strtol(buf, nil, 10);
	fprint(fd, "url %s\n", url);
	// fprint(2, "url %s\n", url);
	sprint(buf, "/mnt/web/%d/body", conn);
	int jfd = open(buf, OREAD);
	char* data = readfile(jfd);
	if (data == nil){
		fprint(2, "no data");
		close(jfd);
		close(fd);
		return nil;
	}
	close(jfd);
	close(fd);
	json = jsonparse(data);
	free(data);
	return json;
}
Lock queryl;
void search(Req *r) {

	char url[1024];
	if (c.q == nil || strcmp(c.q, "") == 0){
		respond(r, "no query");
		return;
	}
	if(r->fid->aux != nil){
		proxyfd(r);
		return;
	}
	sprint(url, "https://%s/search?format=jsonv2&q=%s", c.server, c.q);
	// fprint(2, url);
	// webget 
	lock(&queryl);
	if((nsec() - c.lastq) < 1000000000){
		// fprint(2, "sleeping");
		sleep(1000);
	};


	c.lastq = nsec();
	unlock(&queryl);
	JSON *j = webget(url);
	// fprint(2, "%J\n", j);
	char *file = tmpnam(nil);
	int fd = create(file, ORDWR|ORCLOSE, 0600);
	r->fid->aux = malloc(sizeof(int));
	*(int *)r->fid->aux = fd;
	for(JSONEl* el = j->first; el != nil; el = el->next) {
		JSON* v = jsonbyname(el->val, "lat");
		fprint(fd, "%s", v->s);
		v = jsonbyname(el->val, "lon");
		fprint(fd, " %s", v->s);
		v = jsonbyname(el->val, "addresstype");
		fprint(fd, "\t%s", v->s);
		v = jsonbyname(el->val, "osm_id");
		fprint(fd, " %lld", (long long )v->n);
		v = jsonbyname(el->val, "display_name");
		fprint(fd, " %s\n", v->s);
	}
	//fprint(fd, "%J", j);
	proxyfd(r);
	jsonfree(j);
}
Reqqueue *webserviceq;
void fsread(Req *r) {
	FileAux *f = r->fid->file->aux;
	switch(f->file){
	case QUERY:
		readstr(r, c.q);
		respond(r, nil);
		break;
	case SEARCH:
		reqqueuepush(webserviceq, r, search);
		break;
	default:
		respond(r, "not implemented");
	}
}

void fswrite(Req *r) {
	FileAux *f = r->fid->file->aux;
	switch(f->file){
	case QUERY:
		c.q = strdup(r->ifcall.data);
		c.q[r->ifcall.count] = 0;
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	default:
		respond(r, "bad write");
	}
}

Srv fs = {
	.read = fsread,
	.write = fswrite,
};

void poptree(void) {
	FileAux *faux = malloc(sizeof(FileAux));
	faux->file = SEARCH;
	createfile(fs.tree->root, "search", nil, 0666, faux);

	faux = malloc(sizeof(FileAux));
	faux->file = QUERY;
	createfile(fs.tree->root, "query", nil, 0666, faux);
}
void usage(void) {
	fprint(2, "usage: %s [-m mountpoint] [-S srvname]\n", argv0);
	exits("usage");
}


void threadmain(int argc, char *argv[]) {
	fs.tree = alloctree(nil, nil, DMDIR|777, nil);
	char *srvname = nil;
	int mountflags = 0;
	char *mtpt = "/mnt/osmname";
	JSONfmtinstall(); // sometimes used for debugging

	int fd = open("/mnt/web/ctl", OWRITE);
	fprint(fd, "useragent OSMFS (https://github.com/driusan/osm9)");
	close(fd);
	ARGBEGIN{
	case 'S':
		srvname = EARGF(usage());
		mtpt = nil;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	}ARGEND;
	
	if(access("/mnt/web/ctl", AEXIST) != 0) {
		sysfatal("missing webfs");
	}
	poptree();
	if (mountflags == 0) {
		mountflags = MREPL;
	}
	webserviceq = reqqueuecreate();
	c.lastq = 0;
	threadpostmountsrv(&fs, srvname, mtpt, mountflags);
	exits("");
}
