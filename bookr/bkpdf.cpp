/*
 * Bookr: document reader for the Sony PSP
 * Copyright (C) 2005 Carlos Carrasco Martinez (carloscm at gmail dot com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "bkbookmark.h"

#ifdef MAC
static void* memalign(int t, int s) {
	return malloc(s);
}
#else
#include <malloc.h>
#endif

#include "bkpdf.h"

extern "C" {
#include "fitz.h"
#include "mupdf.h"
}

// singleton
static fz_renderer *fzrast = NULL;
static unsigned int* bounceBuffer = NULL;
static unsigned int* backBuffer = NULL;
static fz_pixmap* fullPageBuffer = NULL;

struct PDFContext {
	/* current document params */
	//string docTitle;
	pdf_xref *xref;
	pdf_outline *outline;
	pdf_pagetree *pages;

	/* current view params */
	/*float zoom;
	int rotate;
	fz_pixmap *image;*/

	/* current page params */
	int pageno;
	pdf_page *page;
	float zoom;
	int zoomLevel;
	float rotate;
};

static int pdfInit() {
	fz_error *error;
	if (fzrast == NULL) {
		error = fz_newrenderer(&fzrast, pdf_devicergb, 0, 1024 * 512);
		if (error)
			return -1;
	}
	if (bounceBuffer == NULL) {
		bounceBuffer = (unsigned int*)memalign(16, 480*272*4);
	}
	if (backBuffer == NULL) {
		backBuffer = (unsigned int*)memalign(16, 480*272*4);
	}
	return 0;
}

static PDFContext* pdfOpen(char *filename) {
	PDFContext* ctx = new PDFContext();
	memset(ctx, 0, sizeof(PDFContext));
	fz_error *error;
	fz_obj *obj;

	/*
	* Open PDF and load xref table
	*/
	error = pdf_newxref(&ctx->xref);
	if (error) {
		printf("err1: %s\n", error->msg);
		delete ctx;
		return 0;
	}

	error = pdf_loadxref(ctx->xref, filename);
	if (error) {
		printf("err2: %s\n", error->msg);
		if (!strncmp(error->msg, "ioerror", 7)) {
			delete ctx;
			return 0;
		}
		printf("There was a problem with file \"%s\".\n"
			"It may be corrupted, or generated by broken software.\n\n"
			"%s\n\nTrying to continue anyway...\n", filename, error->msg);
		error = pdf_repairxref(ctx->xref, filename);
		if (error) {
			printf("err3: %s\n", error->msg);
			delete ctx;
			return 0;
		}
	}

	/*
	* Handle encrypted PDF files
	*/

	error = pdf_decryptxref(ctx->xref);
	if (error) {
		printf("err4: %s\n", error->msg);
		delete ctx;
		return 0;
	}

	/*if (ctx->xref->crypt) {
		//printf("err5: %s\n", error->msg);
		printf("err5\n", error->msg);
		delete ctx;
		return 0;
	}*/

	if (ctx->xref->crypt) {
		error = pdf_setpassword(ctx->xref->crypt, "");
		if (error) {
			printf("err5: encrypted file (tried empty password): %s\n", error->msg);
			delete ctx;
			return 0;
			/*
			fz_droperror(error);
			password = winpassword(app, filename);
			if (!password)
				exit(1);
			error = pdf_setpassword(app->xref->crypt, password);
			if (error)
				pdfapp_warn(app, "Invalid password.");
			*/
		}
	}

	/*
	* Load page tree
	*/

	error = pdf_loadpagetree(&ctx->pages, ctx->xref);
	if (error) {
		printf("err6: %s\n", error->msg);
		delete ctx;
		return 0;
	}

	/*
	* Load meta information
	* TODO: move this into mupdf library
	*/
	obj = fz_dictgets(ctx->xref->trailer, "Root");
	if (!obj) {
		printf("err7: %s\n", error->msg);
		//pdfapp_error(app, fz_throw("syntaxerror: missing Root object"));
		delete ctx;
		return 0;
	}

	error = pdf_loadindirect(&ctx->xref->root, ctx->xref, obj);
	if (error) {
		printf("err8: %s\n", error->msg);
		delete ctx;
		return 0;
	}

	obj = fz_dictgets(ctx->xref->trailer, "Info");
	if (obj) {

		error = pdf_loadindirect(&ctx->xref->info, ctx->xref, obj);

		if (error) {
			printf("err10: %s\n", error->msg);
			delete ctx;
			return 0;
		}
	}

	error = pdf_loadnametrees(ctx->xref);
	// non-critical error, we can live without the outline
	if (error) {
		printf("warn11 - no outline: %s\n", error->msg);
	} else {
		error = pdf_loadoutline(&ctx->outline, ctx->xref);
		if (error) {
			printf("warn12 - no outline: %s\n", error->msg);
		}
	}

	/*
	ctx->doctitle = filename;
	if (strrchr(app->doctitle, '\\'))
		app->doctitle = strrchr(app->doctitle, '\\') + 1;
	if (strrchr(app->doctitle, '/'))
		app->doctitle = strrchr(app->doctitle, '/') + 1;
	if (app->xref->info) {
		obj = fz_dictgets(app->xref->info, "Title");
		if (obj) {
			error = pdf_toutf8(&app->doctitle, obj);
			if (error) {
				delete ctx;
				return 0;
			}
		}
	}
	*/
	/*
	* Start at first page
	*/
/*
	app->shrinkwrap = 1;
	if (app->pageno < 1)
		app->pageno = 1;
	if (app->zoom <= 0.0)
		app->zoom = 1.0;
	app->rotate = 0;
	app->panx = 0;
	app->pany = 0;

	pdfapp_showpage(app, 1, 1);
*/

	ctx->pageno = 1;
	ctx->zoom = 1.0f;
	ctx->zoomLevel = 4;
	return ctx;
}

