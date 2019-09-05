/*
 * spaceball.c - code to perform camera fly-throughs using a spaceball
 * 
 *  $Id: spaceball.c,v 1.6 2011/02/07 05:26:41 johns Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#if !defined(_MSC_VER)
#include <unistd.h>
#endif

#include "trackball.h"   /* quaternion code */
#include "tachyon.h"     /* main ray tracer api headers */
#include "spaceball.h"   /* protos for this file */
#include "glwin.h"       /* windowing system spaceball events */

#if defined(USESPACEBALL)
#include "sball.h"       /* spaceball code */
#endif

void * tachyon_init_spaceball(SceneHandle scene, void *glwin, char * serialport) {
  sbHandle * bh;

  bh = (sbHandle *) malloc(sizeof(sbHandle));
  if (bh == NULL) {
    return NULL;
  }

  memset(bh, 0, sizeof(sbHandle));
  bh->glwin = glwin; /* windowing system handle */
#if defined(USESPACEBALL)
  if (port != NULL) {
    bh->sball = sball_open(serialport);
    if (bh->sball == NULL) {
      free(bh);
      return NULL;
    }
  }
#endif

  rt_get_camera_position(scene, &bh->orig_camcent, &bh->orig_camviewvec, &bh->orig_camupvec, &bh->orig_camrightvec);

  bh->camcent = bh->orig_camcent;
  bh->camviewvec = bh->orig_camviewvec;
  bh->camupvec = bh->orig_camupvec;
  trackball(bh->curquat, 0.0, 0.0, 0.0, 0.0);
  trackball(bh->lastquat, 0.0, 0.0, 0.0, 0.0);

  return bh;
}


