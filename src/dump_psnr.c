/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggTheora SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE Theora SOURCE CODE IS COPYRIGHT (C) 2002-2009                *
 * by the Xiph.Org Foundation and contributors http://www.xiph.org/ *
 *                                                                  *
 ********************************************************************

  function: example dumpvid application; dumps Theora streams
  last mod: $Id$

 ********************************************************************/

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if !defined(_LARGEFILE_SOURCE)
#define _LARGEFILE_SOURCE
#endif
#if !defined(_LARGEFILE64_SOURCE)
#define _LARGEFILE64_SOURCE
#endif
#if !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif

#include <stdio.h>
#if !defined(_WIN32)
#include <getopt.h>
#include <unistd.h>
#else
#include "getopt.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/stat.h>
/*Yes, yes, we're going to hell.*/
#if defined(_WIN32)
#include <io.h>
#endif
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include "vidinput.h"

const char *optstring = "fsya";
struct option options [] = {
  {"frame-type",no_argument,NULL,'f'},
  {"summary",no_argument,NULL,'s'},
  {"luma-only",no_argument,NULL,'y'},
  {"frame-averaged",no_argument,NULL,'a'},
  {NULL,0,NULL,0}
};

static int show_frame_type;
static int summary_only;
static int luma_only;
static int frame_averaged;

static void usage(char *_argv[]){
  fprintf(stderr,"Usage: %s [options] <video1> <video2>\n"
   "    <video1> and <video2> must be YUV4MPEG files.\n\n"
   "    Options:\n\n"
   "      -f --frame-type     Show QI value for each frame.\n"
   "      -s --summary        Only output the summary line.\n"
   "      -y --luma-only      Only output values for the luma channel.\n"
   "      -a --frame-averaged Average frame PSNR values together",_argv[0]);
}

