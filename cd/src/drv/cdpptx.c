/** \file
* \brief CD PPTX driver
*
* See Copyright Notice in cd.h
*/

#ifdef WIN32
#include <windows.h>
#else
#include <iconv.h>
#endif

#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <limits.h> 
#include <math.h>

#include "cd.h"
#include "cd_private.h"
#include "cdpptx.h"

#include "pptx.h"

struct _cdCtxCanvas
{
  /* public */
  cdCanvas* canvas;
  char filename[10240];

  pptxPresentation *presentation;

  int nDashes;
  int *dashes;

  char* utf8_buffer;
  int utf8mode, utf8_buffer_len;

#ifndef WIN32
  iconv_t cdgl_iconv;
#endif
};

static void cdglCheckUtf8Buffer(cdCtxCanvas *ctxcanvas, int len)
{
  if (!ctxcanvas->utf8_buffer)
  {
    ctxcanvas->utf8_buffer = malloc(len + 1);
    ctxcanvas->utf8_buffer_len = len;
  }
  else if (ctxcanvas->utf8_buffer_len < len)
  {
    ctxcanvas->utf8_buffer = realloc(ctxcanvas->utf8_buffer, len + 1);
    ctxcanvas->utf8_buffer_len = len;
  }
}

static void cdglStrConvertToUTF8(cdCtxCanvas *ctxcanvas, const char* str, int len)
{
  /* FTGL multibyte strings are always UTF-8 */

  if (ctxcanvas->utf8mode || cdStrIsAscii(str))
  {
    cdglCheckUtf8Buffer(ctxcanvas, len);
    memcpy(ctxcanvas->utf8_buffer, str, len);
    ctxcanvas->utf8_buffer[len] = 0;
    return;
  }

#ifdef WIN32
  {
    wchar_t* wstr;
    int wlen = MultiByteToWideChar(0, 0, str, len, NULL, 0);
    if (!wlen)
    {
      cdglCheckUtf8Buffer(ctxcanvas, 1);
      ctxcanvas->utf8_buffer[0] = 0;
      return;
    }

    wstr = (wchar_t*)calloc((wlen + 1), sizeof(wchar_t));
    MultiByteToWideChar(0, 0, str, len, wstr, wlen);
    wstr[wlen] = 0;

    len = WideCharToMultiByte(65001, 0, wstr, wlen, NULL, 0, NULL, NULL);
    if (!len)
    {
      cdglCheckUtf8Buffer(ctxcanvas, 1);
      ctxcanvas->utf8_buffer[0] = 0;
      free(wstr);
      return;
    }

    cdglCheckUtf8Buffer(ctxcanvas, len);
    WideCharToMultiByte(65001, 0, wstr, wlen, ctxcanvas->utf8_buffer, len, NULL, NULL);
    ctxcanvas->utf8_buffer[len] = 0;

    free(wstr);
  }
#else
  {
    size_t ulen = (size_t)len;
    size_t utf8len = ulen * 2;

    if (ctxcanvas->cdgl_iconv == (iconv_t)-1)
    {
      cdglCheckUtf8Buffer(ctxcanvas, 1);
      ctxcanvas->utf8_buffer[0] = 0;
      return;
    }

    cdglCheckUtf8Buffer(ctxcanvas, utf8len);

    iconv(ctxcanvas->cdgl_iconv, (char**)&str, &ulen, &(ctxcanvas->utf8_buffer), &utf8len);
  }
#endif
}

static const char* getHatchStyles(int style)
{
  switch (style)
  {
  default: /* CD_HORIZONTAL */
    return "ltHorz";
  case CD_VERTICAL:
    return "ltVert";
  case CD_FDIAGONAL:
    return "ltDnDiag";
  case CD_BDIAGONAL:
    return "ltUpDiag";
  case CD_CROSS:
    return "smGrid";
  case CD_DIAGCROSS:
    return "diagCross";
  }
}

static const char* getLineStyle(cdCtxCanvas *ctxcanvas, int lineStyle)
{
  int i;

  switch (lineStyle)
  {
  default: /* CD_CONTINUOUS */
    return "solid";
  case CD_DASHED:
    return "sysDash";
  case CD_DOTTED:
    return "sysDot";
  case CD_DASH_DOT:
    return "sysDashDot";
  case CD_DASH_DOT_DOT:
    return "sysDashDotDot";
  case CD_CUSTOM:
    ctxcanvas->nDashes = ctxcanvas->canvas->line_dashes_count;
    if (ctxcanvas->dashes)
      free(ctxcanvas->dashes);
    ctxcanvas->dashes = (int *)malloc(sizeof(int)*ctxcanvas->nDashes);
    for (i = 0; i < ctxcanvas->nDashes; i += 2)
    {
      ctxcanvas->dashes[i] = (ctxcanvas->canvas->line_dashes[i] / ctxcanvas->canvas->line_width) * 100;
      ctxcanvas->dashes[i + 1] = (ctxcanvas->canvas->line_dashes[i + 1] / ctxcanvas->canvas->line_width) * 100;
    }
    return "custom";
  }
}

