/*
 * spaceball.h - prototypes and definitions for Tachyon 
 *               Spaceball/SpaceNavigator interfaces
 *
 *  $Id: spaceball.h,v 1.4 2011/02/02 06:10:39 johns Exp $
 */

#if defined(USESPACEBALL)
#include "sball.h"
#endif

typedef struct {
  void *glwin;
#if defined(USESPACEBALL)
  SBallHandle sball;
#endif
  int buttondown;

  apivector camcent;
  apivector camviewvec;
  apivector camupvec;

  apivector orig_camcent;
  apivector orig_camviewvec;
  apivector orig_camupvec;
  apivector orig_camrightvec;

  float curtrans[3];
  float newtrans[3];
  float curquat[4];
  float lastquat[4];
} sbHandle;

void * tachyon_init_spaceball(SceneHandle scene, void * glwin, char * port);
int tachyon_spaceball_update(sbHandle * bh, SceneHandle scene);


