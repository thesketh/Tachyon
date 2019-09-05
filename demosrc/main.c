/* 
 * main.c - This file contains the Tachyon main program.
 *
 *  $Id: main.c,v 1.110 2013/04/21 17:38:47 johns Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "tachyon.h"    /* The Tachyon ray tracing library API */
#include "getargs.h"    /* command line argument/option parsing */
#include "parse.h"      /* Support for my own scene file format */
#include "nffparse.h"   /* Support for NFF files, as in SPD */
#include "ac3dparse.h"  /* Support for AC3D files */
#include "mgfparse.h"   /* Support for MGF files */

#ifdef USEOPENGL
#include "glwin.h"      /* OpenGL run-time display code */

#if defined(_MSC_VER)
#include <windows.h>
#else
#include <X11/Xlib.h>
#endif
#include <GL/gl.h>
#endif

#include "spaceball.h"  /* Spaceball fly-through code */

typedef struct {
  float x;
  float y;
  float z;
} floatvec; 


typedef struct {
  int oxsize, oysize;
  int xsize, ysize, fson;
#if defined(USEOPENGL)
  unsigned char * img;
  void * glwin;
#endif
} dispHandle;


#if defined(ANDROID)

int ray(int argc, char **argv); /* provide a prototype... */

/*
 * Tachyon is wrapped in a JNI shared object and called by Java... 
 */
#include <jni.h>

#if !defined(TACHYON_JNI_CLASSNAME)
#define TACHYON_JNI_CLASSNAME "com/photonlimited/Tachyon/Tachyon"
#endif
#if !defined(TACHYON_JNI_WRAPFUNC)
#define TACHYON_JNI_WRAPFUNC Java_com_photonlimited_Tachyon_Tachyon_nativeMain
#endif

void logtojava(JNIEnv *env, jobject thiz, const char *logstring) {
#if 1
  // c version JNI interface:
  jclass clazz = (*env)->FindClass(env, TACHYON_JNI_CLASSNAME);

  jmethodID logOutput = (*env)->GetMethodID(env, clazz, "logOutput",
                                            "(Ljava/lang/String;)V");

  /* this actually logs a char*, logstring */
  (*env)->CallVoidMethod(env, thiz, logOutput, (*env)->NewStringUTF(env, logstring));
#else
  // c++ version JNI interface:
  jclass clazz = (env)->FindClass(TACHYON_JNI_CLASSNAME);
  jmethodID logOutput = (env)->GetMethodID(clazz, "logOutput",
                                           "(Ljava/lang/String;)V");

  /* this actually logs a char*, logstring */
  (env)->CallVoidMethod(thiz, logOutput, (env)->NewStringUTF(logstring));
#endif
}

/* XXX gross disgusting hack!!!! */
JNIEnv *global_jnienv;  /* XXX hack! */
jobject global_thiz;   /* XXX hack! */

/* 
 * This is the main JNI wrapper function.
 * Contains startup code, neverending loop, shutdown code, etc...
 */
void TACHYON_JNI_WRAPFUNC(JNIEnv* env, jobject thiz ) {
  char* rargv[10];
  
  global_jnienv = env; /* XXX this is a hack! */
  global_thiz = thiz;  /* XXX this is a hack! */

  fprintf(stderr, "--stderr fprintf---------------------------------\n");
  printf("---regular printf----------------------------\n");
  fflush(stdout);
  logtojava(global_jnienv, global_thiz, "--Log event ---------------------\n");

#if 0
  printf("Android platform info:\n");
  printf("  sizeof(char): %d\n", sizeof(char));
  printf("  sizeof(int): %d\n", sizeof(int));
  printf("  sizeof(long): %d\n", sizeof(long));
  printf("  sizeof(void*): %d\n", sizeof(void*));
  fflush(stdout);
#endif

  rargv[0] = "tachyon.so";
  rargv[1] = "/sdcard/balls.dat";
  rargv[2] = "-o";
  rargv[3] = "/sdcard/outfile.tga";
  rargv[4] = "+V";
  rargv[5] = NULL;
 
  ray(5, rargv); /* launch Tachyon... */

  logtojava(global_jnienv, global_thiz, "--Log event ---------------------\n");
  fprintf(stderr, "--stderr fprintf---------------------------------\n");
  printf("---regular printf----------------------------\n");
  fflush(stdout);
}

static void my_ui_message(int a, char * msg) {
  char logstring[256];
  strncpy(logstring, msg, sizeof(logstring)-2);
  strcat(logstring, "\n");
 
  logtojava(global_jnienv, global_thiz, logstring);
}

