/** \file
 * \brief Cairo Double Buffer Driver
 *
 * See Copyright Notice in cd.h
 */

#include "cdcairoctx.h"
#include "cddbuf.h"
#include <stdlib.h>
#include <stdio.h>


static void cdkillcanvas (cdCtxCanvas* ctxcanvas)
{
  cdKillImage(ctxcanvas->image_dbuffer);
  if (ctxcanvas->kill_dbuffer)
    cdKillCanvas(ctxcanvas->canvas_dbuffer);
  ctxcanvas->cr = NULL; /* avoid to destroy it twice */
  cdcairoKillCanvas(ctxcanvas);
}

static void cddeactivate(cdCtxCanvas* ctxcanvas)
{
  cdCanvas* canvas_dbuffer = ctxcanvas->canvas_dbuffer;
  /* this is done in the canvas_dbuffer context */
  cdCanvasDeactivate(canvas_dbuffer);
}

static void cdflush(cdCtxCanvas* ctxcanvas)
{
  int old_writemode;
  cdImage* image_dbuffer = ctxcanvas->image_dbuffer;
  cdCanvas* canvas_dbuffer = ctxcanvas->canvas_dbuffer;

  /* flush the writing in the image */
  cairo_show_page(ctxcanvas->cr);

  /* this is done in the canvas_dbuffer context */
  /* Flush can be affected by Origin and Clipping, but not WriteMode */
  old_writemode = cdCanvasWriteMode(canvas_dbuffer, CD_REPLACE);
  cdCanvasPutImageRect(canvas_dbuffer, image_dbuffer, 0, 0, 0, 0, 0, 0);
  cdCanvasWriteMode(canvas_dbuffer, old_writemode);
}

static void set_killdbuffer_attrib(cdCtxCanvas* ctxcanvas, char* data)
{
  if (!data || data[0] == '0')
    ctxcanvas->kill_dbuffer = 0;
  else
    ctxcanvas->kill_dbuffer = 1;
}

static char* get_killdbuffer_attrib(cdCtxCanvas* ctxcanvas)
{
  if (ctxcanvas->kill_dbuffer)
    return "1";
  else
    return "0";
}

static cdAttribute killdbuffer_attrib =
{
  "KILLDBUFFER",
  set_killdbuffer_attrib,
  get_killdbuffer_attrib
};

static void cdcreatecanvas(cdCanvas* canvas, cdCanvas* canvas_dbuffer)
{
  int w, h;
  cdCtxCanvas* ctxcanvas;
  cdImage* image_dbuffer;
  cdCtxImage* ctximage;

  cdCanvasActivate(canvas_dbuffer);
  w = canvas_dbuffer->w;
  h = canvas_dbuffer->h;
  if (w==0) w=1;
  if (h==0) h=1;

  /* this is done in the canvas_dbuffer context */
  image_dbuffer = cdCanvasCreateImage(canvas_dbuffer, w, h);
  if (!image_dbuffer) 
    return;

  ctximage = image_dbuffer->ctximage;

  /* Init the driver DBuffer */
  ctxcanvas = cdcairoCreateCanvas(canvas, ctximage->cr);

  if (!ctxcanvas)
    return;

  ctxcanvas->image_dbuffer = image_dbuffer;
  ctxcanvas->canvas_dbuffer = canvas_dbuffer;

  canvas->w = ctximage->w;
  canvas->h = ctximage->h;
  canvas->w_mm = ctximage->w_mm;
  canvas->h_mm = ctximage->h_mm;
  canvas->bpp = ctximage->bpp;
  canvas->xres = ctximage->xres;
  canvas->yres = ctximage->yres;

  {
    char* mode = cdCanvasGetAttribute(canvas_dbuffer, "UTF8MODE");
    int utf8mode = mode? (mode[0]=='1'? 1: 0): 0;
    if (utf8mode)
      cdCanvasSetAttribute(canvas, "UTF8MODE", "1");
  }

  cdRegisterAttribute(canvas, &killdbuffer_attrib);
}

static int cdactivate(cdCtxCanvas* ctxcanvas)
{
  int w, h;
  cdCanvas* canvas_dbuffer = ctxcanvas->canvas_dbuffer;

  /* this is done in the canvas_dbuffer context */
  /* this will update canvas size */
  cdCanvasActivate(canvas_dbuffer);
  w = canvas_dbuffer->w;
  h = canvas_dbuffer->h;
  if (w==0) w=1;
  if (h==0) h=1;

  /* check if the size changed */
  if (w != ctxcanvas->image_dbuffer->w ||
      h != ctxcanvas->image_dbuffer->h)
  {
    cdCanvas* canvas = ctxcanvas->canvas;
    /* save the current, if the rebuild fail */
    cdImage* old_image_dbuffer = ctxcanvas->image_dbuffer;
    cdCtxCanvas* old_ctxcanvas = ctxcanvas;

    /* if the image is rebuild, the canvas that uses the image must be also rebuild */

    /* rebuild the image and the canvas */
    canvas->ctxcanvas = NULL;
    cdcreatecanvas(canvas, canvas_dbuffer);
    if (!canvas->ctxcanvas)
    {
      canvas->ctxcanvas = old_ctxcanvas;
      return CD_ERROR;
    }

    canvas->ctxcanvas->kill_dbuffer = old_ctxcanvas->kill_dbuffer;

    /* remove the old image and canvas */
    cdKillImage(old_image_dbuffer);
    old_ctxcanvas->cr = NULL; /* avoid to destroy it twice */
    cdcairoKillCanvas(old_ctxcanvas);

    /* update canvas attributes */
    cdUpdateAttributes(canvas);
  }

  return CD_OK;
}

static void cdinittable(cdCanvas* canvas)
{
  cdcairoInitTable(canvas);

  canvas->cxActivate = cdactivate;
  canvas->cxDeactivate = cddeactivate;
  canvas->cxFlush = cdflush;
  canvas->cxKillCanvas = cdkillcanvas;
}

static cdContext cdDBufferContext =
{
  CD_CAP_ALL & ~(CD_CAP_PLAY | CD_CAP_YAXIS | CD_CAP_REGION | CD_CAP_WRITEMODE | CD_CAP_PALETTE ),
  CD_CTX_IMAGE|CD_CTX_PLUS,
  cdcreatecanvas,  
  cdinittable,
  NULL,             
  NULL, 
};

#ifdef USE_GTK3
cdContext* cdContextDBuffer(void)
#else
cdContext* cdContextCairoDBuffer(void)
#endif
{
  return &cdDBufferContext;
}