int tachyon_spaceball_update(sbHandle * bh, SceneHandle scene) {
  int tx, ty, tz, rx, ry, rz, buttons, buttonchanged;
  int win_event = 0;
  int direct_event = 0;

  float qq[4]={0.0, 0.0, 0.0, 1.0};
  float xx[3]={1.0, 0.0, 0.0};
  float yy[3]={0.0, 1.0, 0.0};
  float zz[3]={0.0, 0.0, 1.0};

  float m[4][4];
  float t[3];

  static float transdivisor = 5000.0;
  static float angdivisor = 20000.0;

  /* explicitly initialize event state variables */
  rx=ry=rz=tx=ty=tz=buttons=0;

  if (glwin_get_spaceball(bh->glwin, &rx, &ry, &rz, &tx, &ty, &tz, &buttons)) {
    win_event = 1;
  }

#if defined(USESPACEBALL)
  if (bh->sball != NULL) {
    int rx2, ry2, rz2, tx2, ty2, tz2, buttons2;
    if (sball_getstatus(bh->sball, &tx2, &ty2, &tz2, &rx2, &ry2, &rz2, &buttons2) {
      direct_event = 1;
      rx += rx2;
      ry += ry2;
      rz += rz2;
      tx += tx2;
      ty += ty2;
      tz += tz2;
      buttons |= buttons2;
    }
  }
#endif

  if (!win_event && !direct_event) {
    /* usleep(5); */ /* if no movement then sleep for a tiny bit.. */
    return 0; // no events to report
  }

  /* find which buttons changed state */
  buttonchanged = buttons ^ bh->buttondown;

  /* negate rotations given by spaceball */
  rx = -rx;
  ry = -ry;
  rz = -rz;

#if defined(USESPACEBALL)
  if ((buttonchanged & SBALL_BUTTON_PICK) && (buttons & SBALL_BUTTON_PICK)) ||
      (buttonchanged & SBALL_BUTTON_1) && (buttons & SBALL_BUTTON_1)) ||
      (buttonchanged & SBALL_BUTTON_LEFT) && (buttons & SBALL_BUTTON_LEFT)) ) {

#else
  if ((buttonchanged & 1)  && (buttons & 1)) {
#endif
    bh->curtrans[0] = 0.0;
    bh->curtrans[1] = 0.0;
    bh->curtrans[2] = 0.0;
    trackball(bh->curquat, 0.0, 0.0, 0.0, 0.0);
    trackball(bh->lastquat, 0.0, 0.0, 0.0, 0.0);
    transdivisor = 5000.0;
    angdivisor = 20000.0; 

    bh->camviewvec = bh->orig_camviewvec;
    bh->camupvec = bh->orig_camupvec;
    bh->camcent = bh->orig_camcent;
  }


#if defined(USESPACEBALL)
  if (buttons & SBALL_BUTTON_1) {
    transdivisor /= 1.2;
    angdivisor /= 1.2;
  }
  else if (buttons & SBALL_BUTTON_2) {
    transdivisor *= 1.2; 
    angdivisor *= 1.2;
  }

  if (buttons & SBALL_BUTTON_3)
    transdivisor /= 1.2;
  else if (buttons & SBALL_BUTTON_4)
    transdivisor *= 1.2; 

  if (buttons & SBALL_BUTTON_5) 
    angdivisor *= 1.2;
  else if (buttons & SBALL_BUTTON_6)
    angdivisor /= 1.2;

  if (buttons & SBALL_BUTTON_7) {
    return 1;  /* quit the fly through */
  }
#endif

  t[0] = tx / transdivisor; 
  t[1] = ty / transdivisor;
  t[2] = tz / transdivisor;

  /* 
   * convert rotations and translations from the
   * spaceball's coordinate frame into the camera's frame.
   */
  bh->newtrans[0] = 
    t[0] * bh->orig_camrightvec.x +
    t[1] * bh->orig_camupvec.x +
    t[2] * bh->orig_camviewvec.x;

  bh->newtrans[1] = 
    t[0] * bh->orig_camrightvec.y +
    t[1] * bh->orig_camupvec.y +
    t[2] * bh->orig_camviewvec.y;

  bh->newtrans[2] = 
    t[0] * bh->orig_camrightvec.z +
    t[1] * bh->orig_camupvec.z +
    t[2] * bh->orig_camviewvec.z;

  /* 
   * rotate around camera's coordinate frame
   */ 
  xx[0] = bh->orig_camrightvec.x;
  xx[1] = bh->orig_camrightvec.y;
  xx[2] = bh->orig_camrightvec.z;

  yy[0] = bh->orig_camupvec.x;
  yy[1] = bh->orig_camupvec.y;
  yy[2] = bh->orig_camupvec.z;

  zz[0] = bh->orig_camviewvec.x;
  zz[1] = bh->orig_camviewvec.y;
  zz[2] = bh->orig_camviewvec.z;

  /* do rotations */ 
  axis_to_quat(xx, rx / angdivisor, bh->lastquat);

  axis_to_quat(yy, ry / angdivisor, qq);
  add_quats(qq, bh->lastquat, bh->lastquat);

  axis_to_quat(zz, rz / angdivisor, qq);
  add_quats(qq, bh->lastquat, bh->lastquat);

  add_quats(bh->lastquat, bh->curquat, bh->curquat);

  build_rotmatrix(m, bh->curquat);

  /*
   * translate along the new axes
   */
  t[0] = m[0][0] * bh->newtrans[0] + m[0][1] * bh->newtrans[1] + m[0][2] * bh->newtrans[2];
  t[1] = m[1][0] * bh->newtrans[0] + m[1][1] * bh->newtrans[1] + m[1][2] * bh->newtrans[2];
  t[2] = m[2][0] * bh->newtrans[0] + m[2][1] * bh->newtrans[1] + m[2][2] * bh->newtrans[2];

  bh->camcent.x += t[0];
  bh->camcent.y += t[1];
  bh->camcent.z += t[2];


  /*
   * rotate view system with spaceball
   */
  bh->camviewvec.x = m[0][0] * bh->orig_camviewvec.x + m[0][1] * bh->orig_camviewvec.y + m[0][2] * bh->orig_camviewvec.z;
  bh->camviewvec.y = m[1][0] * bh->orig_camviewvec.x + m[1][1] * bh->orig_camviewvec.y + m[1][2] * bh->orig_camviewvec.z;
  bh->camviewvec.z = m[2][0] * bh->orig_camviewvec.x + m[2][1] * bh->orig_camviewvec.y + m[2][2] * bh->orig_camviewvec.z;

  bh->camupvec.x = m[0][0] * bh->orig_camupvec.x + m[0][1] * bh->orig_camupvec.y + m[0][2] * bh->orig_camupvec.z;
  bh->camupvec.y = m[1][0] * bh->orig_camupvec.x + m[1][1] * bh->orig_camupvec.y + m[1][2] * bh->orig_camupvec.z;
  bh->camupvec.z = m[2][0] * bh->orig_camupvec.x + m[2][1] * bh->orig_camupvec.y + m[2][2] * bh->orig_camupvec.z;

  /*
   * update camera parameters before we render again
   */
  rt_camera_position(scene, bh->camcent, bh->camviewvec, bh->camupvec);

  return 0;
}


