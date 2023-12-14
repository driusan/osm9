#include <u.h>
#include <libc.h>
#include "osm.h"

double d2r(double deg) {
	return deg*PI / 180.0;
}

#define sec(x) (1.0 / cos(x))

tilepos clienttile(client *c) {
	tilepos tp;
	if (c == nil) { 
		tp.x = -1;
		tp.y = -1;
		return tp;
	}

	long tilesPerGlobe = 1<< c->zoom;
	double latr = d2r(c->world.lat);
	// pseudo-code from https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
	// xtile = n * ((lon_deg + 180) / 360)
	// ytile = n * (1 - (log(tan(lat_rad) + sec(lat_rad)) / π)) / 2
	// n =  tilesPerGlobe

	tp.x = (int )((double )tilesPerGlobe * (c->world.lng + 180.0) / 360.0);
	if (tp.x < 0) {
		tp.x += tilesPerGlobe;
	}

	tp.y = (int )floor(tilesPerGlobe * (1.0 - (log(tan(latr) + sec(latr)) / PI)) / 2.0);
	if (tp.y < 0) {
		tp.y += tilesPerGlobe;
	}
	return tp;
}

latlong tile2world(tilepos tp, int zoom) {
	// ported from https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames
	// n = 2 ^ zoom
	// lon_deg = xtile / n * 360.0 - 180.0
	// lat_rad = arctan(sinh(π * (1 - 2 * ytile / n)))
	// lat_deg = lat_rad * 180.0 / π

	latlong ll;
	double dx = (double )tp.x + 0.5;
	double dy = (double )tp.y + 0.5;
	long tilesPerGlobe = 1 << zoom;
	double n = (double)tilesPerGlobe;
	ll.lng = dx / n * 360.0 - 180.0;
	double lat_rad = atan(sinh(PI * (1.0 - (2.0 * dy) / n)));
	ll.lat = lat_rad * 180.0 / PI;
	return ll;
}
