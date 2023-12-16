#include <u.h>
#include <libc.h>
#include <draw.h>
#include <keyboard.h>
#include <event.h>

#include "osm.h"

int debug = 0;

#define MAXZOOM 17

const char* menuitems[] = {
	"Exit",
	nil,
};
const Menu menu = {
	.item = menuitems,
};
Image *ctext;
client c = {
	.server = "tile.openstreetmap.org",
	.zoom = 10,
	.world = {
		.lat = 45.5273219,
		.lng = -73.5703556,
	}
};

#define MAXMEMCACHE 4096

typedef struct {
	tilepos pos;
	Image *i;
} MemImageCache;
void cachetile(long x, long y, int zoom);

double degperpixel(int zoom) {
	int ntiles = 1<<zoom;
	return (360.0 / (double )ntiles) / 256.0;
}

static MemImageCache* imagecache;

Image* getimage(tilepos tp, int zoom) {
	assert(imagecache != nil);
	int ntiles = 1<<zoom;
	MemImageCache cached = imagecache[(tp.x*ntiles + tp.y) % (MAXMEMCACHE*MAXMEMCACHE)];
	
	if (cached.pos.x == tp.x && cached.pos.y == tp.y && cached.i != nil) {
		if (debug) fprint(2, "Using cache\n");
		return cached.i;
	}
	if (debug) fprint(2, "Memcache miss: (%ld, %ld)=(%ld, %ld)? i=%p\n", cached.pos.x, cached.pos.y, tp.x, tp.y, cached.i);
	cachetile(tp.x, tp.y, zoom);
	char *filename = smprint("%s/lib/osm/cache/%d/%ld/%ld.png", getenv("home"), zoom, tp.x, tp.y);
	Dir *d = dirstat(filename);
	if (d == nil) {
		return nil;
	}
	free(d);
	int fd[2];
	if (pipe(fd) < 0) {
		sysfatal("pipe");
	}
	int pid = rfork(RFPROC|RFFDG);
	if (pid == 0) {
		dup(fd[1], 1);
		close(fd[0]);
		close(fd[1]);

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
		assert(i != nil);
		imagecache[(tp.x*ntiles + tp.y) % (MAXMEMCACHE*MAXMEMCACHE)].pos = tp;	
		imagecache[(tp.x*ntiles + tp.y) % (MAXMEMCACHE*MAXMEMCACHE)].i = i;
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
		if (debug) fprint(2, "Cache miss: %s\n", filename);

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
			Waitmsg* w = wait();
			assert(w != nil);
			if(debug)
				fprint(2, "Got %s: %s\n", filename, w->msg);

		}
	} else {
		// fprint(2, "Found %s\n", filename);
		free(d);
	}
	free(filename);
	
}

void drawcopyright(Image *screen){
	Font *f = display->defaultfont;
	Point corig;

	Point fsz = stringsize(f, "Map data from OpenStreetMap");
	corig = (Point ){ screen->r.max.x - fsz.x, screen->r.max.y - (fsz.y*2)};
	string(screen, corig, ctext, ZP, f, "Map data from OpenStreetMap");
	fsz = stringsize(f, "openstreetmap.org/copyright");
	corig = (Point ){ screen->r.max.x - fsz.x, screen->r.max.y - (fsz.y)};
	string(screen, corig, ctext, ZP, f, "openstreetmap.org/copyright");

}