static void pdfClose(PDFContext* ctx) {
	if (ctx->pages)
		pdf_droppagetree(ctx->pages);
	ctx->pages = 0;

	if (ctx->page)
		pdf_droppage(ctx->page);
	ctx->page = 0;

	/*if (ctx->image)
		fz_droppixmap(ctx->image);
	ctx->image = nil;*/

	if (ctx->outline)
		pdf_dropoutline(ctx->outline);
	ctx->outline = 0;

	if (ctx->xref->store)
		pdf_dropstore(ctx->xref->store);
	ctx->xref->store = 0;

	pdf_closexref(ctx->xref);
	ctx->xref = 0;
}

static fz_matrix pdfViewctm(PDFContext* ctx) {
	fz_matrix ctm;
	ctm = fz_identity();
	ctm = fz_concat(ctm, fz_translate(0, -ctx->page->mediabox.y1));
	ctm = fz_concat(ctm, fz_scale(ctx->zoom, -ctx->zoom));
	ctm = fz_concat(ctm, fz_rotate(ctx->rotate + ctx->page->rotate));
	return ctm;
}

static fz_pixmap* pdfRenderTile(PDFContext* ctx, int x, int y, int w, int h, bool transform = false) {
	fz_error *error;
	fz_matrix ctm;
	fz_rect bbox;

	bbox.x0 = x;
	bbox.y0 = y;
	bbox.x1 = x;
	bbox.y1 = y;
	ctm = pdfViewctm(ctx);
	if (transform) {
		bbox.x1 += w;
		bbox.y1 += h;
		bbox = fz_transformaabb(ctm, bbox);
	} else {
		bbox.x0 = x + ctx->page->mediabox.x0;
		bbox.y0 = y + ctx->page->mediabox.y0;
		bbox.x1 = bbox.x0 + w;
		bbox.y1 = bbox.y0 + h;
	}
	fz_irect ir = fz_roundrect(bbox);
	if (!transform) {
		ir.x1 = ir.x0 + w;
		ir.y1 = ir.y0 + h;
	}
	fz_pixmap* pix = (fz_pixmap*)malloc(sizeof(fz_pixmap));
	error = fz_rendertree(&pix, fzrast, ctx->page->tree, ctm, ir, 1);
	if (error) {
		return 0;
	}
	return pix;
}