static void my_ui_progress(int percent) {
  logtojava(global_jnienv, global_thiz, ".");
#if 0
  printf("\rRendering Progress:       %3d%% complete            \r", percent);
  fflush(stdout);
#endif
}

#else /* not ANDROID */

/*
 * Normal Tachyon build... 
 */
static void my_ui_message(int a, char * msg) {
  printf("%s\n", msg);
}

static void my_ui_progress(int percent) {
  printf("\rRendering Progress:       %3d%% complete            \r", percent);
  fflush(stdout);
}

#endif



#if defined(USEOPENGL)
/*
 * routines for managing runtime display of ray traced scene
 */
static int tachyon_display_reshape(dispHandle *dh) {
  float wscalex, wscaley, wminscale;
  float wxoffset, wyoffset;
  int wsx, wsy, instereo, maxx, maxy;

  glwin_get_wininfo(dh->glwin, &instereo, NULL);
  glwin_get_winsize(dh->glwin, &wsx, &wsy);
  maxx=dh->xsize;
  maxy=dh->ysize;  
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
  glOrtho(0.0, wsx, 0.0, wsy, -1.0, 1.0); /* flip upside-down Tachyon image */

  glMatrixMode(GL_MODELVIEW);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelZoom(wminscale, wminscale); /* flip upside-down Tachyon image */

  return 0;
}
#endif


static dispHandle * tachyon_display_create(SceneHandle scene) {
  dispHandle * dh = (dispHandle *) malloc(sizeof(dispHandle));
  if (dh != NULL) {
    memset(dh, 0, sizeof(dispHandle));
    /* record original image resolution for restoration if needed */
    rt_get_resolution(scene, &dh->oxsize, &dh->oysize);
    /* record current image resolution */
    dh->xsize=dh->oxsize; 
    dh->ysize=dh->oysize; 
    dh->fson=0;

#if defined(USEOPENGL)
      dh->img = (unsigned char *) malloc((dh->xsize)*(dh->ysize)*3);
      if (dh->img != NULL) {
        rt_rawimage_rgb24(scene, dh->img);

        dh->glwin = glwin_create("Tachyon Parallel/Multiprocessor Ray Tracer", dh->xsize, dh->ysize);
        tachyon_display_reshape(dh);
      } else {
        printf("Couldn't allocate image buffer for framebuffer display!!\n");
        free(dh);
        return NULL;
      } 
#endif
  }

  return dh;
}


