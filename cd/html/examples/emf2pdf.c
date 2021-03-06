#include <stdlib.h>
#include <stdio.h>

#include "cd.h"
#include "cdemf.h"
#include "cdpdf.h"


int main(int argc, char** argv)
{
  const char* emf_filename;
  const char* pdf_filename;
  int pdf_w, pdf_h;
  cdCanvas* pdf_canvas;
  
  if (argc < 3)
  {
    printf("Missing parameters: emf_filename pdf_filename\n");
    return 1;
  }
  
  emf_filename = argv[1];
  pdf_filename = argv[2];
  
  //use A4 paper, but could be CD_LETTER; use 600 DPI
//  pdf_canvas = cdCreateCanvasf(CD_PDF, "%s -p%d -s%d", pdf_filename, CD_A4, 600);
  pdf_canvas = cdCreateCanvasf(CD_PDF, "%s -w270 -h195 -s120", pdf_filename);
  if (pdf_canvas == NULL)
  {
    printf("CreateCanvas(CD_PDF) - Failed!\n");
    return 1;
  }

  cdCanvasGetSize(pdf_canvas, &pdf_w, &pdf_h, NULL, NULL);

  cdCanvasPlay(pdf_canvas, CD_EMF, 0, pdf_w-1, 0, pdf_h-1, (void*)emf_filename);

  cdKillCanvas(pdf_canvas);

  return 0;
}