static void setInteriorStyle(cdCtxCanvas *ctxcanvas, int interiorStyle, int hatchStyle, unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha,
                             unsigned char bRed, unsigned char bGreen, unsigned char bBlue, unsigned char bAlpha, int backopacity)
{
  switch (interiorStyle)
  {
  case CD_SOLID:
    pptxSolidFill(ctxcanvas->presentation, red, green, blue, alpha);
    break;
  case CD_HATCH:
    pptxHatchLine(ctxcanvas->presentation, getHatchStyles(hatchStyle), red, green, blue, alpha, bRed, bGreen, bBlue, bAlpha);
    break;
  case CD_PATTERN:
  {
    int width, height, i;
    long *pattern = cdCanvasGetPattern(ctxcanvas->canvas, &width, &height);
    int plane_size = width*height;
    unsigned char *red = (unsigned char *)malloc(3 * plane_size);
    unsigned char *green = red + plane_size;
    unsigned char *blue = green + plane_size;
    for (i = 0; i<plane_size; i++)
      cdDecodeColor(pattern[i], &red[i], &green[i], &blue[i]);
    pptxPattern(ctxcanvas->presentation, red, green, blue, width, height);
    free(red);
    break;
  }
  case CD_STIPPLE:
  {
    int width, height;
    unsigned char *stipple = cdCanvasGetStipple(ctxcanvas->canvas, &width, &height);
    long foreground = cdCanvasForeground(ctxcanvas->canvas, CD_QUERY);
    long background = cdCanvasForeground(ctxcanvas->canvas, CD_QUERY);
    pptxStipple(ctxcanvas->presentation, cdRed(foreground), cdGreen(foreground), cdBlue(foreground), cdRed(background), cdGreen(background), cdBlue(background), stipple, width, height, backopacity);
    break;
  }
  default: /* CD_HOLLOW */
    pptxNoFill(ctxcanvas->presentation);
    break;
  }
}

static void cdkillcanvas(cdCtxCanvas *ctxcanvas)
{
  pptxKillPresentation(ctxcanvas->presentation, ctxcanvas->filename);

#ifndef WIN32
  if (ctxcanvas->cdgl_iconv != (iconv_t)-1)
    iconv_close(ctxcanvas->cdgl_iconv);
#endif

  if (ctxcanvas->dashes)
    free(ctxcanvas->dashes);

  if (ctxcanvas->utf8_buffer)
    free(ctxcanvas->utf8_buffer);

  memset(ctxcanvas, 0, sizeof(cdCtxCanvas));
  free(ctxcanvas);
}

static void cdflush(cdCtxCanvas* ctxcanvas)
{
  pptxCloseSlide(ctxcanvas->presentation);

  pptxOpenSlide(ctxcanvas->presentation);
}

static int cdclip(cdCtxCanvas *ctxcanvas, int mode)
{
  /* dummy - must be defined */
  (void)ctxcanvas;
  return mode;
}