static int tachyon_display_draw(SceneHandle scene, dispHandle *dh) {
#if defined(USEOPENGL)
  if (dh->img != NULL) {
    /* handle redraw events */
    while (glwin_handle_events(dh->glwin, GLWIN_EV_POLL_NONBLOCK)) {
      int evdev, evval;
      char evkey;
      int wsx, wsy, instereo;
      glwin_get_lastevent(dh->glwin, &evdev, &evval, &evkey);
      glwin_get_wininfo(dh->glwin, &instereo, NULL);
      glwin_get_winsize(dh->glwin, &wsx, &wsy);

      if (evdev == GLWIN_EV_KBD) {
        switch (evkey) {
          case 'q': 
          case 'Q':
          case 0x1b: /* ESC key */
            return 1; /* exit from program */
            break;

          case 'f':
          case 'F':
            /* toggle fullscreen state */
            dh->fson = (!dh->fson);
            if (glwin_fullscreen(dh->glwin, dh->fson, 0) == 0) {
              printf("Toggling fullscreen mode %s...\n",
                     (dh->fson) ? "on" : "off");
            } else {
              printf("Fullscreen mode not available...\n");
            }
            break;

#if 0
          default:
            printf("ignored keypress %0x: '%c'\n",
                   evkey, (isalnum(evkey)) ? evkey : '\0');
            break;
#endif
        }
      }

      tachyon_display_reshape(dh);
      glwin_draw_image(dh->glwin, dh->xsize, dh->ysize, dh->img);

      if (evdev == GLWIN_EV_KBD) {
        switch (evkey) {
          case 'n':
          case 'N':
            dh->xsize = dh->oxsize;
            dh->ysize = dh->oysize;
            free(dh->img);
            dh->img = (unsigned char *) calloc(1, dh->xsize*dh->ysize*3);
            rt_rawimage_rgb24(scene, dh->img);
            rt_resolution(scene, dh->xsize, dh->ysize);
            glwin_resize(dh->glwin, dh->xsize, dh->ysize);
            tachyon_display_reshape(dh);
            break;

          case 'r':
          case 'R':
            free(dh->img);
            dh->img = (unsigned char *) calloc(1, wsx*wsy*3);
            rt_rawimage_rgb24(scene, dh->img);
            rt_resolution(scene, wsx, wsy);
            dh->xsize = wsx;
            dh->ysize = wsy;
            tachyon_display_reshape(dh);
            break;

          case ']':
            dh->xsize *= 2;
            dh->ysize *= 2;
            free(dh->img);
            dh->img = (unsigned char *) calloc(1, dh->xsize*dh->ysize*3);
            rt_rawimage_rgb24(scene, dh->img);
            rt_resolution(scene, dh->xsize, dh->ysize);
            glwin_resize(dh->glwin, dh->xsize, dh->ysize);
            tachyon_display_reshape(dh);
            break;

          case '>':
            dh->xsize *= 1.5;
            dh->ysize *= 1.5;
            free(dh->img);
            dh->img = (unsigned char *) calloc(1, dh->xsize*dh->ysize*3);
            rt_rawimage_rgb24(scene, dh->img);
            rt_resolution(scene, dh->xsize, dh->ysize);
            glwin_resize(dh->glwin, dh->xsize, dh->ysize);
            tachyon_display_reshape(dh);
            break;

          case '[':
            dh->xsize /= 2;
            dh->ysize /= 2;
            free(dh->img);
            dh->img = (unsigned char *) calloc(1, dh->xsize*dh->ysize*3);
            rt_rawimage_rgb24(scene, dh->img);
            rt_resolution(scene, dh->xsize, dh->ysize);
            glwin_resize(dh->glwin, dh->xsize, dh->ysize);
            tachyon_display_reshape(dh);
            break;

          case '<':
            dh->xsize /= 1.5;
            dh->ysize /= 1.5;
            free(dh->img);
            dh->img = (unsigned char *) calloc(1, dh->xsize*dh->ysize*3);
            rt_rawimage_rgb24(scene, dh->img);
            rt_resolution(scene, dh->xsize, dh->ysize);
            glwin_resize(dh->glwin, dh->xsize, dh->ysize);
            tachyon_display_reshape(dh);
            break;
        }
      }
    } 

    glwin_draw_image(dh->glwin, dh->xsize, dh->ysize, dh->img);
  }

#endif

  return 0;
}

static void tachyon_display_delete(dispHandle *dh) {
#if defined(USEOPENGL)
  if (dh->img != NULL) {
    glwin_destroy(dh->glwin);
    free(dh->img);
  }
#endif
}


/* 
 * main loop for creating animations by flying using a spaceball
 * or other 3-D input mechanism. 
 */
static int fly_scene(argoptions opt, SceneHandle scene, int node) {
  dispHandle * dh = NULL;
  int done = 0;
  int frameno = 0;
  float fps;
  rt_timerhandle fpstimer;
  rt_timerhandle animationtimer;
  char outfilename[1];
  sbHandle * bh = NULL;

  if (node == 0)
    dh = tachyon_display_create(scene);
#if defined(USEOPENGL)
  else 
    rt_rawimage_rgb24(scene, NULL); /* must match node 0 image depth */
#endif

  rt_set_ui_message(NULL);
  rt_set_ui_progress(NULL);

  if (node == 0)
    printf("Interactive Camera Flight\n");

  outfilename[0] = '\0';
  rt_outputfile(scene, outfilename);

  fpstimer=rt_timer_create();
  animationtimer=rt_timer_create();

#if defined(USEOPENGL)
  if (node == 0) {
#if 1
    bh = tachyon_init_spaceball(scene, dh->glwin, opt.spaceballport);
#else
    if (rt_numnodes() < 2) {
      bh = tachyon_init_spaceball(scene, dh->glwin, opt.spaceballport);
    } else {
      printf("WARNING: Spaceball mode disabled when running with distributed memory");
    }
#endif
  }
#endif

  rt_timer_start(animationtimer);
  while (!done) {
    if (frameno != 0) {
      rt_timer_stop(fpstimer);
      fps = 1.0f / (float) rt_timer_time(fpstimer);
    } else {
      fps = 0.0f;
    }

    rt_timer_start(fpstimer);
    if (node == 0) {
      printf("\rRendering Frame: %9d   %10.1f FPS       ", frameno, fps);
      fflush(stdout);
    } 

    if (bh != NULL)
      done = tachyon_spaceball_update(bh, scene);

    rt_renderscene(scene);

    if (dh != NULL) {
      int rc = tachyon_display_draw(scene, dh); 
      if (rc != 0)
        done=1;
    }
    frameno++;
  } 

  rt_timer_stop(animationtimer);
  fps = frameno / (float) rt_timer_time(animationtimer);

  if (node == 0) {
    printf("\rCompleted animation of %d frames                            \n", frameno);
    printf("Animation Time: %10.4f seconds  (Averaged %7.1f FPS)\n", 
         rt_timer_time(animationtimer), fps); 
  }
  rt_timer_destroy(fpstimer);

  if (node == 0) {
    printf("\nFinished Running Camera.\n");

    if (dh !=NULL)
      tachyon_display_delete(dh);
  }

  rt_deletescene(scene); /* free the scene */
  rt_finalize(); /* close down the rendering library and MPI */

  return 0;
}



