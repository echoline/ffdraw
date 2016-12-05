#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include "draw.h"

#define _drawdebug 1

Display	*display;
Font	*font;
Image	*screen;
Screen	*_screen;
Point	ZP;

static int	screenid;

Screen*
allocscreen(Image *image, Image *fill, int public)
{
	uchar *a;
	Screen *s;
	int id, try;
	Display *d;

	d = image->display;
	if(d != fill->display){
		fprintf(stderr, "allocscreen: image and fill on different displays");
		return nil;
	}
	s = malloc(sizeof(Screen));
	if(s == nil)
		return nil;
	if(!screenid)
		screenid = getpid();
	for(try=0; try<25; try++){
		/* loop until find a free id */
		a = bufimage(d, 1+4+4+4+1);
		if(a == nil)
			break;
		id = ++screenid & 0xffff;	/* old devdraw bug */
		a[0] = 'A';
		BPLONG(a+1, id);
		BPLONG(a+5, image->id);
		BPLONG(a+9, fill->id);
		a[13] = public;
		if(flushimage(d, 0) != -1)
			goto Found;
	}
	free(s);
	return nil;

    Found:
	s->display = d;
	s->id = id;
	s->image = image;
	if(s->image != nil && s->image->chan != 0){
		s->fill = fill;
		return s;
	}
	exit(-1);
}

int
freescreen(Screen *s)
{
	uchar *a;
	Display *d;

	if(s == nil)
		return 0;
	d = s->display;
	a = bufimage(d, 1+4);
	if(a == nil){
Error:
		free(s);
		return -1;
	}
	a[0] = 'F';
	BPLONG(a+1, s->id);
	/*
	 * flush(1) because screen is likely holding last reference to
	 * window, and want it to disappear visually.
	 */
	if(flushimage(d, 1) < 0)
		goto Error;
	free(s);
	return 1;
}

Image*
_allocwindow(Image *i, Screen *s, Rectangle r, int ref, ulong col)
{
	Display *d;

	d = s->display;
	i = _allocimage(i, d, r, d->screenimage->chan, 0, col, s->id, ref);
	if(i == nil)
		return nil;
	i->screen = s;
	i->next = s->display->windows;
	s->display->windows = i;
	return i;
}

Image*
namedimage(Display *d, char *name)
{
	uchar *a;
	char *err, buf[12*12+1];
	Image *i;
	int id, n;
	ulong chan;

	err = nil;
	i = nil;

	n = strlen(name);
	if(n >= 256){
		err = "name too long";
    Error:
		if(err != nil)
			fprintf(stderr,"namedimage: %s", err);
		else
			fprintf(stderr,"namedimage: %s", strerror(errno));
		free(i);
		return nil;
	}
	/* flush pending data so we don't get error allocating the image */
	flushimage(d, 0);
	a = bufimage(d, 1+4+1+n);
	if(a == nil)
		goto Error;
	d->imageid++;
	id = d->imageid;
	a[0] = 'n';
	BPLONG(a+1, id);
	a[5] = n;
	memmove(a+6, name, n);
	if(flushimage(d, 0) < 0)
		goto Error;

	if(pread(d->ctlfd, buf, 12*12, 0) < 12*12)
		goto Error;
	buf[12*12] = '\0';

	i = malloc(sizeof(Image));
	if(i == nil){
	Error1:
		a = bufimage(d, 1+4);
		if(a != nil){
			a[0] = 'f';
			BPLONG(a+1, id);
			flushimage(d, 0);
		}
		goto Error;
	}
	i->display = d;
	i->id = id;
	if((chan=strtochan(buf+2*12))==0){
		fprintf(stderr, "bad channel '%.12s' from devdraw", buf+2*12);
		goto Error1;
	}
	i->chan = chan;
	i->depth = chantodepth(chan);
	i->repl = atoi(buf+3*12);
	i->r.min.x = atoi(buf+4*12);
	i->r.min.y = atoi(buf+5*12);
	i->r.max.x = atoi(buf+6*12);
	i->r.max.y = atoi(buf+7*12);
	i->clipr.min.x = atoi(buf+8*12);
	i->clipr.min.y = atoi(buf+9*12);
	i->clipr.max.x = atoi(buf+10*12);
	i->clipr.max.y = atoi(buf+11*12);
	i->screen = nil;
	i->next = nil;
	return i;
}