void drawcoords(Image *screen){
	Font *f = display->defaultfont;
	char buf[96];
	snprint(buf, 96,
		"%f°%c %f°%c", 
		fabs(c.world.lat), (c.world.lat >= 0 ? 'N' : 'S'),
		fabs(c.world.lng), (c.world.lng >= 0 ? 'E' : 'W')
	);
	// fprint(2, "%s\n", buf);
	Point fsz = stringsize(f, buf);
	Point orig = (Point ){ screen->r.max.x - fsz.x - 10,screen->r.min.y + 5};
	string(screen, orig, ctext, ZP, f, buf);
}
void drawrow(Image *screen, Rectangle drawpos, tilepos center, int screenwidth, int tilesz, int zoom) {
	tilepos drawtile;
	Image *src;

	int ntiles = screenwidth / tilesz;

	Rectangle origin;

	origin = drawpos;
	// origin
	src = getimage(center, zoom);
	drawtile = center;
	if (src != nil)	draw(screen, drawpos, src, nil, src->r.min);
	for(int x = 0; x <= (int )ceil((double )ntiles / 2.0)+1; x++) {
		// left
		drawpos = origin;
		drawpos.min.x = origin.min.x - (tilesz*x);
		drawpos.max.x = origin.max.x - (tilesz*x);
		drawtile.x = center.x - x;

		src = getimage(drawtile, zoom);
		if (src != nil) draw(screen, drawpos, src, nil, src->r.min);
		// freeimage(src);
		// right
		drawpos = origin;
		drawpos.min.x = origin.min.x + (tilesz*x);
		drawpos.max.x = origin.max.x + (tilesz*x);
		drawtile.x = center.x + x;
		src = getimage(drawtile, zoom);
		if (src != nil) draw(screen, drawpos, src, nil, src->r.min);
		// freeimage(src);
	}
}
void redraw(Image *screen){
	Rectangle centertile = screen->r;

	int zoom = c.zoom;
	tilepos center = clienttile(&c);
	latlong origintilepos = tile2world(center, zoom);
	int width = screen->r.max.x - screen->r.min.x;
	int tilesz = 256;
	int height = screen->r.max.y - screen->r.min.y;
	//	int tileszy = 256; // src->r.max.x - src->r.min.x;
	int ntiles = width / tilesz;	

	latlong diff = {c.world.lat - origintilepos.lat, c.world.lng-origintilepos.lng};
	double ppd = 1.0 / degperpixel(zoom);
	// FIXME: Handle fractional tiles if lat/long isn't top left of tile
	centertile.min.x += (width / 2) - (tilesz / 2);
	centertile.min.y += (height / 2) - (tilesz / 2);

	centertile.min.x -= (diff.lng*ppd) / 2.0;
	centertile.min.y += (diff.lat*ppd) / 2.0;

	centertile.max.x = centertile.min.x + tilesz;
	centertile.max.y = centertile.min.y + tilesz;

	for(int i = 0; i <= (ntiles / 2)+1; i++) {
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


	/*
	corig = centertile.min;
	string(screen, corig, ctext, ZP, f, "+");

	string(screen, centertile.min, ctext, ZP, f, "+");
	string(screen, centertile.max, ctext, ZP, f, "+");
	*/

	drawcopyright(screen);
	drawcoords(screen);
//	flushimage(display, 1);
}

void eresized(int new) {
	if (new && getwindow(display, Refnone) < 0) {
		fprint(2, "can't reattach to window");
	}

	redraw(screen);
}

void changezoom(int newzoom){
	if (newzoom > MAXZOOM || newzoom <= 1) {
		return;
	}
	int ntiles = 1<<c.zoom;
	if (ntiles > MAXMEMCACHE) {
		ntiles = MAXMEMCACHE;
	}
	if (imagecache != nil) {
		for(int i = 0; i < ntiles; i++) {
			for(int j = 0; j < ntiles; j++) {
				freeimage(imagecache[i*ntiles + j].i);
			}
		}
		free(imagecache);
	}
	ntiles = 1<<newzoom;
	if (ntiles > MAXMEMCACHE) {
		ntiles = MAXMEMCACHE;
	}
	imagecache =calloc(ntiles*ntiles, sizeof(MemImageCache));
	// memset(imagecache, ntiles*ntiles, sizeof(Image*));
	c.zoom = newzoom;
}
void main(void) {
	Event e;
	ulong evt;
	Mouse lastmouse;
	Dir *d = dirstat("/mnt/osm/latlong");
	if (d == nil) {
		sysfatal("osm/fs not running");
	}
	free(d);
	changezoom(c.zoom);

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
			//fprint(2, "%d %P\n\n", e.mouse.buttons, e.mouse.xy);
			if (e.mouse.buttons == 4) {
				int i = emenuhit(3, &e.mouse, &menu);
				if (i >= 0){
					if (strcmp(menu.item[i], "Exit") == 0){
						exits("");
					}
				}
			} else if ((e.mouse.buttons & 1) && (lastmouse.buttons & 1)){
				int dx = e.mouse.xy.x - lastmouse.xy.x;
				int dy = e.mouse.xy.y - lastmouse.xy.y;
				c.world.lng -= degperpixel(c.zoom) * dx;
				c.world.lat += degperpixel(c.zoom) * dy;
				redraw(screen);
			}
			lastmouse = e.mouse;
			break;
		case Ekeyboard:
			switch(e.kbdc){
			case Kdel:
			case 'q':
				exits("");
			case 'r':
				redraw(screen);
				break;
			case '+':
				if (debug) fprint(2, "New zoom: %d\n", c.zoom+1);
				changezoom(c.zoom+1);
				redraw(screen);
				break;
			case '-':
				changezoom(c.zoom-1);
				redraw(screen);
				break;
			case Kdown:
				c.world.lat -= degperpixel(c.zoom) * 100;
				redraw(screen);
				break;
			case Kup:
				c.world.lat += degperpixel(c.zoom) * 100;
				redraw(screen);
				break;
			case Kleft: 
				c.world.lng -= degperpixel(c.zoom) * 100;
				redraw(screen);
				break;
			case Kright: 
				c.world.lng += degperpixel(c.zoom) * 100;
				redraw(screen);
				break;
			}
			break;
		}
	}
}