/* 
 * main loop for creating animations by playing recorded camera fly-throughs
 */
static int animate_scene(argoptions opt, SceneHandle scene, int node) {
  char outfilename[1000];
  FILE * camfp;
  dispHandle * dh = NULL;

  if (node == 0)
    dh = tachyon_display_create(scene);
#if defined(USEOPENGL)
  else 
    rt_rawimage_rgb24(scene, NULL); /* must match node 0 image depth */
#endif

  /* if we have a camera file, then animate.. */
  if ((camfp = fopen(opt.camfilename, "r")) != NULL) {
    floatvec cv, cu, cc;
    apivector cmv, cmu, cmc;
    int frameno=0;
    int done=0;
    float fps;
    rt_timerhandle fpstimer;
    rt_timerhandle animationtimer;

    rt_set_ui_message(NULL);
    rt_set_ui_progress(NULL);

    if (node == 0)
      printf("Running Camera File: %s\n", opt.camfilename);

    fpstimer=rt_timer_create();
    animationtimer=rt_timer_create();

    rt_timer_start(animationtimer);

    while (!feof(camfp) && !done) {
      fscanf(camfp, "%f %f %f  %f %f %f  %f %f %f",
        &cv.x, &cv.y, &cv.z, &cu.x, &cu.y, &cu.z, &cc.x, &cc.y, &cc.z);

      cmv.x = cv.x; cmv.y = cv.y; cmv.z = cv.z;
      cmu.x = cu.x; cmu.y = cu.y; cmu.z = cu.z;
      cmc.x = cc.x; cmc.y = cc.y; cmc.z = cc.z;

      if (frameno != 0) {
        rt_timer_stop(fpstimer);
        fps = 1.0f / (float) rt_timer_time(fpstimer);
      } else {
        fps = 0.0f;
      }

      rt_timer_start(fpstimer);
      outfilename[0] = '\0';
      if (opt.nosave == 1) {
        if (node == 0) {
          printf("\rRendering Frame: %9d   %10.4f FPS       ", frameno, fps);
          fflush(stdout);
        } 
      } else {
        sprintf(outfilename, opt.outfilename, frameno);
        if (node == 0) {
          printf("\rRendering Frame to %s   (%10.4f FPS)       ", outfilename, fps);
          fflush(stdout);
        }
      }
 
      rt_outputfile(scene, outfilename);
      rt_camera_position(scene, cmc, cmv, cmu);

      rt_renderscene(scene);

      if (dh != NULL) {
        int rc = tachyon_display_draw(scene, dh); 
        if (rc != 0)
          done=1;
      }

      frameno++;
    } 
    rt_timer_stop(animationtimer);
    fps = frameno / (float) rt_timer_time(animationtimer);
    if (node == 0) {
      printf("\rCompleted animation of %d frames                            \n", frameno);
      printf("Animation Time: %10.4f seconds  (Averaged %7.4f FPS)\n", 
           rt_timer_time(animationtimer), fps); 
    }
    rt_timer_destroy(fpstimer);
    fclose(camfp);
  } else {
    if (node == 0) {
      printf("Couldn't open camera file: %s\n", opt.camfilename);
      printf("Aborting render.\n");
    }
    rt_deletescene(scene); /* free the scene */
    rt_finalize(); /* close down the rendering library and MPI */
    return -1;
  }

  if (node == 0) {
    printf("\nFinished Running Camera.\n");

    if (dh !=NULL)
      tachyon_display_delete(dh);
  }

  rt_deletescene(scene); /* free the scene */
  rt_finalize(); /* close down the rendering library and MPI */

  return 0;
}