int main(int _argc,char *_argv[]){
  video_input  vid1;
  video_input_info info1;
  video_input  vid2;
  video_input_info info2;
  int64_t  gsqerr;
  int64_t  gnpixels;
  int64_t  gplsqerr[3];
  int64_t  gplnpixels[3];
  int samplemax;
  double psnrsum;
  double plpsnrsum[3];
  int          frameno;
  FILE        *fin;
  int          long_option_index;
  int          c;
#ifdef _WIN32
  /*We need to set stdin/stdout to binary mode on windows.
    Beware the evil ifdef.
    We avoid these where we can, but this one we cannot.
    Don't add any more, you'll probably go to hell if you do.*/
  _setmode(_fileno(stdin),_O_BINARY);
#endif
  /*Process option arguments.*/
  while((c=getopt_long(_argc,_argv,optstring,options,&long_option_index))!=EOF){
    switch(c){
      case 'f':show_frame_type=1;break;
      case 's':summary_only=1;break;
      case 'y':luma_only=1;break;
      case 'a':frame_averaged=1;break;
      default:usage(_argv);break;
    }
  }
  if(optind+2!=_argc){
    usage(_argv);
    exit(1);
  }
  fin=strcmp(_argv[optind],"-")==0?stdin:fopen(_argv[optind],"rb");
  if(fin==NULL){
    fprintf(stderr,"Unable to open '%s' for extraction.\n",_argv[optind]);
    exit(1);
  }
  fprintf(stderr,"Opening %s...\n",_argv[optind]);
  if(video_input_open(&vid1,fin)<0)exit(1);
  video_input_get_info(&vid1,&info1);
  fin=strcmp(_argv[optind+1],"-")==0?stdin:fopen(_argv[optind+1],"rb");
  if(fin==NULL){
    fprintf(stderr,"Unable to open '%s' for extraction.\n",_argv[optind+1]);
    exit(1);
  }
  fprintf(stderr,"Opening %s...\n",_argv[optind+1]);
  if(video_input_open(&vid2,fin)<0)exit(1);
  video_input_get_info(&vid2,&info2);
  /*Check to make sure these videos are compatible.*/
  if(info1.pic_w!=info2.pic_w||info1.pic_h!=info2.pic_h){
    fprintf(stderr,"Video resolution does not match.\n");
    exit(1);
  }
  if(info1.pixel_fmt!=info2.pixel_fmt){
    fprintf(stderr,"Pixel formats do not match.\n");
    exit(1);
  }
  if(info1.depth!=info2.depth){
    fprintf(stderr,"Depth does not match.\n");
    exit(1);
  }
  if(info1.depth > 16){
    fprintf(stderr,"Sample depths above 16 are not supported.\n");
    exit(1);
  }
  if((info1.pic_x&!(info1.pixel_fmt&1))!=(info2.pic_x&!(info2.pixel_fmt&1))||
   (info1.pic_y&!(info1.pixel_fmt&2))!=(info2.pic_y&!(info2.pixel_fmt&2))){
    fprintf(stderr,"Chroma subsampling offsets do not match.\n");
    exit(1);
  }
  if(info1.fps_n*(int64_t)info2.fps_d!=
   info2.fps_n*(int64_t)info1.fps_d){
    fprintf(stderr,"Warning: framerates do not match.\n");
  }
  if(info1.par_n*(int64_t)info2.par_d!=
   info2.par_n*(int64_t)info1.par_d){
    fprintf(stderr,"Warning: aspect ratios do not match.\n");
  }
  samplemax = (1 << info1.depth) - 1;
  gsqerr=gplsqerr[0]=gplsqerr[1]=gplsqerr[2]=0;
  gnpixels=gplnpixels[0]=gplnpixels[1]=gplnpixels[2]=0;
  psnrsum = 0;
  plpsnrsum[0] = 0;
  plpsnrsum[1] = 0;
  plpsnrsum[2] = 0;
  for(frameno=0;;frameno++){
    video_input_ycbcr f1;
    video_input_ycbcr f2;
    int64_t     plsqerr[3];
    long            plnpixels[3];
    int64_t     sqerr;
    long            npixels;
    double plpsnr[3];
    double psnr;
    int             ret1;
    int             ret2;
    int             pli;
    int             nplanes;
    ret1=video_input_fetch_frame(&vid1,f1,NULL);
    ret2=video_input_fetch_frame(&vid2,f2,NULL);
    if(ret1==0&&ret2==0)break;
    else if(ret1<0||ret2<0)break;
    else if(ret1==0){
      fprintf(stderr,"%s ended before %s.\n",
       _argv[optind],_argv[optind+1]);
      break;
    }
    else if(ret2==0){
      fprintf(stderr,"%s ended before %s.\n",
       _argv[optind+1],_argv[optind]);
      break;
    }
    /*Okay, we got one frame from each.*/
    sqerr=0;
    npixels=0;
    nplanes = luma_only ? 1 : 3;
    for(pli=0;pli<nplanes;pli++){
      int xdec;
      int ydec;
      int y1;
      int y2;
      xdec=pli&&!(info1.pixel_fmt&1);
      ydec=pli&&!(info1.pixel_fmt&2);
      plsqerr[pli]=0;
      plnpixels[pli]=0;
      for (y1 = info1.pic_y >> ydec, y2 = info2.pic_y >> ydec;
       y1 < (info1.pic_y + info1.pic_h + ydec) >> ydec; y1++, y2++) {
        int x1;
        int x2;
        for (x1 = info1.pic_x >> xdec, x2 = info2.pic_x >> xdec;
         x1 < (info1.pic_x + info1.pic_w + xdec) >> xdec; x1++, x2++) {
          int d;
          if (info1.depth > 8) {
            d=*(f1[pli].data+y1*f1[pli].stride + x1*2) +
             (*(f1[pli].data+y1*f1[pli].stride + x1*2 + 1) << 8) -
             *(f2[pli].data+y2*f2[pli].stride + x2*2) -
             (*(f2[pli].data+y2*f2[pli].stride + x2*2 + 1) << 8);
          } else {
            d=*(f1[pli].data+y1*f1[pli].stride+x1)-
             *(f2[pli].data+y2*f2[pli].stride+x2);
          }
          plsqerr[pli]+=d*d;
          plnpixels[pli]++;
        }
      }
      sqerr+=plsqerr[pli];
      gplsqerr[pli]+=plsqerr[pli];
      npixels+=plnpixels[pli];
      gplnpixels[pli]+=plnpixels[pli];
      plpsnr[pli] =
       10*(log10(samplemax*samplemax) +
        log10(plnpixels[pli]) - log10(plsqerr[pli]));
    }
    psnr = 10*(log10(samplemax*samplemax) + log10(npixels) - log10(sqerr));
    if(!summary_only){
      if(!luma_only){
        printf("%08i: %-7G  (Y': %-7G  Cb: %-7G  Cr: %-7G)\n",frameno,
         psnr,plpsnr[0],plpsnr[1],plpsnr[2]);
      }
      else{
        printf("%08i: %-7G\n",frameno,plpsnr[0]);
      }
    }
    gsqerr+=sqerr;
    gnpixels+=npixels;
    psnrsum+=psnr;
    plpsnrsum[0]+=plpsnr[0];
    plpsnrsum[1]+=plpsnr[1];
    plpsnrsum[2]+=plpsnr[2];
  }
  if(!luma_only){
    printf("Total: %-7G  (Y': %-7G  Cb: %-7G  Cr: %-7G)\n",
     10*(log10(samplemax*samplemax)+log10(gnpixels)-log10(gsqerr)),
     10*(log10(samplemax*samplemax)+log10(gplnpixels[0])-log10(gplsqerr[0])),
     10*(log10(samplemax*samplemax)+log10(gplnpixels[1])-log10(gplsqerr[1])),
     10*(log10(samplemax*samplemax)+log10(gplnpixels[2])-log10(gplsqerr[2])));
  }
  else{
    printf("Total: %-7G\n",
     10*(log10(samplemax*samplemax)+log10(gplnpixels[0])-log10(gplsqerr[0])));
  }
  if(frame_averaged){
    if(!luma_only){
      printf("Frame-averaged: %-7G  (Y': %-7G  Cb: %-7G  Cr: %-7G)\n",
       psnrsum/frameno,plpsnrsum[0]/frameno,plpsnrsum[1]/frameno,
       plpsnrsum[2]/frameno);
    }
    else{
      printf("Frame-averaged: %-7G\n",plpsnrsum[0]/frameno);
    }
  }
  video_input_close(&vid1);
  video_input_close(&vid2);
  return 0;
}
