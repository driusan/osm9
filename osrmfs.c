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
	CTL,
	PROFILE,
	SRC,
	DST,

	NEAREST,
	ROUTE,
} FileType;

typedef struct {
	FileType file;
} FileAux;

typedef struct{
	char *server;
	char *version;
	char *profile;

	latlong src, dst;
} osrmclient;

osrmclient c = {
	.server = "router.project-osrm.org",
	.version = "v1",
	.profile = "driving",
	// LaFontaine Park
	.src = {
		.lat = 45.5273219,
		.lng = -73.5703556,
	},
	// Beaver Lake
	.dst = {
		.lat = 45.4987479,
		.lng = -73.5987143,

	}
};

Reqqueue* webserviceq;

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
//	fprint(2, "%s", buf);
	close(jfd);
	close(fd);
	json = jsonparse(data);
	free(data);
	if (json == nil) {
		return nil;
	}
	JSON*code = jsonbyname(json, "code");
	if (code == nil) {
		jsonfree(json);
		return nil;
	}
	if (strcmp(code->s, "Ok") != 0) {
		jsonfree(json);
		return nil;
	}
	return json;
}
void latlongreq(Req *r, latlong *dst){
	double lat, lng;
	r->ofcall.count = 0;
	if (sscanf(r->ifcall.data, "%lf %lf", &lat, &lng) != 2){
		respond(r, "bad latlong");
		return;
	}
	dst->lat = lat;
	dst->lng = lng;
	respond(r, nil);
}
void nearest(Req *r) {
	char url[1024];
	sprint(url, "https://%s/nearest/%s/%s/%f,%f\n", c.server, c.version, c.profile, c.src.lng,c.src.lat);
	if (r->ifcall.offset > 0){
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	JSON* json = webget(url);
	if (json == nil){
		respond(r, "internal error");
		return;
	}
	JSON* waypoints = jsonbyname(json, "waypoints");
	if (waypoints == nil || waypoints->t != JSONArray){
		jsonfree(json);
		respond(r, "bad waypoints");
		return;
	}
	JSON* wayobj = waypoints->first->val;

	JSON* distance = jsonbyname(wayobj, "distance");
	JSON* location = jsonbyname(wayobj, "location");
	
	int count = sprint(r->ofcall.data, "%f %f\t%f\n",
		location->first->next->val->n,
		location->first->val->n,
		distance->n
	);
	if (count < 0) {
		jsonfree(json);
		respond(r, "internal error");
		return;
	}
	r->ofcall.count = (unsigned int) count;
	jsonfree(json);
	respond(r, nil);
}

typedef struct {
	char *s;
} ResponseCache;

void route(Req *r) {
	if (r->fid->aux != nil) {
		proxyfd(r);
		return;
	}
	char url[1024];
	sprint(url, "https://%s/route/%s/%s/%f,%f;%f,%f?steps=true\n", c.server, c.version, c.profile, c.src.lng,c.src.lat, c.dst.lng, c.dst.lat);
	if (r->ifcall.offset > 0){
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	JSON *j = webget(url);
	if (j == nil) {
		respond(r, "webget fail");
		return;
	}
	JSON* routes = jsonbyname(j, "routes");
	if (routes == nil) {
		jsonfree(j);
		respond(r, "no routes");
		return;
	}
	JSON* firstroute = routes->first->val;
	JSON* legs = jsonbyname(firstroute, "legs");
	if (legs == nil){
		jsonfree(j);
		respond(r, "no legs");
		return;
	}
	JSON *firstleg = legs->first->val;
	JSON *steps = jsonbyname(firstleg, "steps");
	if (steps == nil) {
		jsonfree(j);
		respond(r, "no steps");
		return;
	}
	char *file = tmpnam(nil);
	int fd = create(file, ORDWR|ORCLOSE, 0600);
	for(JSONEl* step = steps->first; step != nil; step = step->next){
		JSON* name = jsonbyname(step->val, "name");
		JSON* maneuver = jsonbyname(step->val, "maneuver");
		JSON* mantype = jsonbyname(maneuver, "type");
		JSON* mandir = jsonbyname(maneuver, "modifier");
		JSON* distance = jsonbyname(step->val, "distance");
		JSON* duration = jsonbyname(step->val, "duration");
		if (strcmp(mantype->s, "depart") == 0) {
			fprint(fd, "Depart");
			if (mandir != nil && strcmp(mandir->s, "") != 0) {
				fprint(fd, " heading %s", mandir->s);
			}
			if(name != nil && strcmp(name->s, "") != 0){
				fprint(fd, " along %s", name->s);
			} else if (distance != nil && distance->n > 0){
				fprint(fd, " and continue");
			}
			if (distance != nil && distance->n > 0){
				fprint(fd, " for %0.0fm", distance->n);
			}
			if (duration != nil && duration->n > 0){
				fprint(fd, " (approx %0.0fs)", duration->n);
			}
			fprint(fd, "\n");
		} else if (strcmp(mantype->s, "turn") == 0){
			fprint(fd, "Turn %s", mandir->s);
			if (name->s != nil && strcmp(name->s, "") != 0) {
				fprint(fd, " onto %s", name->s);
			}
			if (distance != nil && distance->n > 0){
				fprint(fd, " for %0.0fm", distance->n);
			}
			if (duration != nil && duration->n > 0){
				fprint(fd, " (approx %0.0fs)", duration->n);
			}
			fprint(fd,"\n");
		
		} else if (strcmp(mantype->s, "new name") == 0){
			fprint(fd, "Continue along %s", name->s);
			if (distance != nil && distance->n > 0){
				fprint(fd, " for %0.0fm", distance->n);
			}
			if (duration != nil && duration->n > 0){
				fprint(fd, " (approx %0.0fs)", duration->n);
			}
			fprint(fd, "\n");
		} else if (strcmp(mantype->s, "arrive") == 0){
			fprint(fd, "Arrive.");
			if (mandir != nil && strcmp(mandir->s, "") != 0) {
				fprint(fd, " Destination should be on your %s.", mandir->s);
			}
			fprint(fd, "\n");
		} else {
			fprint(fd, "Unknown maneuver type for %J\n", maneuver);
		}
		// fprint(fd, "along %s\n", name->s);
	}
	// fprint(fd, "Done!\n");
	r->fid->aux = malloc(sizeof(int));
	*(int *)(r->fid->aux) = fd;
	proxyfd(r);
	jsonfree(j);
}

void fsread(Req *r) {
	FileAux *f = r->fid->file->aux;
	int count;
	switch(f->file){
	case CTL:
		respond(r, "write only");
		return;
	case PROFILE:
		count = sprint(r->ofcall.data, "%s\n", c.profile);
		if (r->ifcall.offset > 0){
			r->ofcall.count = 0;
			respond(r, nil);
			return;
		}
		if (count < 0){
			respond(r, "bad profile");
			return;
		}
		r->ofcall.count = (unsigned int) count;
		respond(r, nil);
		return;
	case SRC:
		count = sprint(r->ofcall.data, "%f %f\n", c.src.lat, c.src.lng);
		if (r->ifcall.offset > 0){
			r->ofcall.count = 0;
			respond(r, nil);
			return;
		}
		if (count < 0){
			respond(r, "bad src");
			return;
		}
		r->ofcall.count = (unsigned int) count;
		respond(r, nil);
		return;
	case DST:
		count = sprint(r->ofcall.data, "%f %f\n", c.dst.lat, c.dst.lng);
		if (r->ifcall.offset > 0){
			r->ofcall.count = 0;
			respond(r, nil);
			return;
		}
		if (count < 0){
			respond(r, "bad src");
			return;
		}
		r->ofcall.count = (unsigned int) count;
		respond(r, nil);
		return;
	case NEAREST:
		reqqueuepush(webserviceq, r, nearest);
		return;
	case ROUTE:
		reqqueuepush(webserviceq, r, route);
		return;
	}
	respond(r, "not implemented");
}

void ctl(Req *r);

void fswrite(Req *r) {
	FileAux *f = r->fid->file->aux;
	switch(f->file){
	case CTL:
		ctl(r);
		return;
	case SRC:
		latlongreq(r, &c.src);
		return;
	case DST:
		latlongreq(r, &c.dst);
		return;
	default:
		respond(r, "write prohibited");
		return;
	}
}
void ctl(Req *r){
	if (strncmp(r->ifcall.data, "server ", 7) == 0) {
		c.server = strdup(&r->ifcall.data[7]);
		char *ch = c.server;
		while(*(ch++)) {
			if (*ch == '\n') {
				*ch = 0;
				break;
			}
		}
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	} else if (strncmp(r->ifcall.data, "profile ", 8) == 0) {
		c.profile = strdup(&r->ifcall.data[8]);
		c.profile[r->ifcall.count - 8] = 0;
		char *ch = c.profile;
		while(*(ch++)) {
			if (*ch == '\n') {
				*ch = 0;
				break;
			}
		}
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	respond(r, "bad ctl");
}

Srv fs = {
	.read = fsread,
	.write = fswrite,
};

void poptree(void) {
	FileAux *faux = malloc(sizeof(FileAux));
	faux->file = CTL;
	createfile(fs.tree->root, "ctl", nil, 0666, faux);

	faux = malloc(sizeof(FileAux));
	faux->file = PROFILE;
	createfile(fs.tree->root, "profile", nil, 0444, faux);

	faux = malloc(sizeof(FileAux));
	faux->file = SRC;
	createfile(fs.tree->root, "src", nil, 0666, faux);

	faux = malloc(sizeof(FileAux));
	faux->file = DST;
	createfile(fs.tree->root, "dst", nil, 0666, faux);

	faux = malloc(sizeof(FileAux));
	faux->file = NEAREST;
	createfile(fs.tree->root, "nearest", nil, 0444, faux);

	faux = malloc(sizeof(FileAux));
	faux->file = ROUTE;
	createfile(fs.tree->root, "route", nil, 0444, faux);

}
void usage(void) {
	fprint(2, "usage: %s [-m mountpoint] [-S srvname]\n", argv0);
	exits("usage");
}


void threadmain(int argc, char *argv[]) {
	fs.tree = alloctree(nil, nil, DMDIR|777, nil);
	char *srvname = nil;
	int mountflags = 0;
	char *mtpt = "/mnt/osrm";
	JSONfmtinstall(); // sometimes used for debugging

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
	threadpostmountsrv(&fs, srvname, mtpt, mountflags);
	exits("");
}
