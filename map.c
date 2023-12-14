#include <u.h>
#include <libc.h>
#include <draw.h>
#include <keyboard.h>
#include <event.h>

#include "osm.h"

Image *ctext;
client c = {
	.server = "tile.openstreetmap.org",
	.zoom = 10,
	.world = {
		.lat = 45.5273219,
		.lng = -73.5703556,
	}
};

void cachetile(long x, long y, int zoom);

Image* getimage(tilepos tp, int zoom) {
	cachetile(tp.x, tp.y, zoom);
	Dir *d;
	char *filename = smprint("%s/lib/osm/cache/%d/%ld/%ld.png", getenv("home"), zoom, tp.x, tp.y);
	int fd[2];
	if (pipe(fd) < 0) {
		sysfatal("pipe");
	}
	int pid = rfork(RFPROC|RFFDG);
	if (pid == 0) {
		dup(fd[1], 1);
		close(fd[0]);
		close(fd[1]);
		d = dirstat(filename);
		assert(d != nil);
		free(d);
		char *cmd = smprint("png -c %s", filename);
		// fprint(2, "cmd: %s\n", cmd);
		execl("/bin/rc", "rc", "-c", cmd, nil);
		exits("");
	} else if (pid < 0) {
		sysfatal("fork");
	} else {
		close(fd[1]);
		Image* i = readimage(display, fd[0], 0);
		close(fd[0]);
		assert(waitpid() > 0);
		assert(i);
		return i;
	}
	sysfatal("unreachable");
	return nil;
}

void cachetile(long x, long y, int zoom){
	char *filename = smprint("%s/lib/osm/cache/%d/%ld/%ld.png", getenv("home"), zoom, x, y);
	Dir *d;
	int pid;
	d = dirstat(filename);
	if (d == nil) {
		//fprint(2, "No %s\n", filename);
		pid = rfork(RFPROC|RFFDG);
		if (pid == 0) {
			int wfd = open("/mnt/osm/ctl", OWRITE);
			fprint(wfd, "zoom %d\n", zoom);
			fprint(wfd, "x %ld\n", x);
			fprint(wfd, "y %ld\n", y);
			close(wfd);
		//	fprint(2, "osm/get\n");
			execl("/bin/rc", "rc" "-c", "osm/get", nil);
		} else if (pid < 0) {
			sysfatal("fork");
		} else {
			assert(waitpid() > 0);

		}
	} else {
		// fprint(2, "Found %s\n", filename);
		free(d);
	}
	free(filename);
	
}

void drawrow(Image *screen, Rectangle drawpos, tilepos center, int screenwidth, int tilesz, int zoom) {
	tilepos drawtile;
	Image *src;

	int ntiles = screenwidth / tilesz;

	Rectangle origin;

	origin = drawpos;
	// origin
	src = getimage(center, c.zoom);
	drawtile = center;
	draw(screen, drawpos, src, nil, src->r.min);
	for(int x = 0; x <= (int )ceil((double )ntiles / 2.0); x++) {
		// left
		drawpos = origin;
		drawpos.min.x = origin.min.x - (tilesz*x);
		drawpos.max.x = origin.max.x - (tilesz*x);
		drawtile.x = center.x - x;

		src = getimage(drawtile, zoom);
		draw(screen, drawpos, src, nil, src->r.min);
		freeimage(src);
		// right
		drawpos = origin;
		drawpos.min.x = origin.min.x + (tilesz*x);
		drawpos.max.x = origin.max.x + (tilesz*x);
		drawtile.x = center.x + x;
		src = getimage(drawtile, zoom);
		draw(screen, drawpos, src, nil, src->r.min);
		freeimage(src);
	}
}
void redraw(Image *screen){
	Rectangle centertile = screen->r;
	int zoom = c.zoom;
	tilepos center = clienttile(&c);
	int width = screen->r.max.x - screen->r.min.x;
	int tilesz = 256;
	int height = screen->r.max.y - screen->r.min.y;
	//	int tileszy = 256; // src->r.max.x - src->r.min.x;
	int ntiles = width / tilesz;	

	// FIXME: Handle fractional tiles if lat/long isn't top left of tile
	centertile.min.x += (width / 2) - (tilesz / 2);
	centertile.min.y += (height / 2) - (tilesz / 2);
	for(int i = 0; i <= ntiles / 2; i++) {
		Rectangle drawpos = centertile;
		// up
		tilepos row = center;
		row.y = center.y - i;
		drawpos = centertile;
		drawpos.min.y = centertile.min.y - (i*tilesz);
		drawpos.max.y = centertile.max.y - (i*tilesz);

		drawrow(screen, drawpos, row, width, tilesz, zoom);

		// down
		row = center;
		row.y = center.y + i;
		drawpos = centertile;
		drawpos.min.y = centertile.min.y + (i*tilesz);
		drawpos.max.y = centertile.max.y + (i*tilesz);

		drawrow(screen, drawpos, row, width, tilesz, zoom);
	}

	Font *f = display->defaultfont;
	Point fsz = stringsize(f, "Map data from OpenStreetMap");
	Point corig = { screen->r.max.x - fsz.x, screen->r.max.y - (fsz.y*2)};
	string(screen, corig, ctext, ZP, f, "Map data from OpenStreetMap");
	fsz = stringsize(f, "openstreetmap.org/copyright");
	corig = (Point ){ screen->r.max.x - fsz.x, screen->r.max.y - (fsz.y)};
	string(screen, corig, ctext, ZP, f, "openstreetmap.org/copyright");
	flushimage(display, 1);
}

void eresized(int new) {
	if (new && getwindow(display, Refnone) < 0) {
		fprint(2, "can't reattach to window");
	}
	redraw(screen);
}
void main(void) {
	Event e;
	ulong evt;
	if (initdraw(0, 0, "OSM Map") == 0) {
		sysfatal("initdraw");
	}
	ctext = allocimage(display, Rect(0, 0, 1, 1), RGB24, 1, DBlack);
	einit(Emouse|Ekeyboard);
	redraw(screen);
	for(;;) {
		evt = eread(Emouse|Ekeyboard, &e);
		switch(evt) {
		case Emouse:
			break;
		case Ekeyboard:
			switch(e.kbdc){
			case Kdel:
			case 'q':
				exits("");
			case '+':
				c.zoom++;
				redraw(screen);
				break;
			case '-':
				c.zoom--;
				redraw(screen);
				break;
			}
			break;
		}
	}
}
