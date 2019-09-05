/* animspheres.c 
 * This file contains a demo program and driver for the raytracer.
 *
 *  $Id: animspheres.c,v 1.16 2011/01/31 15:26:14 johns Exp $
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tachyon.h"
#include "glwin.h"

#if defined(USEOPENGL)
#include <GL/gl.h>
#endif

int rt_mynode(void); /* proto */

/* Number of frames to render */
#define MAXFRAMES 400

/* Number of bouncing spheres */
#define NUMSP 16 

/* NTSC Resolution */
/* #define XRES 640 
   #define YRES 480
*/

/* MPEG-1 Resolution */
#define XRES 352
#define YRES 240

#define MAXX 1.0 
#define MAXY 1.0 
#define MAXZ 1.0 

#define MINX -1.0
#define MINY -1.0
#define MINZ -1.0

#define LOOP 200.0
#define LOOP2 100.0
#define RAD 6.28

typedef struct {
     apitexture tex;
      apivector ctr;
         apiflt rad; 
      apivector dir;
     void * voidtex;
} asphere;

asphere sp[NUMSP];

apiflt randflt(void) {
  long a;
  apiflt f;
 
  a=rand() % 1000;

  f=(a*1.0) / 1000.0;
  return f; 
}


void initspheres(void) {
  int i;
  apiflt t1;

  for (i=0; i<NUMSP; i++) {
   sp[i].tex.col.r=randflt() / 3.0 + 0.66;         
   sp[i].tex.col.g=randflt() / 3.0 + 0.66;         
   sp[i].tex.col.b=randflt() / 3.0 + 0.66;         
   t1=randflt()*0.9;
   
   sp[i].tex.ambient=0.1;
   sp[i].tex.diffuse=t1;
   sp[i].tex.specular=0.9 - t1;
   sp[i].tex.opacity=1.0;
   
   sp[i].tex.scale.x=1.0;
   sp[i].tex.scale.y=1.0;
   sp[i].tex.scale.z=1.0;

   sp[i].tex.rot.x=0.0;
   sp[i].tex.rot.y=0.0;
   sp[i].tex.rot.z=0.0;
   sp[i].tex.texturefunc=rand() % 7;

   sp[i].ctr.x=randflt() * 2.0 - 1.0;
   sp[i].ctr.y=randflt() * 2.0 - 1.0;
   sp[i].ctr.z=randflt() * 2.0 - 1.0;

   sp[i].rad=randflt()*0.5 + 0.05;
   
   sp[i].dir.x=randflt() * 0.05 - 0.02; 
   sp[i].dir.y=randflt() * 0.05 - 0.02; 
   sp[i].dir.z=randflt() * 0.05 - 0.02; 
  }
}

void movesp(void) {
  int i;

  for (i=0; i<NUMSP; i++) {
    sp[i].ctr.x += sp[i].dir.x;
    sp[i].ctr.y += sp[i].dir.y;
    sp[i].ctr.z += sp[i].dir.z;
 
    if (sp[i].ctr.x > MAXX) {
      sp[i].ctr.x = MAXX;
      sp[i].dir.x = -sp[i].dir.x;
    }
    if (sp[i].ctr.x < MINX) {
      sp[i].ctr.x = MINX;
      sp[i].dir.x = -sp[i].dir.x;
    }

    if (sp[i].ctr.y > MAXY) {
      sp[i].ctr.y = MAXY;
      sp[i].dir.y = -sp[i].dir.y;
    }
    if (sp[i].ctr.y < MINY) {
      sp[i].ctr.y = MINY;
      sp[i].dir.y = -sp[i].dir.y;
    }

    if (sp[i].ctr.z > MAXZ) {
      sp[i].ctr.z = MAXZ;
      sp[i].dir.z = -sp[i].dir.z;
    }
    if (sp[i].ctr.z < MINZ) {
      sp[i].ctr.z = MINZ;
      sp[i].dir.z = -sp[i].dir.z;
    }
    sp[i].tex.ctr.x = sp[i].dir.x;
    sp[i].tex.ctr.y = sp[i].dir.y;
    sp[i].tex.ctr.z = sp[i].dir.z;
  }
}

void drawsp(SceneHandle scene) {
  int i;
  apitexture p1;  
  apivector ct1, n1;

  for (i=0; i<NUMSP; i++) {
    sp[i].voidtex = rt_texture(scene, &sp[i].tex);
    rt_sphere(scene, sp[i].voidtex, sp[i].ctr, sp[i].rad);   
  }

  p1.col.r=1.0;
  p1.col.g=1.0;
  p1.col.b=1.0;
  p1.ambient=0.1;
  p1.diffuse=0.5;
  p1.specular=0.4;
  p1.opacity=1.0;
 
  ct1.x=0.0;
  ct1.y=-1.10;
  ct1.z=0.0;

  n1.x=0.0;
  n1.y=1.0;
  n1.z=0.0;
 
  rt_plane(scene, rt_texture(scene, &p1), ct1, n1);  
}