#if defined(ANDROID) || defined(VXWORKS) 
int ray(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
  SceneHandle scene;
  unsigned int rc;
  argoptions opt;
  char * filename;
  int node, fileindex;
  rt_timerhandle parsetimer;
  size_t len;
   
  node = rt_initialize(&argc, &argv);

  rt_set_ui_message(my_ui_message);
  rt_set_ui_progress(my_ui_progress);

  if (node == 0) {
    char strbuf[1024];
    sprintf(strbuf, "Tachyon Parallel/Multiprocessor Ray Tracer   Version %s   ",
            TACHYON_VERSION_STRING);
    my_ui_message(0, strbuf);
    sprintf(strbuf, "Copyright 1994-2013,    John E. Stone <john.stone@gmail.com> ");
    my_ui_message(0, strbuf);
    sprintf(strbuf, "------------------------------------------------------------ "); 
    my_ui_message(0, strbuf);
  }

  if ((rc = getargs(argc, argv, &opt, node)) != 0) {
    rt_finalize();
#if defined(ANDROID)
    return rc;
#else
    exit(rc);
#endif
  }

  if (opt.numfiles > 1) {
    if (node == 0)
      printf("Rendering %d scene files.\n", opt.numfiles);
  }

  for (fileindex=0; fileindex<opt.numfiles; fileindex++) { 
    scene = rt_newscene();

    /* process command line overrides */
    presceneoptions(&opt, scene);

    filename = opt.filenames[fileindex];

    if (opt.numfiles > 1) {
      if (node == 0)
        printf("\nRendering scene file %d of %d, %s\n", fileindex+1, opt.numfiles, filename);
    }

    parsetimer=rt_timer_create();
    rt_timer_start(parsetimer);

    len = strlen(filename);

    if (len > 4 && (!strcmp(filename+len-4, ".nff") ||
                    !strcmp(filename+len-4, ".NFF"))) {
      rc = ParseNFF(filename, scene); /* must be an NFF file */
    }
    else if (len > 3 && (!strcmp(filename+len-3, ".ac") ||
                         !strcmp(filename+len-3, ".AC"))) {
      rc = ParseAC3D(filename, scene); /* Must be an AC3D file */
    }
#ifdef USELIBMGF
    else if (len > 4 && (!strcmp(filename+len-4, ".mgf") ||
                         !strcmp(filename+len-4, ".MGF"))) {
      rc = ParseMGF(filename, scene, 1); /* Must be an MGF file */
    }
#endif
    else {  
      rc = readmodel(filename, scene); /* Assume its a Tachyon scene file */
    }

    rt_timer_stop(parsetimer);
    if (rc == PARSENOERR && node == 0) 
      printf("Scene Parsing Time: %10.4f seconds\n", rt_timer_time(parsetimer));
    rt_timer_destroy(parsetimer);
   
    if (rc != PARSENOERR && node == 0) {
      switch(rc) {
        case PARSEBADFILE:
          printf("Parser failed due to nonexistent input file: %s\n", filename);
          break;
        case PARSEBADSUBFILE:
          printf("Parser failed due to nonexistent included file.\n");
          break;
        case PARSEBADSYNTAX:
          printf("Parser failed due to an input file syntax error.\n");
          break;
        case PARSEEOF:
          printf("Parser unexpectedly hit an end of file.\n");
          break;
        case PARSEALLOCERR:
          printf("Parser ran out of memory.\n");
          break; 
      }
      if (fileindex+1 < opt.numfiles)
        printf("Aborting render, continuing with next scene file...\n");
      else
        printf("Aborting render.\n");

      rt_deletescene(scene); /* free the scene */
      continue;              /* process the next scene */
    }

    /* process command line overrides */
    postsceneoptions(&opt, scene);

    /* choose which rendering mode to use */
    if (opt.usecamfile == 1) {
      return animate_scene(opt, scene, node); /* fly using prerecorded data */
    } else if (opt.spaceballon) {
      return fly_scene(opt, scene, node);     /* fly Spaceball/SpaceNavigator */
    } else {
      if (opt.numfiles > 1 && opt.nosave != 1) {
        char multioutfilename[FILENAME_MAX];
        sprintf(multioutfilename, opt.outfilename, fileindex);
        rt_outputfile(scene, multioutfilename);
      }

      rt_renderscene(scene); /* Render a single frame */
    }

    rt_deletescene(scene);   /* free the scene, get ready for next one */
  }

  rt_finalize();             /* close down the rendering library and MPI */
  freeoptions(&opt);         /* free parsed command line option data */

  return 0;
}