static void pdfRenderFullPage(PDFContext* ctx) {
	if (fullPageBuffer != NULL) {
		fz_droppixmap(fullPageBuffer);
		fullPageBuffer = NULL;
	}
	float h = ctx->page->mediabox.y1 - ctx->page->mediabox.y0;
	float w = ctx->page->mediabox.x1 - ctx->page->mediabox.x0;
	fullPageBuffer = pdfRenderTile(ctx,
		(int)ctx->page->mediabox.x0,
		(int)ctx->page->mediabox.y0,
		(int)w, (int)h, true);
	// precalc color shift
	unsigned int* s = (unsigned int*)fullPageBuffer->samples;
	unsigned int n = fullPageBuffer->w * fullPageBuffer->h;
	for (unsigned int i = 0; i < n; ++i) {
		*s >>= 8;
		++s;
	}
}

static char lastPageError[1024];
static int pdfLoadPage(PDFContext* ctx) {
	if (fullPageBuffer != NULL) {
		fz_droppixmap(fullPageBuffer);
		fullPageBuffer = NULL;
	}
	strcpy(lastPageError, "No error");

	fz_error *error;
	fz_obj *obj;

	if (ctx->page)
		pdf_droppage(ctx->page);
	ctx->page = 0;

	obj = pdf_getpageobject(ctx->pages, ctx->pageno - 1);

	error = pdf_loadpage(&ctx->page, ctx->xref, obj);
	if (error) {
		printf("errLP1: %s\n", error->msg);
		strcpy(lastPageError, error->msg);
		return -1;
	}

	//printf("\n\n------------------------------------------------\n");
	//fz_debugtree(ctx->page->tree);

	if (BKUser::options.pdfFastScroll) {
		pdfRenderFullPage(ctx);
	}
	return 0;
}

BKPDF::BKPDF(string& f) : ctx(0), bannerFrames(0), banner(""), panX(0), panY(0), loadNewPage(false), pageError(false), path(f) {
}

static BKPDF* singleton = 0;

BKPDF::~BKPDF() {
	if (ctx != 0) {
		setBookmark(true);

		pdfClose(ctx);
		delete ctx;
	}
	ctx = 0;
	singleton = 0;

	if (fullPageBuffer != NULL) {
		fz_droppixmap(fullPageBuffer);
		fullPageBuffer = NULL;
	}
}

static long long alloc_mallocs = 0;
static long long alloc_malloc_size = 0;
static long long alloc_frees = 0;
static long long alloc_reallocs = 0;
static long long alloc_realloc_size = 0;
static long long alloc_large_realloc = 0;

static void reset_allocs() {
	alloc_mallocs = 0;
	alloc_malloc_size = 0;
	alloc_frees = 0;
	alloc_reallocs = 0;
	alloc_realloc_size = 0;
	alloc_large_realloc = 0;
}

static void print_allocs() {
#ifndef PSP
/*
	printf("\n-----------------------------\n");
	printf("alloc_mallocs = %qd\n", alloc_mallocs);
	printf("alloc_malloc_size = %qd\n", alloc_malloc_size); 
	printf("alloc_frees = %qd\n", alloc_frees);
	printf("alloc_reallocs = %qd\n", alloc_reallocs);
	printf("alloc_realloc_size = %qd\n", alloc_realloc_size);
	printf("alloc_large_realloc = %qd\n", alloc_large_realloc);
*/
#endif
}

static void* bkmalloc(fz_memorycontext *mem, int n) {
	void* buf = NULL;
	/*if (n >= 64)
		buf = memalign(16, n);
	else*/
		buf = malloc(n);
	memset(buf, 0, n);
	alloc_malloc_size += n;
	++alloc_mallocs;
	return buf;
}

