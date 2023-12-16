#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

void proxyfd(Req *r) {
	if (r->fid->aux == nil) {
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	int fd = (int ) *((int *)r->fid->aux);
	int n = pread(fd, r->ofcall.data, r->ifcall.count, r->ifcall.offset);
	r->ofcall.count = n;
	if (n == 0) {
		free(r->fid->aux);
		r->fid->aux = nil;
		close(fd);
	}
	if (n >= 0) {
		respond(r, nil);
	} else {
		respond(r, "read error");
	}	
}
