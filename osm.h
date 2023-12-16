typedef int zoom;
typedef struct {
	double lat;
	double lng;
} latlong;

typedef struct {
	long x;
	long y;
} tilepos;

typedef struct {
	char *server;
	int zoom;
	latlong world;
} client;

tilepos clienttile(client *c);

latlong tile2world(tilepos tp, int zoom);

latlong tilecenter2world(tilepos tp, int zoom);