static void *bkrealloc(fz_memorycontext *mem, void *p, int n) {
	++alloc_reallocs;
	alloc_realloc_size += n;
	alloc_large_realloc = alloc_large_realloc < n ? n : alloc_large_realloc;
#ifndef PSP
	//printf("%d\n", n);
#endif
	return realloc(p, n);
}

static void bkfree(fz_memorycontext *mem, void *p) {
	free(p);
	++alloc_frees;
}

static fz_memorycontext bkmem = { bkmalloc, bkrealloc, bkfree };

static bool lastScrollFlag = false;
BKPDF* BKPDF::create(string& file) {
	if (singleton != 0) {
		printf("cannot open more than 1 pdf at the same time\n");
		return singleton;
	}
	
	BKPDF* b = new BKPDF(file);
	singleton = b;

	fz_setmemorycontext(&bkmem);
	fz_cpudetect();
	fz_accelerate();

	pdfInit();

	PDFContext* ctx = pdfOpen((char*)file.c_str());
	if (ctx == 0) {
		delete b;
		return 0;
	}
	b->ctx = ctx;
	
	// Add bookmark support
	int position = BKBookmark::getLastView(b->path);
	b->setPage(position);
	
	reset_allocs();
	b->pageError = pdfLoadPage(ctx) != 0;

	FZScreen::resetReps();
	b->redrawBuffer();
	print_allocs();
	lastScrollFlag = BKUser::options.pdfFastScroll;
	return b;
}

void BKPDF::setPage(int position) {
	if (position > 0 && position <= pdf_getpagecount(ctx->pages))
		ctx->pageno = position;
}

void BKPDF::getPath(string& s) {
	s = path;
}

void BKPDF::render() {
	// the copyImage fills the entire screen
	//FZScreen::clear(0xffffff, FZ_COLOR_BUFFER);
	FZScreen::color(0xffffffff);
	FZScreen::matricesFor2D();
	FZScreen::enable(FZ_TEXTURE_2D);
	FZScreen::enable(FZ_BLEND);
	FZScreen::blendFunc(FZ_ADD, FZ_SRC_ALPHA, FZ_ONE_MINUS_SRC_ALPHA);

	FZScreen::copyImage(FZ_PSM_8888, 0, 0, 480, 272, 480, bounceBuffer, 0, 0, 512, (void*)(0x04000000+(unsigned int)FZScreen::framebuffer()));

	if (pageError) {
		texUI->bindForDisplay();
		FZScreen::ambientColor(0xf06060ff);
		drawRect(0, 126, 480, 26, 6, 31, 1);
		fontBig->bindForDisplay();
		FZScreen::ambientColor(0xff222222);
		char t[256];
		snprintf(t, 256, "Error in page %d: %s", ctx->pageno, lastPageError);
		drawTextHC(t, fontBig, 130);
	}
	if (loadNewPage && BKUser::options.displayLabels) {
		texUI->bindForDisplay();
		FZScreen::ambientColor(0xf0222222);
		drawPill(150, 240, 180, 20, 6, 31, 1);
		fontBig->bindForDisplay();
		FZScreen::ambientColor(0xffffffff);
		drawTextHC("Loading", fontBig, 244);
	}
	if (bannerFrames > 0 && BKUser::options.displayLabels) {
		int alpha = 0xff;
		if (bannerFrames <= 32) {
			alpha = bannerFrames*(256/32) - 8;
		}
		if (alpha > 0) {
			texUI->bindForDisplay();
			FZScreen::ambientColor(0x222222 | (alpha << 24));
			drawPill(150, 240, 180, 20, 6, 31, 1);
			fontBig->bindForDisplay();
			FZScreen::ambientColor(0xffffff | (alpha << 24));
			//char t[256];
			//snprintf(t, 256, "Page %d of %d", ctx->pageno, pdf_getpagecount(ctx->pages));
			//drawTextHC(t, fontBig, 244);
			drawTextHC((char*)banner.c_str(), fontBig, 244);
		}
	}
}

static inline void bk_memcpysr8(void* to, void* from, unsigned int n) {
	int* s = (int*)from;
	int* d = (int*)to;
	n >>= 2;
	for (unsigned int i = 0; i < n; ++i) {
		*d = *s >> 8;
		++s;
		++d;
	}
}