void
_setdrawop(Display *d, Drawop op)
{
	uchar *a;

	if(op != SoverD){
		a = bufimage(d, 1+1);
		if(a == nil)
			return;
		a[0] = 'O';
		a[1] = op;
	}
}
		
static void
draw1(Image *dst, Rectangle *r, Image *src, Point *p0, Image *mask, Point *p1, Drawop op)
{
	uchar *a;

	_setdrawop(dst->display, op);

	a = bufimage(dst->display, 1+4+4+4+4*4+2*4+2*4);
	if(a == nil)
		return;
	if(src == nil)
		src = dst->display->black;
	if(mask == nil)
		mask = dst->display->opaque;
	a[0] = 'd';
	BPLONG(a+1, dst->id);
	BPLONG(a+5, src->id);
	BPLONG(a+9, mask->id);
	BPLONG(a+13, r->min.x);
	BPLONG(a+17, r->min.y);
	BPLONG(a+21, r->max.x);
	BPLONG(a+25, r->max.y);
	BPLONG(a+29, p0->x);
	BPLONG(a+33, p0->y);
	BPLONG(a+37, p1->x);
	BPLONG(a+41, p1->y);

}

void
draw(Image *dst, Rectangle r, Image *src, Image *mask, Point p1)
{
	draw1(dst, &r, src, &p1, mask, &p1, SoverD);
}

int
_freeimage1(Image *i)
{
	uchar *a;
	Display *d;
	Image *w;

	if(i == nil || i->display == nil)
		return 0;
	d = i->display;
	if(i->screen != nil){
		w = d->windows;
		if(w == i)
			d->windows = i->next;
		else
			while(w != nil){
				if(w->next == i){
					w->next = i->next;
					break;
				}
				w = w->next;
			}
	}
	a = bufimage(d, 1+4);
	if(a == nil)
		return -1;
	a[0] = 'f';
	BPLONG(a+1, i->id);
	return 0;
}

int
freeimage(Image *i)
{
	int ret;

	ret = _freeimage1(i);
	free(i);
	return ret;
}

static void
_closedisplay(Display *disp, int isshutdown)
{
	int fd;
	char buf[128];

	if(disp == nil)
		return;
	if(disp == display)
		display = nil;
	if(disp->oldlabel[0]){
		snprintf(buf, sizeof buf, "%s/label", disp->windir);
		fd = open(buf, O_WRONLY);
		if(fd >= 0){
			write(fd, disp->oldlabel, strlen(disp->oldlabel));
			close(fd);
		}
	}

	/*
	 * if we're shutting down, don't free all the resources.
	 * if other procs are getting shot down by notes too,
	 * one might get shot down while holding the malloc lock.
	 * just let the kernel clean things up when we exit.
	 */
	if(isshutdown)
		return;

	free(disp->devdir);
	free(disp->windir);
	freeimage(disp->white);
	freeimage(disp->black);
	close(disp->fd);
	close(disp->ctlfd);
	/* should cause refresh slave to shut down */
	close(disp->reffd);
	/* qunlock(&disp->qlock); TODO */
	free(disp);
}

void
closedisplay(Display *disp)
{
	_closedisplay(disp, 0);
}

/* note handler */
static void
drawshutdown(void)
{
	Display *d;

	d = display;
	if(d != nil){
		display = nil;
		_closedisplay(d, 1);
	}
}

Point
Pt(int x, int y)
{
	Point p;

	p.x = x;
	p.y = y;
	return p;
}

Rectangle
Rect(int x, int y, int bx, int by)
{
	Rectangle r;

	r.min.x = x;
	r.min.y = y;
	r.max.x = bx;
	r.max.y = by;
	return r;
}

