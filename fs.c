#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <stdio.h>

#include "osm.h"

typedef enum {
	CTL,
	LATLONG,
	XPOS,
	YPOS,
	ZOOM,
	URL,
} FileType;

typedef struct {
	FileType file;
} FileAux;

client c = {
	.server = "tile.openstreetmap.org",
	.zoom = 10,
	.world = {
		// LaFontaine Park
		.lat = 45.5273219,
		.lng = -73.5703556
	},
};

void fsread(Req *r) {
	FileAux *f = r->fid->file->aux;
	int count;
	tilepos tp;
	switch(f->file){
	case CTL:
		respond(r, "write only");
		return;
	case ZOOM:
		count = sprint(r->ofcall.data, "%d\n", c.zoom);
		if (r->ifcall.offset > 0) {
			r->ofcall.count = 0;
			respond(r, nil);
			return;
		}
		if (count < 0) {
			r->ofcall.count = 0;
			respond(r, "bad zoom");
			return;
		}
		r->ofcall.count = (unsigned int) count;
		respond(r, nil);
		return;
	case LATLONG:
		count = sprint(r->ofcall.data, "%f %f\n", c.world.lat, c.world.lng);
		if (r->ifcall.offset > 0) {
			r->ofcall.count = 0;
			respond(r, nil);
			return;
		}
		if (count < 0) {
			r->ofcall.count = 0;
			respond(r, "bad coordinates");
			return;
		}
		r->ofcall.count = (unsigned int) count;
		respond(r, nil);
		return;
	case XPOS:
		tp = clienttile(&c);
		if (tp.x < 0) {
			respond(r, "bad coordinates");
			return;
		}
		count = sprint(r->ofcall.data, "%ld\n", tp.x);
		if (r->ifcall.offset > 0) {
			r->ofcall.count = 0;
			respond(r, nil);
			return;
		}
		if (count < 0) {
			r->ofcall.count = 0;
			respond(r, "bad coordinates");
			return;
		}
		r->ofcall.count = (unsigned int) count;
		respond(r, nil);
		return;
	case YPOS:
		tp = clienttile(&c);
		if (tp.x < 0) {
			respond(r, "bad coordinates");
			return;
		}
		count = sprint(r->ofcall.data, "%ld\n", tp.y);
		if (r->ifcall.offset > 0) {
			r->ofcall.count = 0;
			respond(r, nil);
			return;
		}
		if (count < 0) {
			r->ofcall.count = 0;
			respond(r, "bad coordinates");
			return;
		}
		r->ofcall.count = (unsigned int) count;
		respond(r, nil);
		return;
	case URL:
		tp = clienttile(&c);
		count = sprint(r->ofcall.data, "https://%s/%d/%ld/%ld.png\n", c.server, c.zoom, tp.x, tp.y);
		if (r->ifcall.offset > 0) {
			r->ofcall.count = 0;
			respond(r, nil);
			return;
		}
		if (count < 0) {
			r->ofcall.count = 0;
			respond(r, "bad coordinates");
			return;
		}
		r->ofcall.count = (unsigned int) count;
		respond(r, nil);
		return;
	}
	respond(r, "not implemented");
}

void ctl(Req *r);

void latlongreq(Req *r){
	double lat, lng;
	r->ofcall.count = 0;
	if (sscanf(r->ifcall.data, "%lf %lf", &lat, &lng) != 2){
		respond(r, "bad latlong");
		return;
	}
	c.world.lat = lat;
	c.world.lng = lng;
	respond(r, nil);
}
void fswrite(Req *r) {
	FileAux *f = r->fid->file->aux;

	switch(f->file){
	case CTL:
		ctl(r);
		return;
	case LATLONG:
		latlongreq(r);
		return;
	default:
		respond(r, "write prohibited");
		return;
	}
}

void ctl(Req *r){
	tilepos tp;
	latlong world;
	if (strncmp(r->ifcall.data, "zoom ", 5) == 0) {
		int z = strtol(&r->ifcall.data[5], nil, 10);
		if (z == 0) {
			r->ofcall.count = 0;
			respond(r, "bad zoom");
			return;
		}
		c.zoom = z;
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	} else if (strncmp(r->ifcall.data, "server ", 7) == 0) {
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
	} else if (strncmp(r->ifcall.data, "lat ", 4) == 0) {
		double l = strtod(&r->ifcall.data[4], nil);
		if (l == 0) {
			r->ofcall.count = 0;
			respond(r, "bad latitude");
			return;
		}
		c.world.lat = l;
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	} else if (strncmp(r->ifcall.data, "long ", 4) == 0) {
		double l = strtod(&r->ifcall.data[4], nil);
		if (l == 0) {
			r->ofcall.count = 0;
			respond(r, "bad longitude");
			return;
		}
		c.world.lng = l;
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	} else if (strncmp(r->ifcall.data, "x ", 2) == 0) {
		tp = clienttile(&c);
		long x = strtol(&r->ifcall.data[2], nil, 10);
		if (x <= 0) {
			r->ofcall.count = 0;
			respond(r, "bad x tile");
			return;
		}
		tp.x = x;
		world = tilecenter2world(tp, c.zoom);
		c.world.lng = world.lng;
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	} else if (strncmp(r->ifcall.data, "y ", 2) == 0) {
		tp = clienttile(&c);
		long y = strtol(&r->ifcall.data[2], nil, 10);
		if (y <= 0) {
			r->ofcall.count = 0;
			respond(r, "bad y tile");
			return;
		}
		tp.y = y;
		world = tilecenter2world(tp, c.zoom);
		c.world.lat = world.lat;
		tp = clienttile(&c);
		assert(tp.y == y);
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
	faux->file = LATLONG;
	createfile(fs.tree->root, "latlong", nil, 0666, faux);

	faux = malloc(sizeof(FileAux));
	faux->file = XPOS;
	createfile(fs.tree->root, "x", nil, 0444, faux);

	faux = malloc(sizeof(FileAux));
	faux->file = YPOS;
	createfile(fs.tree->root, "y", nil, 0444, faux);

	faux = malloc(sizeof(FileAux));
	faux->file = ZOOM;
	createfile(fs.tree->root, "zoom", nil, 0444, faux);

	faux = malloc(sizeof(FileAux));
	faux->file = URL;
	createfile(fs.tree->root, "url", nil, 0444, faux);
}
void usage(void) {
	fprint(2, "usage: %s [-m mountpoint] [-S srvname]\n", argv0);
	exits("usage");
}


void main(int argc, char *argv[]) {
	fs.tree = alloctree(nil, nil, DMDIR|777, nil);
	char *srvname = nil;
	int mountflags = 0;
	char *mtpt = "/mnt/osm";

	ARGBEGIN{
	case 'S':
		srvname = EARGF(usage());
		mtpt = nil;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	}ARGEND;
	
	poptree();
	if (mountflags == 0) {
		mountflags = MREPL;
	}
	postmountsrv(&fs, srvname, mtpt, mountflags);
	exits("");
}