int main(int argc, char **argv) {
  SceneHandle scene;
  int i, xres, yres, maxframes;
  apivector Ccenter, Cview, Cup;
  apivector ctr1, ctr2;
  apitexture tex1, tex2;
  void * vtx1, * vtx2;
  char fname[100];
  int nosave, opengl;
  void *glwin = NULL;
  unsigned char *img = NULL;

  rt_initialize(&argc, &argv); 

  nosave=0;
  opengl=0;
  xres=XRES;
  yres=XRES;
  maxframes=MAXFRAMES;
  for (i=1; i<argc; i++) {
    if (!strcmp("-res", argv[i])) {
      if (i+2<argc) {
        i++;
        sscanf(argv[i], "%d", &xres);
        i++;
        sscanf(argv[i], "%d", &xres);
      }
      continue;
    }
    if (!strcmp("-frames", argv[i])) {
      if (i+1<argc) {
        i++;
        sscanf(argv[i], "%d", &maxframes);
      }
      continue;
    }
    if (!strcmp("-nosave", argv[i])) {
      nosave=1;
      continue;
    }
    if (!strcmp("-opengl", argv[i])) {
      opengl=1; 
      continue;
    }
  }

  if (opengl) {
    img = (unsigned char *) calloc(1, xres*yres*3); 
    if (img) 
      glwin = glwin_create(argv[0], xres, yres);
  }

  Ccenter.x=0.0; Ccenter.y=0.0; Ccenter.z=-3.0;
  Cview.x=0.0;   Cview.y=0.0;   Cview.z=1.0;
  Cup.x=0.0;     Cup.y=1.0;     Cup.z=0.0;

  ctr1.x=20.0;  ctr1.y=20.0; ctr1.z=-40.0;
  ctr2.x=-20.0; ctr2.y=20.0; ctr2.z=-40.0;
  
  tex1.col.r=1.0;
  tex1.col.g=0.5;
  tex1.col.b=0.0;
  tex1.ambient=1.0;
  tex1.opacity=1.0;
  tex2=tex1;
  tex2.col.r=0.0;
  tex2.col.b=1.0;

  initspheres();
 
  for (i=0; i<MAXFRAMES; i++) {  
    scene = rt_newscene();
    vtx1=rt_texture(scene, &tex1);
    vtx2=rt_texture(scene, &tex2);


    if (!nosave) {
      sprintf(fname,"outfile.%4.4d.tga",i);
      if (rt_mynode()==0) printf("Rendering: %s\n", fname);
      rt_outputfile(scene, fname);
    } else {
      printf("\rRendering %d...           ", i);
      fflush(stdout);  
    }

    if (img != NULL)
      rt_rawimage_rgb24(scene, img);

    rt_resolution(scene, xres, yres);
    rt_verbose(scene, 0);

    rt_camera_setup(scene, 1.0, 1.0, 0, 5, Ccenter, Cview, Cup);

    movesp(); 
    drawsp(scene);

    rt_light(scene, vtx1, ctr1, 1.0);
    rt_light(scene, vtx2, ctr2, 1.0);

    rt_renderscene(scene);

#ifdef USEOPENGL
    if (opengl) {
      float wscalex, wscaley, wminscale;
      float wxoffset, wyoffset;
      int wsx, wsy, instereo, maxx, maxy;

      glwin_get_wininfo(glwin, &instereo, NULL);
      glwin_get_winsize(glwin, &wsx, &wsy);
      maxx=xres;
      maxy=yres;
      wscalex = wsx / (float) maxx;
      wscaley = wsy / (float) maxy;
      wminscale = (wscalex < wscaley) ? wscalex : wscaley;
      wxoffset = ((wminscale * maxx) - wsx) / 2.0f;
      wyoffset = ((wminscale * maxy) - wsy) / 2.0f;

      glDrawBuffer(GL_BACK);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glClearColor(0.0, 0.0, 0.0, 1.0); /* black */
      glViewport(0, 0, wsx, wsy);
      glClear(GL_COLOR_BUFFER_BIT);

      glShadeModel(GL_FLAT);
      glViewport((int) -wxoffset, (int) -wyoffset, wsx, wsy);
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glOrtho(0.0, wsx, 0.0, wsy, -1.0, 1.0); /* flip upside-down image */

      glMatrixMode(GL_MODELVIEW);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glPixelZoom(wminscale, wminscale); /* flip upside-down Tachyon image */

      glwin_draw_image(glwin, xres, yres, img);
    }
#endif

    rt_deletescene(scene);

#if 0
    for (j=0; j<NUMSP; j++)
      free(sp[i].voidtex);  
#endif
  }

  rt_finalize();

  if (opengl) {
    glwin_destroy(glwin);
    if (img) 
      free(img);
  }

  return 0;
}
   