Rectangle
insetrect(Rectangle r, int n)
{
	r.min.x += n;
	r.min.y += n;
	r.max.x -= n;
	r.max.y -= n;
	return r;
}

int
badrect(Rectangle r)
{
	int x, y;
	uint z;

	x = Dx(r);
	y = Dy(r);
	if(x > 0 && y > 0){
		z = x*y;
		if(z/x == y && z < 0x10000000)
			return 0;
	}
	return 1;
}

static char channames[] = "rgbkamx";

int
chantodepth(ulong c)
{
	int n;

	for(n=0; c; c>>=8){
		if(TYPE(c) >= NChan || NBITS(c) > 8 || NBITS(c) <= 0)
			return 0;
		n += NBITS(c);
	}
	if(n==0 || (n>8 && n%8) || (n<8 && 8%n))
		return 0;
	return n;
}

ulong
strtochan(char *s)
{
	char *p, *q;
	ulong c;
	int t, n, d;

	c = 0;
	d = 0;
	p=s;
	while(*p && isspace(*p))
		p++;

	while(*p && !isspace(*p)){
		if((q = strchr(channames, p[0])) == nil) 
			return 0;
		t = q-channames;
		if(p[1] < '0' || p[1] > '9')
			return 0;
		n = p[1]-'0';
		d += n;
		c = (c<<8) | __DC(t, n);
		p += 2;
	}
	if(d==0 || (d>8 && d%8) || (d<8 && 8%d))
		return 0;
	return c;
}

static
int
doflush(Display *d)
{
	int n, nn;

	n = d->bufp-d->buf;
	if(n <= 0)
		return 1;

	if((nn=write(d->fd, d->buf, n)) != n){
		if(_drawdebug)
			fprintf(stderr, "flushimage fail: d=%p: n=%d nn=%d %r\n", d, n, nn); /**/
		d->bufp = d->buf;	/* might as well; chance of continuing */
		return -1;
	}
	d->bufp = d->buf;
	return 1;
}

int
flushimage(Display *d, int visible)
{
	if(d == nil)
		return 0;
	if(visible){
		*d->bufp++ = 'v';	/* five bytes always reserved for this */
		if(d->_isnewdisplay){
			BPLONG(d->bufp, d->screenimage->id);
			d->bufp += 4;
		}
	}
	return doflush(d);
}

uchar*
bufimage(Display *d, int n)
{
	uchar *p;

	if(n<0 || n>d->bufsize){
		fprintf(stderr, "bad count in bufimage");
		return nil;
	}
	if(d->bufp+n > d->buf+d->bufsize)
		if(doflush(d) < 0)
			return nil;
	p = d->bufp;
	d->bufp += n;
	return p;
}

Image*
_allocimage(Image *ai, Display *d, Rectangle r, ulong chan, int repl, ulong col, int screenid, int refresh)
{
	uchar *a;
	char *err;
	Image *i;
	Rectangle clipr;
	int id;
	int depth;

	err = nil;
	i = nil;

	if(badrect(r)){
		fprintf(stderr, "bad rectangle");
		return nil;
	}
	if(chan == 0){
		fprintf(stderr, "bad channel descriptor");
		return nil;
	}

	depth = chantodepth(chan);
	if(depth == 0){
		err = "bad channel descriptor";
    Error:
		if(err != nil)
			fprintf(stderr, "allocimage: %s", err);
		else
			fprintf(stderr, "allocimage: %s", strerror(errno));
		free(i);
		return nil;
	}

	a = bufimage(d, 1+4+4+1+4+1+4*4+4*4+4);
	if(a == nil)
		goto Error;
	d->imageid++;
	id = d->imageid;
	a[0] = 'b';
	BPLONG(a+1, id);
	BPLONG(a+5, screenid);
	a[9] = refresh;
	BPLONG(a+10, chan);
	a[14] = repl;
	BPLONG(a+15, r.min.x);
	BPLONG(a+19, r.min.y);
	BPLONG(a+23, r.max.x);
	BPLONG(a+27, r.max.y);
	if(repl)
		/* huge but not infinite, so various offsets will leave it huge, not overflow */
		clipr = Rect(-0x3FFFFFFF, -0x3FFFFFFF, 0x3FFFFFFF, 0x3FFFFFFF);
	else
		clipr = r;
	BPLONG(a+31, clipr.min.x);
	BPLONG(a+35, clipr.min.y);
	BPLONG(a+39, clipr.max.x);
	BPLONG(a+43, clipr.max.y);
	BPLONG(a+47, col);

	if(ai != nil)
		i = ai;
	else{
		i = malloc(sizeof(Image));
		if(i == nil){
			a = bufimage(d, 1+4);
			if(a != nil){
				a[0] = 'f';
				BPLONG(a+1, id);
				flushimage(d, 0);
			}
			goto Error;
		}
	}
	i->display = d;
	i->id = id;
	i->depth = depth;
	i->chan = chan;
	i->r = r;
	i->clipr = clipr;
	i->repl = repl;
	i->screen = nil;
	i->next = nil;
	return i;
}