#ifdef PSP
extern "C" {
	extern int bk_memcpy(void*, void*, unsigned int);
	// not working...
	//extern int bk_memcpysr8(void*, void*, unsigned int);
};
#else
#define bk_memcpy memcpy
#endif


void BKPDF::redrawBuffer() {
	if (pageError)
		return;
	if (BKUser::options.pdfFastScroll && fullPageBuffer != NULL) {
		// copy region of the full page buffer
		int cw = 480;
		bool fillGrey = false;
		int dskip = 0;
		int px = panX;
		int py = panY;
		if (fullPageBuffer->w < 480) {
			px = 0;
			cw = fullPageBuffer->w;
			fillGrey = true;
			dskip += (480 - fullPageBuffer->w) / 2;
		} else if (px + 480 > fullPageBuffer->w) {
			px = fullPageBuffer->w - 480;
		}
		int ch = 272;
		if (fullPageBuffer->h < 272) {
			py = 0;
			ch = fullPageBuffer->h;
			fillGrey = true;
			dskip += ((272 - fullPageBuffer->h) / 2)*480;
		} else if (py + 272 > fullPageBuffer->h) {
			py = fullPageBuffer->h - 272;
		}
		unsigned int* s = (unsigned int*)fullPageBuffer->samples + px + (fullPageBuffer->w*py);
		unsigned int* d = bounceBuffer;
		if (fillGrey) {
			memset(d, 0x50, 480*272*4);
		}
		d += dskip;
		for (int j = 0; j < ch; ++j) {
			bk_memcpy(d, s, cw*4);
			d += 480;
			s += fullPageBuffer->w;
		}
		return;
	}
	fz_pixmap* pix = pdfRenderTile(ctx, panX, panY, 480, 272);
	// copy and shift colors
	unsigned int* s = (unsigned int*)pix->samples;
	unsigned int* d = backBuffer;
	const int n = 480*272;
	bk_memcpysr8(d, s, n*4);
	fz_droppixmap(pix);
	bk_memcpy(bounceBuffer, backBuffer, n*4);
}

void BKPDF::panBuffer(int nx, int ny) {
	if (BKUser::options.pdfFastScroll && fullPageBuffer != NULL) {
		panX = nx;
		panY = ny;
		redrawBuffer();
		return;
	}
	if (pageError)
		return;
	if (ny != panY) {
		int dy = ny - panY;
		int ady = dy > 0 ? dy : -dy;
		int pdfx = panX;
		int pdfy = dy > 0 ? panY + 272 : panY + dy;
		int pdfw = 480;
		int pdfh = ady;
		int bx = 0;
		int by = dy > 0 ? 272 - dy : 0;
		int destDirtyX = 0;
		int destDirtyY = dy > 0 ? 0 : ady;
		int srcDirtyY = dy > 0 ? ady : 0;
		int n = pdfh*pdfw;
		// displaced area
		bk_memcpy(backBuffer + destDirtyX + destDirtyY*480, bounceBuffer + srcDirtyY*480, 480*(272 - pdfh)*4);
		// render new area
		fz_pixmap* pix = pdfRenderTile(ctx, pdfx, pdfy, pdfw, pdfh);
		// copy and shift colors
		bk_memcpysr8(backBuffer + bx + by*480, pix->samples, n*4);
		fz_droppixmap(pix);
		panY = ny;
		unsigned int* t = bounceBuffer;
		bounceBuffer = backBuffer;
		backBuffer = t;
	}
	if (nx != panX) {
		int dx = nx - panX;
		int adx = dx > 0 ? dx : -dx;
		int pdfx = dx > 0 ? panX + 480 : panX + dx;
		int pdfy = panY;
		int pdfw = adx;
		int pdfh = 272;
		int bx = dx > 0 ? 480 - dx : 0;
		int by = 0;

		int destDirtyX = dx > 0 ? 0 : adx;
		int destDirtyY = 0;
		int srcDirtyX = dx > 0 ? adx : 0;
		// displaced area
		unsigned int* s = bounceBuffer + srcDirtyX;
		unsigned int* d = backBuffer + destDirtyX + destDirtyY*480;
		int tw = 480 - pdfw;
		for (int j = 0; j < 272; ++j) {
			bk_memcpy(d, s, tw*4);
			s += 480;
			d += 480;
		}
		// render new area
		fz_pixmap* pix = pdfRenderTile(ctx, pdfx, pdfy, pdfw, pdfh);
		// copy and shift colors
		s = (unsigned int*)pix->samples;
		d = backBuffer + bx + by*480;
		for (int j = 0; j < 272; ++j) {
			bk_memcpysr8(d, s, pdfw*4);
			d += tw + pdfw;
			s += pdfw;
		}
		fz_droppixmap(pix);
		panX = nx;
		unsigned int* t = bounceBuffer;
		bounceBuffer = backBuffer;
		backBuffer = t;
	}
}