static void cdline(cdCtxCanvas* ctxcanvas, int x1, int y1, int x2, int y2)
{
  int xmin = x1;
  int xmax = x1;
  int ymin = y1;
  int ymax = y1;

  long foreground = cdCanvasForeground(ctxcanvas->canvas, CD_QUERY);

  unsigned char red = cdRed(foreground);
  unsigned char green = cdGreen(foreground);
  unsigned char blue = cdBlue(foreground);

  unsigned char alpha = cdAlpha(foreground);

  const char* lineStyle = getLineStyle(ctxcanvas, cdCanvasLineStyle(ctxcanvas->canvas, CD_QUERY));

  int width = cdCanvasLineWidth(ctxcanvas->canvas, CD_QUERY);

  if (x2<xmin) xmin = x2;
  if (x2>xmax) xmax = x2;
  if (y2<ymin) ymin = y2;
  if (y2>ymax) ymax = y2;

  pptxBeginLine(ctxcanvas->presentation, xmin, ymin, (xmax - xmin) + 1, (ymax - ymin) + 1);

  pptxMoveTo(ctxcanvas->presentation, x1 - xmin, y1 - ymin);

  pptxLineTo(ctxcanvas->presentation, x2 - xmin, y2 - ymin);

  pptxLineClose(ctxcanvas->presentation);

  pptxNoFill(ctxcanvas->presentation);

  pptxEndLine(ctxcanvas->presentation, width, red, green, blue, alpha, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
}

static void cdrect(cdCtxCanvas *ctxcanvas, int xmin, int xmax, int ymin, int ymax)
{
  long foreground = cdCanvasForeground(ctxcanvas->canvas, CD_QUERY);

  unsigned char red = cdRed(foreground);
  unsigned char green = cdGreen(foreground);
  unsigned char blue = cdBlue(foreground);

  unsigned char alpha = cdAlpha(foreground);

  const char* lineStyle = getLineStyle(ctxcanvas, cdCanvasLineStyle(ctxcanvas->canvas, CD_QUERY));

  int width = cdCanvasLineWidth(ctxcanvas->canvas, CD_QUERY);

  pptxBeginLine(ctxcanvas->presentation, xmin, ymin, (xmax - xmin) + 1, (ymax - ymin) + 1);

  pptxMoveTo(ctxcanvas->presentation, xmin - xmin, ymin - ymin);

  pptxLineTo(ctxcanvas->presentation, xmax - xmin, ymin - ymin);
  pptxLineTo(ctxcanvas->presentation, xmax - xmin, ymax - ymin);
  pptxLineTo(ctxcanvas->presentation, xmin - xmin, ymax - ymin);
  pptxLineTo(ctxcanvas->presentation, xmin - xmin, ymin - ymin);

  pptxLineClose(ctxcanvas->presentation);

  pptxNoFill(ctxcanvas->presentation);

  pptxEndLine(ctxcanvas->presentation, width, red, green, blue, alpha, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
}

static void cdbox(cdCtxCanvas *ctxcanvas, int xmin, int xmax, int ymin, int ymax)
{
  long foreground = cdCanvasForeground(ctxcanvas->canvas, CD_QUERY);
  long background = cdCanvasBackground(ctxcanvas->canvas, CD_QUERY);

  unsigned char red = cdRed(foreground);
  unsigned char green = cdGreen(foreground);
  unsigned char blue = cdBlue(foreground);

  unsigned char alpha = cdAlpha(foreground);

  unsigned char bRed = cdRed(background);
  unsigned char bGreen = cdGreen(background);
  unsigned char bBlue = cdBlue(background);

  unsigned char bAlpha = cdAlpha(background);

  const char* lineStyle = getLineStyle(ctxcanvas, cdCanvasLineStyle(ctxcanvas->canvas, CD_QUERY));

  int width = cdCanvasLineWidth(ctxcanvas->canvas, CD_QUERY);

  int backOpacity = cdCanvasBackOpacity(ctxcanvas->canvas, CD_QUERY);

  int interiorStyle = cdCanvasInteriorStyle(ctxcanvas->canvas, CD_QUERY);

  int hatchStyle = cdCanvasHatch(ctxcanvas->canvas, CD_QUERY);

  pptxBeginLine(ctxcanvas->presentation, xmin, ymin, (xmax - xmin) + 1, (ymax - ymin) + 1);

  pptxMoveTo(ctxcanvas->presentation, xmin - xmin, ymin - ymin);

  pptxLineTo(ctxcanvas->presentation, xmin - xmin, ymin - ymin);
  pptxLineTo(ctxcanvas->presentation, xmax - xmin, ymin - ymin);
  pptxLineTo(ctxcanvas->presentation, xmax - xmin, ymax - ymin);
  pptxLineTo(ctxcanvas->presentation, xmin - xmin, ymax - ymin);
  pptxLineTo(ctxcanvas->presentation, xmin - xmin, ymin - ymin);

  pptxLineClose(ctxcanvas->presentation);

  setInteriorStyle(ctxcanvas, interiorStyle, hatchStyle, red, green, blue, alpha,
                   bRed, bGreen, bBlue, bAlpha, backOpacity);

  pptxEndLine(ctxcanvas->presentation, width, red, green, blue, alpha, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
}

static void cdarc(cdCtxCanvas *ctxcanvas, int xc, int yc, int w, int h, double a1, double a2)
{
  int arcStartX, arcStartY, arcEndX, arcEndY;
  double angle1, angle2;

  int pxmin = xc - (w / 2);
  int pymin = yc - (h / 2);

  long foreground = cdCanvasForeground(ctxcanvas->canvas, CD_QUERY);

  unsigned char red = cdRed(foreground);
  unsigned char green = cdGreen(foreground);
  unsigned char blue = cdBlue(foreground);

  unsigned char alpha = cdAlpha(foreground);

  const char* lineStyle = getLineStyle(ctxcanvas, cdCanvasLineStyle(ctxcanvas->canvas, CD_QUERY));

  int width = cdCanvasLineWidth(ctxcanvas->canvas, CD_QUERY);

  if (ctxcanvas->canvas->invert_yaxis)
    cdGetArcStartEnd(xc, yc, w, h, -a1, -a2, &arcStartX, &arcStartY, &arcEndX, &arcEndY);
  else
    cdGetArcStartEnd(xc, yc, w, h, a1, a2, &arcStartX, &arcStartY, &arcEndX, &arcEndY);

  angle1 = atan2(arcStartY - yc, arcStartX - xc)*CD_RAD2DEG;
  angle2 = atan2(arcEndY - yc, arcEndX - xc)*CD_RAD2DEG;

  if (angle1 < 0.)
    angle1 += 360.;

  if (angle2 < 0.)
    angle2 += 360.;

  if (angle2 < angle1)
  {
    double tmp = angle1;
    angle1 = angle2;
    angle2 = tmp;
  }

  pptxBeginSector(ctxcanvas->presentation, "arc", pxmin, pymin, abs(w), abs(h), (int)angle1, (int)angle2);

  pptxNoFill(ctxcanvas->presentation);

  pptxEndLine(ctxcanvas->presentation, width, red, green, blue, alpha, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
}

static void cdsector(cdCtxCanvas *ctxcanvas, int xc, int yc, int w, int h, double a1, double a2)
{
  int arcStartX, arcStartY, arcEndX, arcEndY;
  double angle1, angle2;

  int pxmin = xc - (w / 2);
  int pymin = yc - (h / 2);

  long foreground = cdCanvasForeground(ctxcanvas->canvas, CD_QUERY);
  long background = cdCanvasBackground(ctxcanvas->canvas, CD_QUERY);

  unsigned char red = cdRed(foreground);
  unsigned char green = cdGreen(foreground);
  unsigned char blue = cdBlue(foreground);

  unsigned char alpha = cdAlpha(foreground);

  unsigned char bRed = cdRed(background);
  unsigned char bGreen = cdGreen(background);
  unsigned char bBlue = cdBlue(background);

  unsigned char bAlpha = cdAlpha(background);

  const char* lineStyle = getLineStyle(ctxcanvas, cdCanvasLineStyle(ctxcanvas->canvas, CD_QUERY));

  int width = cdCanvasLineWidth(ctxcanvas->canvas, CD_QUERY);

  int backOpacity = cdCanvasBackOpacity(ctxcanvas->canvas, CD_QUERY);

  int interiorStyle = cdCanvasInteriorStyle(ctxcanvas->canvas, CD_QUERY);

  int hatchStyle = cdCanvasHatch(ctxcanvas->canvas, CD_QUERY);

  if (ctxcanvas->canvas->invert_yaxis)
    cdGetArcStartEnd(xc, yc, w, h, -a1, -a2, &arcStartX, &arcStartY, &arcEndX, &arcEndY);
  else
    cdGetArcStartEnd(xc, yc, w, h, a1, a2, &arcStartX, &arcStartY, &arcEndX, &arcEndY);

  angle1 = atan2(arcStartY - yc, arcStartX - xc)*CD_RAD2DEG;
  angle2 = atan2(arcEndY - yc, arcEndX - xc)*CD_RAD2DEG;

  if (angle1 < 0.)
    angle1 += 360.;

  if (angle2 < 0.)
    angle2 += 360.;

  if (angle2 < angle1)
  {
    double tmp = angle1;
    angle1 = angle2;
    angle2 = tmp;
  }

  pptxBeginSector(ctxcanvas->presentation, "pie", pxmin, pymin, abs(w), abs(h), (int)angle1, (int)angle2);

  setInteriorStyle(ctxcanvas, interiorStyle, hatchStyle, red, green, blue, alpha, bRed, bGreen, bBlue, bAlpha, backOpacity);

  pptxEndLine(ctxcanvas->presentation, width, red, green, blue, alpha, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
}

static void cdchord(cdCtxCanvas *ctxcanvas, int xc, int yc, int w, int h, double a1, double a2)
{
  int arcStartX, arcStartY, arcEndX, arcEndY;
  double angle1, angle2;

  int pxmin = xc - (w / 2);
  int pymin = yc - (h / 2);

  long foreground = cdCanvasForeground(ctxcanvas->canvas, CD_QUERY);
  long background = cdCanvasBackground(ctxcanvas->canvas, CD_QUERY);

  unsigned char red = cdRed(foreground);
  unsigned char green = cdGreen(foreground);
  unsigned char blue = cdBlue(foreground);

  unsigned char alpha = cdAlpha(foreground);

  unsigned char bRed = cdRed(background);
  unsigned char bGreen = cdGreen(background);
  unsigned char bBlue = cdBlue(background);

  unsigned char bAlpha = cdAlpha(background);

  const char* lineStyle = getLineStyle(ctxcanvas, cdCanvasLineStyle(ctxcanvas->canvas, CD_QUERY));

  int width = cdCanvasLineWidth(ctxcanvas->canvas, CD_QUERY);

  int backOpacity = cdCanvasBackOpacity(ctxcanvas->canvas, CD_QUERY);

  int interiorStyle = cdCanvasInteriorStyle(ctxcanvas->canvas, CD_QUERY);

  int hatchStyle = cdCanvasHatch(ctxcanvas->canvas, CD_QUERY);

  if (ctxcanvas->canvas->invert_yaxis)
    cdGetArcStartEnd(xc, yc, w, h, -a1, -a2, &arcStartX, &arcStartY, &arcEndX, &arcEndY);
  else
    cdGetArcStartEnd(xc, yc, w, h, a1, a2, &arcStartX, &arcStartY, &arcEndX, &arcEndY);

  angle1 = atan2(arcStartY - yc, arcStartX - xc)*CD_RAD2DEG;
  angle2 = atan2(arcEndY - yc, arcEndX - xc)*CD_RAD2DEG;

  if (angle1 < 0.)
    angle1 += 360.;

  if (angle2 < 0.)
    angle2 += 360.;

  if (angle2 < angle1)
  {
    double tmp = angle1;
    angle1 = angle2;
    angle2 = tmp;
  }

  pptxBeginSector(ctxcanvas->presentation, "chord", pxmin, pymin, abs(w), abs(h), (int)angle1, (int)angle2);

  setInteriorStyle(ctxcanvas, interiorStyle, hatchStyle, red, green, blue, alpha, bRed, bGreen, bBlue, bAlpha, backOpacity);

  pptxEndLine(ctxcanvas->presentation, width, red, green, blue, alpha, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
}

static void cdtext(cdCtxCanvas *ctxcanvas, int x, int y, const char *text, int len)
{
  char typeface[1024];
  int bold = 0;
  int italic = 0;
  int strikeout = 0;
  int underline = 0;
  int style, size;
  int xmin, xmax, ymin, ymax;
  int px, py;
  int width, height;

  long foreground = cdCanvasForeground(ctxcanvas->canvas, CD_QUERY);

  unsigned char red = cdRed(foreground);
  unsigned char green = cdGreen(foreground);
  unsigned char blue = cdBlue(foreground);

  unsigned char alpha = cdAlpha(foreground);

  cdCanvasGetFont(ctxcanvas->canvas, typeface, &style, &size);

  if (style&CD_BOLD)
    bold = 1;

  if (style&CD_ITALIC)
    italic = 1;

  if (style&CD_STRIKEOUT)
    strikeout = 1;

  if (style&CD_UNDERLINE)
    underline = 1;

  double rotAngle = ctxcanvas->canvas->text_orientation;

  ctxcanvas->canvas->text_orientation = 0.;

  char *str = cdStrDupN(text, len);

  cdCanvasGetTextBox(ctxcanvas->canvas, x, y, str, &xmin, &xmax, &ymin, &ymax);

  px = xmin;
  py = ymin;

  ctxcanvas->canvas->text_orientation = rotAngle;
  rotAngle *= -1;

  width = (xmax - xmin) + 1;
  height = (ymax - ymin) + 1;

  int line_height, ascent, baseline;
  cdCanvasGetFontDim(ctxcanvas->canvas, NULL, &line_height, &ascent, NULL);
  baseline = line_height - ascent;

  switch (ctxcanvas->canvas->text_alignment)
  {
  case CD_BASE_LEFT:
  case CD_BASE_CENTER:
  case CD_BASE_RIGHT:
    py -= baseline;
    py -= baseline;
    break;
  case CD_SOUTH_EAST:
  case CD_SOUTH:
  case CD_SOUTH_WEST:
    py -= height / 2;
    py -= height / 2;
    break;
  case CD_NORTH_EAST:
  case CD_NORTH:
  case CD_NORTH_WEST:
    py += height / 2;
    py += height / 2;
    break;
  }

  cdglStrConvertToUTF8(ctxcanvas, str, len);

  pptxText(ctxcanvas->presentation, px, py, width, height, rotAngle, bold, italic, underline, strikeout, size, red, green, blue, alpha,
           typeface, ctxcanvas->utf8_buffer);

  free(str);
}

static int cdfont(cdCtxCanvas *ctxcanvas, const char *type_face, int style, int size)
{
  /* dummy - must be defined */
  (void)ctxcanvas;
  (void)type_face;
  (void)style;
  (void)size;
  return 1;
}

static void getPolyBBox(cdCtxCanvas *ctxcanvas, cdPoint* poly, int n, int mode, int *xmin, int *xmax, int *ymin, int *ymax)
{
  int x, y, i;

#define _BBOX()               \
  if (x > *xmax) *xmax = x;   \
  if (y > *ymax) *ymax = y;   \
  if (x < *xmin) *xmin = x;   \
  if (y < *ymin) *ymin = y;

  *xmin = poly[0].x;
  *xmax = poly[0].x;
  *ymin = poly[0].y;
  *ymax = poly[0].y;

  if (mode == CD_PATH)
  {
    int p;
    i = 0;
    for (p = 0; p<ctxcanvas->canvas->path_n; p++)
    {
      switch (ctxcanvas->canvas->path[p])
      {
      case CD_PATH_MOVETO:
        if (i + 1 > n) return;
        x = poly[i].x;
        y = poly[i].y;
        _BBOX();
        i++;
        break;
      case CD_PATH_LINETO:
        if (i + 1 > n) return;
        x = poly[i].x;
        y = poly[i].y;
        _BBOX();
        i++;
        break;
      case CD_PATH_ARC:
      {
        int xc, yc, w, h;
        double a1, a2;
        int xmn, xmx, ymn, ymx;

        if (i + 3 > n) return;

        if (!cdGetArcPath(poly + i, &xc, &yc, &w, &h, &a1, &a2))
          return;

        cdGetArcBox(xc, yc, w, h, a1, a2, &xmn, &xmx, &ymn, &ymx);

        x = xmn;
        y = ymn;
        _BBOX();
        x = xmx;
        y = ymn;
        _BBOX();
        x = xmx;
        y = ymx;
        _BBOX();
        x = xmn;
        y = ymx;
        _BBOX();


        i += 3;
        break;
      }
      case CD_PATH_CURVETO:
        if (i + 3 > n) return;

        x = poly[i].x;
        y = poly[i].y;
        _BBOX();
        x = poly[i + 1].x;
        y = poly[i + 1].y;
        _BBOX();
        x = poly[i + 2].x;
        y = poly[i + 2].y;
        _BBOX();

        i += 3;
        break;
      default:
        break;
      }
    }
  }
  else if (mode == CD_BEZIER)
  {
    x = poly[0].x;
    y = poly[0].y;
    _BBOX();
    for (i = 1; i < n; i += 3)
    {
      x = poly[i].x;
      y = poly[i].y;
      _BBOX();
      x = poly[i + 1].x;
      y = poly[i + 1].y;
      _BBOX();
      x = poly[i + 2].x;
      y = poly[i + 2].y;
      _BBOX();
    }
  }
  else
  {
    for (i = 0; i < n; i++)
    {
      x = poly[i].x;
      y = poly[i].y;
      _BBOX();
    }
  }
}

static void cdpoly(cdCtxCanvas *ctxcanvas, int mode, cdPoint* poly, int n)
{
  int xmin, xmax, ymin, ymax, i;

  long foreground = cdCanvasForeground(ctxcanvas->canvas, CD_QUERY);

  unsigned char red = cdRed(foreground);
  unsigned char green = cdGreen(foreground);
  unsigned char blue = cdBlue(foreground);

  unsigned char alpha = cdAlpha(foreground);

  unsigned char bRed = cdRed(foreground);
  unsigned char bGreen = cdGreen(foreground);
  unsigned char bBlue = cdBlue(foreground);

  unsigned char bAlpha = cdAlpha(foreground);

  const char* lineStyle = getLineStyle(ctxcanvas, cdCanvasLineStyle(ctxcanvas->canvas, CD_QUERY));

  int width = cdCanvasLineWidth(ctxcanvas->canvas, CD_QUERY);

  int backOpacity = cdCanvasBackOpacity(ctxcanvas->canvas, CD_QUERY);

  int interiorStyle = cdCanvasInteriorStyle(ctxcanvas->canvas, CD_QUERY);

  int hatchStyle = cdCanvasHatch(ctxcanvas->canvas, CD_QUERY);

  if (mode == CD_CLIP)
    return;

  getPolyBBox(ctxcanvas, poly, n, mode, &xmin, &xmax, &ymin, &ymax);

  if (mode == CD_PATH)
  {
    int p;

    pptxBeginLine(ctxcanvas->presentation, xmin, ymin, (xmax - xmin) + 1, (ymax - ymin) + 1);

    i = 0;
    for (p = 0; p<ctxcanvas->canvas->path_n; p++)
    {
      switch (ctxcanvas->canvas->path[p])
      {
      case CD_PATH_NEW:
        break;
      case CD_PATH_MOVETO:
        if (i + 1 > n) return;
        pptxMoveTo(ctxcanvas->presentation, poly[i].x - xmin, poly[i].y - ymin);
        i++;
        break;
      case CD_PATH_LINETO:
        if (i + 1 > n) return;
        pptxLineTo(ctxcanvas->presentation, poly[i].x - xmin, poly[i].y - ymin);
        i++;
        break;
      case CD_PATH_ARC:
      {
        int xc, yc, w, h;
        double a1, a2, angle1, angle2;
        int arcStartX, arcStartY, arcEndX, arcEndY;

        if (i + 3 > n) return;

        if (!cdGetArcPath(poly + i, &xc, &yc, &w, &h, &a1, &a2))
          return;

        i += 3;

        if (ctxcanvas->canvas->invert_yaxis)
          cdGetArcStartEnd(xc, yc, w, h, -a1, -a2, &arcStartX, &arcStartY, &arcEndX, &arcEndY);
        else
          cdGetArcStartEnd(xc, yc, w, h, a1, a2, &arcStartX, &arcStartY, &arcEndX, &arcEndY);

        angle1 = atan2(arcStartY - yc, arcStartX - xc)*CD_RAD2DEG;
        angle2 = atan2(arcEndY - yc, arcEndX - xc)*CD_RAD2DEG;
        angle2 -= angle1;

        pptxLineTo(ctxcanvas->presentation, (int)arcStartX - xmin, (int)arcStartY - ymin);

        pptxArcTo(ctxcanvas->presentation, h / 2, w / 2, angle1, angle2);
        break;
      }
      case CD_PATH_CURVETO:
        if (i + 3 > n) return;
        pptxBezierLineTo(ctxcanvas->presentation, poly[i].x - xmin, poly[i].y - ymin,
                         poly[i + 1].x - xmin, poly[i + 1].y - ymin,
                         poly[i + 2].x - xmin, poly[i + 2].y - ymin);
        i += 3;
        break;
      case CD_PATH_CLOSE:
        pptxLineTo(ctxcanvas->presentation, poly[0].x - xmin, poly[0].y - ymin);
        pptxLineClose(ctxcanvas->presentation);
        pptxEndLine(ctxcanvas->presentation, width, red, green, blue, 0, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
        break;
      case CD_PATH_FILL:
        pptxLineClose(ctxcanvas->presentation);
        setInteriorStyle(ctxcanvas, interiorStyle, hatchStyle, red, green, blue, alpha,
                         bRed, bGreen, bBlue, bAlpha, backOpacity);
        pptxEndLine(ctxcanvas->presentation, width, red, green, blue, 0, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
        break;
      case CD_PATH_STROKE:
        pptxLineClose(ctxcanvas->presentation);
        pptxEndLine(ctxcanvas->presentation, width, red, green, blue, 0, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
        break;
      case CD_PATH_FILLSTROKE:
        pptxLineClose(ctxcanvas->presentation);
        setInteriorStyle(ctxcanvas, interiorStyle, hatchStyle, red, green, blue, alpha,
                         bRed, bGreen, bBlue, bAlpha, backOpacity);
        pptxEndLine(ctxcanvas->presentation, width, red, green, blue, 255, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
        break;
      case CD_PATH_CLIP:
        break;
      }
    }
    return;
  }

  pptxBeginLine(ctxcanvas->presentation, xmin, ymin, (xmax - xmin) + 1, (ymax - ymin) + 1);

  pptxMoveTo(ctxcanvas->presentation, poly[0].x - xmin, poly[0].y - ymin);

  if (mode == CD_BEZIER)
  {
    for (i = 1; i < n; i += 3)
      pptxBezierLineTo(ctxcanvas->presentation, poly[i].x - xmin, poly[i].y - ymin,
      poly[i + 1].x - xmin, poly[i + 1].y - ymin, poly[i + 2].x - xmin,
      poly[i + 2].y - ymin);
  }
  else
  {
    for (i = 1; i < n; i++)
      pptxLineTo(ctxcanvas->presentation, poly[i].x - xmin, poly[i].y - ymin);

    if (mode == CD_CLOSED_LINES)
      pptxLineTo(ctxcanvas->presentation, poly[0].x - xmin, poly[0].y - ymin);
  }

  pptxLineClose(ctxcanvas->presentation);

  switch (mode)
  {
  case CD_CLOSED_LINES:
  case CD_OPEN_LINES:
  case CD_BEZIER:
    pptxNoFill(ctxcanvas->presentation);
    break;
  case CD_FILL:
    setInteriorStyle(ctxcanvas, interiorStyle, hatchStyle, red, green, blue, alpha,
                       bRed, bGreen, bBlue, bAlpha, backOpacity);
    alpha = 0;
    break;
  }

  pptxEndLine(ctxcanvas->presentation, width, red, green, blue, alpha, lineStyle, ctxcanvas->nDashes, ctxcanvas->dashes);
}

static void cdputimagerectmap(cdCtxCanvas *ctxcanvas, int iw, int ih, const unsigned char *index, const long int *colors, int x, int y, int w, int h, int xmin, int xmax, int ymin, int ymax)
{
  pptxImageMap(ctxcanvas->presentation, iw, ih, index, colors, x, y, w, h, xmin, xmax, ymin, ymax);
}

static void cdputimagerectrgb(cdCtxCanvas *ctxcanvas, int iw, int ih, const unsigned char *r, const unsigned char *g, const unsigned char *b, int x, int y, int w, int h, int xmin, int xmax, int ymin, int ymax)
{
  pptxImageRGBA(ctxcanvas->presentation, iw, ih, r, g, b, NULL, x, y, w, h, xmin, xmax, ymin, ymax);
}

static void cdputimagerectrgba(cdCtxCanvas *ctxcanvas, int iw, int ih, const unsigned char *r, const unsigned char *g, const unsigned char *b, const unsigned char *a, int x, int y, int w, int h, int xmin, int xmax, int ymin, int ymax)
{
  pptxImageRGBA(ctxcanvas->presentation, iw, ih, r, g, b, a, x, y, w, h, xmin, xmax, ymin, ymax);
}

static void cdpixel(cdCtxCanvas *ctxcanvas, int x, int y, long int color)
{
  pptxPixel(ctxcanvas->presentation, x, y, 1, cdRed(color), cdGreen(color), cdBlue(color), cdAlpha(color));
}


/*******************/
/* Canvas Creation */
/*******************/

static void set_utf8mode_attrib(cdCtxCanvas* ctxcanvas, char* data)
{
  if (!data || data[0] == '0')
    ctxcanvas->utf8mode = 0;
  else
    ctxcanvas->utf8mode = 1;
}

static char* get_utf8mode_attrib(cdCtxCanvas* ctxcanvas)
{
  if (ctxcanvas->utf8mode)
    return "1";
  else
    return "0";
}

static cdAttribute utf8mode_attrib =
{
  "UTF8MODE",
  set_utf8mode_attrib,
  get_utf8mode_attrib
};

static void cdcreatecanvas(cdCanvas *canvas, void *data)
{
  char filename[10240] = "";
  char* strdata = (char*)data;
  double res = 3.78;
  double w_mm = INT_MAX*res,
         h_mm = INT_MAX*res;
  cdCtxCanvas* ctxcanvas;

  strdata += cdGetFileName(strdata, filename);
  if (filename[0] == 0)
    return;

  ctxcanvas = (cdCtxCanvas *)malloc(sizeof(cdCtxCanvas));
  memset(ctxcanvas, 0, sizeof(cdCtxCanvas));

  sscanf(strdata, "%lgx%lg %lg", &w_mm, &h_mm, &res);
  canvas->w = (int)(w_mm * res);
  canvas->h = (int)(h_mm * res);
  canvas->w_mm = w_mm;
  canvas->h_mm = h_mm;
  canvas->xres = res;
  canvas->yres = res;

  /* top-down orientation */
  canvas->invert_yaxis = 1;

  canvas->bpp = 24;

  ctxcanvas->presentation = pptxCreatePresentation(canvas->w_mm, canvas->h_mm, canvas->w, canvas->h);
  if (!ctxcanvas->presentation)
  {
    free(ctxcanvas);
    return;
  }

  ctxcanvas->nDashes = 0;
  ctxcanvas->dashes = NULL;
#ifndef WIN32
  ctxcanvas->cdgl_iconv = iconv_open("UTF-8", "ISO-8859-1");
#endif

  strcpy(ctxcanvas->filename, filename);

  /* store the base canvas */
  ctxcanvas->canvas = canvas;
  canvas->ctxcanvas = ctxcanvas;

  cdRegisterAttribute(canvas, &utf8mode_attrib);
}

static void cdinittable(cdCanvas* canvas)
{
  canvas->cxFlush = cdflush;
  canvas->cxClip = cdclip;
  canvas->cxPixel = cdpixel;
  canvas->cxLine = cdline;
  canvas->cxPoly = cdpoly;
  canvas->cxRect = cdrect;
  canvas->cxBox = cdbox;
  canvas->cxArc = cdarc;
  canvas->cxSector = cdsector;
  canvas->cxChord = cdchord;
  canvas->cxText = cdtext;
  canvas->cxFont = cdfont;
  canvas->cxPutImageRectMap = cdputimagerectmap;
  canvas->cxPutImageRectRGB = cdputimagerectrgb;
  canvas->cxPutImageRectRGBA = cdputimagerectrgba;

  canvas->cxKillCanvas = cdkillcanvas;
}

static cdContext cdPPTXContext =
{
  CD_CAP_ALL & ~(CD_CAP_CLEAR | CD_CAP_GETIMAGERGB | CD_CAP_IMAGESRV | CD_CAP_PLAY | CD_CAP_PALETTE | CD_CAP_YAXIS |
  CD_CAP_REGION | CD_CAP_WRITEMODE | CD_CAP_FONTDIM | CD_CAP_TEXTSIZE),
  CD_CTX_FILE,
  cdcreatecanvas,
  cdinittable,
  NULL,
  NULL,
};

cdContext* cdContextPPTX(void)
{
  return &cdPPTXContext;
}