Image*
allocimage(Display *d, Rectangle r, ulong chan, int repl, ulong col)
{
	Image *i;

	i = _allocimage(nil, d, r, chan, repl, col, 0, 0);
/*	if(i != nil) TODO
		setmalloctag(i, getcallerpc(&d)); */
	return i;
}

int
gengetwindow(Display *d, char *winname, Image **winp, Screen **scrp, int ref)
{
	int n, fd;
	char buf[64+1], obuf[64+1];
	Image *image;
	Rectangle r;

	obuf[0] = 0;
retry:
	fd = open(winname, OREAD);
	if(fd<0 || (n=read(fd, buf, sizeof buf-1))<=0){
		if((image=d->image) == nil){
			*winp = nil;
			d->screenimage = nil;
			return -1;
		}
		strcpy(buf, "noborder");
	}else{
		close(fd);
		buf[n] = '\0';
		image = namedimage(d, buf);
		if(image == nil){
			/*
			 * theres a race where the winname can change after
			 * we read it, so keep trying as long as the name
			 * keeps changing.
			 */
			if(strcmp(buf, obuf) != 0){
				strcpy(obuf, buf);
				goto retry;
			}
		}
		if(*winp != nil){
			_freeimage1(*winp);
			freeimage((*scrp)->image);
			freescreen(*scrp);
			*scrp = nil;
		}
		if(image == nil){
			*winp = nil;
			d->screenimage = nil;
			return -1;
		}
	}

	d->screenimage = image;
	*scrp = allocscreen(image, d->white, 0);
	if(*scrp == nil){
		*winp = nil;
		d->screenimage = nil;
		freeimage(image);
		return -1;
	}

	r = image->r;
	if(strncmp(buf, "noborder", 8) != 0)
		r = insetrect(image->r, Borderwidth);
	*winp = _allocwindow(*winp, *scrp, r, ref, DWhite);
	if(*winp == nil){
		freescreen(*scrp);
		*scrp = nil;
		d->screenimage = nil;
		freeimage(image);
		return -1;
	}
	d->screenimage = *winp;
	return 1;
}

static char deffontname[] = "*default*";

int
geninitdraw(char *devdir, void(*error)(Display*, char*), char *fontname, char *label, char *windir, int ref)
{
	char buf[128];

	display = initdisplay(devdir, windir, error);
	if(display == nil)
		return -1;

	snprintf(buf, sizeof buf, "%s/winname", display->windir);
	if(gengetwindow(display, buf, &screen, &_screen, ref) < 0){
		closedisplay(display);
		display = nil;
		return -1;
	}

	atexit(drawshutdown);

	return 1;
}

int
initdraw(void(*error)(Display*, char*), char *fontname, char *label)
{
	static char dev[] = "/dev";

	return geninitdraw(dev, error, fontname, label, dev, Refnone);
}

#define	NINFO	12*12