static const float zoomLevels[] = { 0.25f, 0.5f, 0.75f, 0.90f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f,
	1.6f, 1.7f, 1.8f, 1.9f, 2.0f, 2.25f, 2.5f, 2.75f, 3.0f, 3.5f, 4.0f, 5.0f, 7.5f, 10.0f, 16.0f };

int BKPDF::update(unsigned int buttons) {
	if (lastScrollFlag != BKUser::options.pdfFastScroll)
		return BK_CMD_RELOAD;

	bannerFrames--;
	if (bannerFrames < 0)
		bannerFrames = 0;

	if (loadNewPage) {
		if (FZScreen::wasSuspended()) {
			FZScreen::clearSuspended();
			PDFContext* oldctx = ctx;
			ctx = pdfOpen((char*)path.c_str());
			if (ctx == 0) {
				// fucked... just implement error handling some day ok???
				return 0;
			}
			ctx->pageno = oldctx->pageno;
			ctx->zoom = oldctx->zoom;
			ctx->zoomLevel = oldctx->zoomLevel;
			ctx->rotate = oldctx->rotate;
			// remove this if it crashes on resume, but it will leak a ton of ram
			// need*pooled*malloc*NOW
			//pdfClose(oldctx);
			delete oldctx;
		}
		pageError = pdfLoadPage(ctx) != 0;
		redrawBuffer();
		loadNewPage = false;
		char t[256];
		snprintf(t, 256, "Page %d of %d", ctx->pageno, pdf_getpagecount(ctx->pages));
		banner = t;
		bannerFrames = 60;
		return BK_CMD_MARK_DIRTY;
	}

	int* b = FZScreen::ctrlReps();

	float nx = panX;
	float ny = panY;
	bool fullRedraw = false;
	// pan
	if (b[BKUser::pdfControls.panUp] == 1 || b[BKUser::pdfControls.panUp] > 20) {
		ny -= 16.0f;
	}
	if (b[BKUser::pdfControls.panDown] == 1 || b[BKUser::pdfControls.panDown] > 20) {
		ny += 16.0f;
	}
	if (b[BKUser::pdfControls.panLeft] == 1 || b[BKUser::pdfControls.panLeft] > 20) {
		nx -= 16.0f;
	}
	if (b[BKUser::pdfControls.panRight] == 1 || b[BKUser::pdfControls.panRight] > 20) {
		nx += 16.0f;
	}
	// zoom
	if (b[BKUser::pdfControls.zoomOut] == 1) {
		--ctx->zoomLevel;
		if (ctx->zoomLevel < 0)
			ctx->zoomLevel = 0;
		if (BKUser::options.pdfFastScroll && ctx->zoomLevel > 14) {
			ctx->zoomLevel = 14;
			ctx->zoom = 2.0f;
		}
		ctx->zoom = zoomLevels[ctx->zoomLevel];
		fullRedraw = true;
		nx *= ctx->zoom;
		ny *= ctx->zoom;
		char t[256];
		snprintf(t, 256, "Zoom %2.3gx", ctx->zoom);
		banner = t;
		bannerFrames = 60;
	}
	if (b[BKUser::pdfControls.zoomIn] == 1) {
		++ctx->zoomLevel;
		int n = sizeof(zoomLevels)/sizeof(float);
		if (ctx->zoomLevel >= n)
			ctx->zoomLevel = n - 1;
		if (BKUser::options.pdfFastScroll && ctx->zoomLevel > 14) {
			ctx->zoomLevel = 14;
			ctx->zoom = 2.0f;
		}
		ctx->zoom = zoomLevels[ctx->zoomLevel];
		fullRedraw = true;
		nx *= ctx->zoom;
		ny *= ctx->zoom;
		char t[256];
		snprintf(t, 256, "Zoom %2.3gx", ctx->zoom);
		banner = t;
		bannerFrames = 60;
	}
	// clip coords
	if (!pageError) {
		fz_matrix ctm = pdfViewctm(ctx);
		fz_rect bbox = ctx->page->mediabox;
		bbox = fz_transformaabb(ctm, bbox);
		if (ny < 0.0f) {
			ny = 0.0f;
		}
		float h = bbox.y1 - bbox.y0;
		if (ny >= h - 272.0f) {
			ny = h - 273.0f;
		}
		if (nx < 0.0f) {
			nx = 0.0f;
		}
		float w = bbox.x1 - bbox.x0;
		if (nx >= w - 480.0f) {
			nx = w - 481.0f;
		}
#if 0
		if (ny < 0.0f) {
			ny = 0.0f;
		}
		float h = ctx->page->mediabox.y1 - ctx->page->mediabox.y0;
		h *= ctx->zoom;
		if (ny >= h - 272.0f) {
			ny = h - 273.0f;
		}
		if (nx < 0.0f) {
			nx = 0.0f;
		}
		float w = ctx->page->mediabox.x1 - ctx->page->mediabox.x0;
		w *= ctx->zoom;
		if (nx >= w - 480.0f) {
			nx = w - 481.0f;
		}
#endif
	}

	int inx = (int)nx;
	int iny = (int)ny;
	// redraw and/or pan
	if (fullRedraw) {
		panX = inx;
		panY = iny;
		if (BKUser::options.pdfFastScroll) {
			//pdfRenderFullPage(ctx);
			loadNewPage = true;
			return BK_CMD_MARK_DIRTY;
		}
		redrawBuffer();
		return BK_CMD_MARK_DIRTY;
	}
	if (inx != panX || iny != panY) {
		panBuffer(inx, iny);
		return BK_CMD_MARK_DIRTY;
	}

	// main menu
	if (b[FZ_REPS_START] == 1) {
		return BK_CMD_INVOKE_MENU;
	}

	// banner fade
	if (bannerFrames > 0)
		return BK_CMD_MARK_DIRTY;

	// next/prev page
	int oldpage = ctx->pageno;

	if (b[BKUser::pdfControls.nextPage] == 1) {
		ctx->pageno++;
	}
	if (b[BKUser::pdfControls.previousPage] == 1) {
		ctx->pageno--;
	}
	if (b[BKUser::pdfControls.next10Pages] == 1) {
		ctx->pageno+=10;
	}
	if (b[BKUser::pdfControls.previous10Pages] == 1) {
		ctx->pageno-=10;
	}

	if (ctx->pageno < 1)
		ctx->pageno = 1;
	if (ctx->pageno > pdf_getpagecount(ctx->pages))
		ctx->pageno = pdf_getpagecount(ctx->pages);

	if (ctx->pageno != oldpage) {
		loadNewPage = true;
		panY = 0;
		/*pdfLoadPage(ctx);
		redrawBuffer();
		return BK_CMD_MARK_DIRTY;*/
	}

	return 0;
}

void BKPDF::reloadPage(int position) {
	setPage(position);
	loadNewPage = true;
	panY = 0;
}

void BKPDF::setBookmark(bool lastview) {
	// Save the last position		
	BKBookmark::set(path, ctx->pageno, lastview);
}