Display*
initdisplay(char *dev, char *win, void(*error)(Display*, char*))
{
	char buf[128], info[NINFO+1], *t, isnew;
	int n, datafd, ctlfd, reffd;
	Display *disp;
//	Dir *dir;
	Image *image;

	ZP = Pt(0, 0);

	if(dev == nil)
		dev = "/dev";
	if(win == nil)
		win = "/dev";
	if(strlen(dev)>sizeof buf-25 || strlen(win)>sizeof buf-25){
		fprintf(stderr, "initdisplay: directory name too long\n");
		return nil;
	}
	t = strdup(win);
	if(t == nil)
		return nil;

	sprintf(buf, "%s/draw/new", dev);
	ctlfd = open(buf, ORDWR|OCEXEC);
	if(ctlfd < 0){
    Error1:
		free(t);
		fprintf(stderr, "initdisplay: %s: %s\n", buf, strerror(errno));
		return nil;
	}
	/* initial draw read must be EXACTLY 12*12 bytes */
	if((n=read(ctlfd, info, NINFO)) < 0){
    Error2:
		close(ctlfd);
		goto Error1;
	}
	if(n==NINFO+1)
		n = NINFO;
	info[n] = '\0';
	isnew = 0;
	if(n < NINFO)	/* this will do for now, we need something better here */
		isnew = 1;
	sprintf(buf, "%s/draw/%d/data", dev, atoi(info+0*12));
	datafd = open(buf, ORDWR|OCEXEC);
	if(datafd < 0)
		goto Error2;
	sprintf(buf, "%s/draw/%d/refresh", dev, atoi(info+0*12));
	reffd = open(buf, OREAD|OCEXEC);
	if(reffd < 0){
    Error3:
		close(datafd);
		goto Error2;
	}
	disp = calloc(sizeof(Display), 1);
	if(disp == nil){
    Error4:
		close(reffd);
		goto Error3;
	}
	image = nil;
	if(0){
    Error5:
		free(image);
		free(disp);
		goto Error4;
	}
	if(n >= NINFO){
		image = calloc(sizeof(Image), 1);
		if(image == nil)
			goto Error5;
		image->display = disp;
		image->id = 0;
		image->chan = strtochan(info+2*12);
		image->depth = chantodepth(image->chan);
		image->repl = atoi(info+3*12);
		image->r.min.x = atoi(info+4*12);
		image->r.min.y = atoi(info+5*12);
		image->r.max.x = atoi(info+6*12);
		image->r.max.y = atoi(info+7*12);
		image->clipr.min.x = atoi(info+8*12);
		image->clipr.min.y = atoi(info+9*12);
		image->clipr.max.x = atoi(info+10*12);
		image->clipr.max.y = atoi(info+11*12);
	}

	disp->_isnewdisplay = isnew;
	disp->bufsize = 8000;
/* iounit(datafd);
	if(disp->bufsize <= 0)
		disp->bufsize = 8000;
	if(disp->bufsize < 512){
		werrstr("iounit %d too small", disp->bufsize);
		goto Error5;
	} */
	disp->buf = malloc(disp->bufsize+5);	/* +5 for flush message */
	if(disp->buf == nil)
		goto Error5;

	disp->image = image;
	disp->dirno = atoi(info+0*12);
	disp->fd = datafd;
	disp->ctlfd = ctlfd;
	disp->reffd = reffd;
	disp->bufp = disp->buf;
	disp->error = error;
	disp->windir = t;
	disp->devdir = strdup(dev);
	/* qlock(&disp->qlock); TODO */
	disp->white = allocimage(disp, Rect(0, 0, 1, 1), GREY1, 1, DWhite);
	disp->black = allocimage(disp, Rect(0, 0, 1, 1), GREY1, 1, DBlack);
	if(disp->white == nil || disp->black == nil){
		free(disp->devdir);
		free(disp->white);
		free(disp->black);
		goto Error5;
	}
	disp->opaque = disp->white;
	disp->transparent = disp->black;
/*	dir = dirfstat(ctlfd);
	if(dir!=nil && dir->type=='i'){
		disp->local = 1;
		disp->dataqid = dir->qid.path;
	}
	if(dir!=nil && dir->qid.vers==1)	/* other way to tell 
		disp->_isnewdisplay = 1; TODO
	free(dir); */

	return disp;
}
