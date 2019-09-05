/* 
 * parse.c - an UltraLame (tm) parser for simple data files...
 *
 *  $Id: parse.c,v 1.87 2011/02/15 23:51:58 johns Exp $
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h> /* needed for toupper(), macro.. */

#include "tachyon.h"  /* Tachyon ray tracer API */

#ifdef USELIBMGF
#include "mgfparse.h" /* MGF parser code */
#endif

#define PARSE_INTERNAL
#include "parse.h" /* self protos */
#undef PARSE_INTERNAL

#define ERROR_READBUF_SIZE 65536

/*
 * a is unknown compared string, b must be upper case already...)
 */
static int stringcmp(const char * a, const char * b) {
  int i, s, l;

  s=strlen(a);
  l=strlen(b);

  if (s != l) 
    return 1;

  for (i=0; i<s; i++) {
    if (toupper(a[i]) != b[i]) {
      return 1;
    }
  }
  return 0;
}

static void reset_tex_table(parsehandle * ph, SceneHandle scene) {
  apitexture apitex;
  
  ph->maxtextures=512; 
  ph->numtextures=0; 
  ph->textable = (texentry *) malloc(ph->maxtextures * sizeof(texentry));
  memset(ph->textable, 0, ph->maxtextures * sizeof(texentry));

  apitex.col.r=1.0;
  apitex.col.g=1.0; 
  apitex.col.b=1.0; 
  apitex.ambient=0.1;
  apitex.diffuse=0.9;
  apitex.specular=0.0;
  apitex.opacity=1.0;
  apitex.texturefunc=0;

  ph->defaulttex.tex=rt_texture(scene, &apitex);
  rt_hash_init(&ph->texhash, 1024);
}

static void free_tex_table(parsehandle * ph, SceneHandle scene) {
  int i;

  rt_hash_destroy(&ph->texhash);

  for (i=0; i<ph->numtextures; i++) {
    free(ph->textable[i].name);
  }
  
  free(ph->textable);
  ph->textable = NULL; 

  ph->numtextures = 0;
}

static errcode add_texture(parsehandle * ph, void * tex, const char * name) {
  ph->textable[ph->numtextures].tex=tex;
  ph->textable[ph->numtextures].name = malloc(strlen(name) + 1);
  strcpy(ph->textable[ph->numtextures].name, name); 
  rt_hash_insert(&ph->texhash, ph->textable[ph->numtextures].name, ph->numtextures);

  ph->numtextures++;
  if (ph->numtextures >= ph->maxtextures) {
    texentry * newblock;
    int newsize;

    newsize = 2 * ph->maxtextures;
    newblock = realloc(ph->textable, newsize * sizeof(texentry)); 
    if (newblock != NULL) {
      ph->maxtextures = newsize;
      ph->textable = newblock;
      return PARSENOERR;
    } else {
      printf("Parse: %d textures allocated, texture slots full!\n", ph->numtextures);
      ph->numtextures--; /* keep writing over last texture if we've run out.. */
      return PARSEALLOCERR;
    }
  }

  return PARSENOERR;
}

static void * find_texture(parsehandle * ph, const char * name) {
  int i;

  i=rt_hash_lookup(&ph->texhash, name);
  if (i != HASH_FAIL) {
    return ph->textable[i].tex;
  }

  printf("Undefined texture '%s', using default. \n", name);
  return(ph->defaulttex.tex); 
}

apiflt degtorad(apiflt deg) {
  apiflt tmp;
  tmp=deg * 3.1415926 / 180.0;
  return tmp;
}

static void degvectoradvec(apivector * degvec) {
  apivector tmp;

  tmp.x=degtorad(degvec->x);
  tmp.y=degtorad(degvec->y);
  tmp.z=degtorad(degvec->z);
  *degvec=tmp;
}

static void InitRot3d(RotMat * rot, apiflt x, apiflt y, apiflt z) {
  rot->rx1=cos(y)*cos(z);
  rot->rx2=sin(x)*sin(y)*cos(z) - cos(x)*sin(z);
  rot->rx3=sin(x)*sin(z) + cos(x)*cos(z)*sin(y);
  
  rot->ry1=cos(y)*sin(z);
  rot->ry2=cos(x)*cos(z) + sin(x)*sin(y)*sin(z);
  rot->ry3=cos(x)*sin(y)*sin(z) - sin(x)*cos(z);

  rot->rz1=sin(y);
  rot->rz2=sin(x)*cos(y);
  rot->rz3=cos(x)*cos(y);
}

static void Rotate3d(RotMat * rot, apivector * vec) {
  apivector tmp;
  tmp.x=(vec->x*(rot->rx1) + vec->y*(rot->rx2) + vec->z*(rot->rx3));
  tmp.y=(vec->x*(rot->ry1) + vec->y*(rot->ry2) + vec->z*(rot->ry3));
  tmp.z=(vec->x*(rot->rz1) + vec->y*(rot->rz2) + vec->z*(rot->rz3));
  *vec=tmp; 
}

static void Scale3d(apivector * scale, apivector * vec) {
  vec->x=vec->x * scale->x;
  vec->y=vec->y * scale->y;
  vec->z=vec->z * scale->z;
}

static void Trans3d(apivector * trans, apivector * vec) {
  vec->x+=trans->x;
  vec->y+=trans->y;
  vec->z+=trans->z;
}

static void PrintSyntaxError(parsehandle * ph, 
                             const char * string, const char * found) {
  long streampos, readsize;
  long i, j, linecount;
  char cbuf[ERROR_READBUF_SIZE];

  streampos = ftell(ph->ifp);

  /* count lines up to approximate position where error occured */ 
  fseek(ph->ifp, 0, SEEK_SET); 

  i=0;
  linecount=0;
  while (i < streampos) {
    if ((streampos - i) > ERROR_READBUF_SIZE) {
      readsize = ERROR_READBUF_SIZE;
    } else {
      readsize = streampos - i;
    }

    fread(cbuf, readsize, 1, ph->ifp);
    i+=readsize;
    for (j=0; j<readsize; j++) {
      if (cbuf[j] == '\n') {
        linecount++;
      }
    } 
  }

  printf("Parse Error:\n");
  printf("   Encountered a syntax error in file %s\n", ph->filename); 
  printf("   Expected to find %s\n", string);
  printf("   Actually found: %s\n", found);
  printf("   Error occured at or prior to file offset %ld, line %ld\n",
         streampos, linecount);
  printf("   Error position is only approximate, but should be close\n\n");

  fseek(ph->ifp, streampos, SEEK_SET); /* return to previous offset */
}

static errcode GetString(parsehandle * ph, const char * string) {
  char data[255];

  fscanf(ph->ifp, "%s", data);
  if (stringcmp(data, string) != 0) {
    PrintSyntaxError(ph, string, data);
    return PARSEBADSYNTAX;
  }

  return PARSENOERR;
}

unsigned int readmodel(const char * modelfile, SceneHandle scene) {
  parsehandle ph;
  errcode rc;
  int done;

  memset(&ph, 0, sizeof(ph));
  ph.transmode = RT_TRANS_ORIG;
  ph.filename = modelfile;
  ph.ifp=fopen(modelfile, "r");
  if (ph.ifp == NULL) {
    return PARSEBADFILE;
  }

  reset_tex_table(&ph, scene); 

  rc = PARSENOERR;

  /* skip comment blocks, and search for BEGIN_SCENE */
  done = 0;
  while (!done) {
    char tmp[256];
    fscanf(ph.ifp, "%s", tmp);

    if (!stringcmp(tmp, "BEGIN_SCENE")) {
      done=1;
    } else if (!stringcmp(tmp, "#")) {
      int c;
      while (1) {
        c=fgetc(ph.ifp);
        if (c == EOF || c == '\n') /* eat comment text */
          break;
      }
    } else {
      fclose(ph.ifp);
      return PARSEBADSYNTAX;
    }
  }

  rc |= GetScenedefs(&ph, scene); 
  
  if (rc == PARSENOERR) {
    ph.numobjectsparsed=0;
    while ((rc = GetObject(&ph, scene)) == PARSENOERR) {
      ph.numobjectsparsed++;
    } 
  
    if (rc == PARSEEOF)
      rc = PARSENOERR;
  }

  fclose(ph.ifp);

  free_tex_table(&ph, scene);

  return rc;
}

static errcode ReadIncludeFile(parsehandle * ph, const char * includefile, SceneHandle scene) {
  errcode rc;
  const char * oldfilename = ph->filename;
  FILE * oldfp = ph->ifp;

  if (strcmp(includefile, ph->filename) == 0) {
    printf("Warning: possible self-recursive include of file %s\n", 
           includefile);
  }

  ph->filename=includefile;
  ph->ifp=fopen(includefile, "r");
  if (ph->ifp == NULL) {
    printf("Parser failed trying to open file: %s\n", includefile);

    /* restore old file pointers etc */
    ph->filename=oldfilename;
    ph->ifp = oldfp;

    return PARSEBADSUBFILE;
  }

  while ((rc = GetObject(ph, scene)) == PARSENOERR) {
    ph->numobjectsparsed++;
  } 
  fclose(ph->ifp);

  /* restore old file pointers etc */
  ph->filename=oldfilename;
  ph->ifp = oldfp;
  
  if (rc == PARSEEOF){
    rc = PARSENOERR;
  }

  return rc;
}

static errcode GetScenedefs(parsehandle * ph, SceneHandle scene) {
  int xres, yres;
  errcode rc = PARSENOERR;

  rc |= GetString(ph, "RESOLUTION");
  fscanf(ph->ifp, "%d %d", &xres, &yres);

  rt_outputfile(scene, "outfile.tga");
  rt_resolution(scene, xres, yres);
  rt_verbose(scene, 0);

  return rc;
}

static errcode GetShaderMode(parsehandle * ph, SceneHandle scene) {
  errcode rc = PARSENOERR;
  char data[255];

  fscanf(ph->ifp, "%s", data);
  if (!stringcmp(data, "FULL")) {
    rt_shadermode(scene, RT_SHADER_FULL);
  } else if (!stringcmp(data, "MEDIUM")) {
    rt_shadermode(scene, RT_SHADER_MEDIUM);
  } else if (!stringcmp(data, "LOW")) {
    rt_shadermode(scene, RT_SHADER_LOW);
  } else if (!stringcmp(data, "LOWEST")) {
    rt_shadermode(scene, RT_SHADER_LOWEST);
  } else {
    printf("Bad token '%s' while reading shader mode block\n", data);
    return PARSEBADSYNTAX;
  }

  while (rc == PARSENOERR) {
    fscanf(ph->ifp, "%s", data);
    if (!stringcmp(data, "END_SHADER_MODE")) {
      return rc;
    } else if (!stringcmp(data, "SHADOW_FILTER_ON")) {
      rt_shadow_filtering(scene, 1);
    } else if (!stringcmp(data, "SHADOW_FILTER_OFF")) {
      rt_shadow_filtering(scene, 0);
    } else if (!stringcmp(data, "TRANS_MAX_SURFACES")) {
      int transmaxsurf;
      fscanf(ph->ifp, "%d", &transmaxsurf);
      rt_trans_max_surfaces(scene, transmaxsurf);
    } else if (!stringcmp(data, "TRANS_ORIG")) {
      ph->transmode = RT_TRANS_ORIG;       /* reset to standard mode */
      rt_trans_mode(scene, ph->transmode);
    } else if (!stringcmp(data, "TRANS_RASTER3D")) {
      ph->transmode |= RT_TRANS_RASTER3D;  /* add Raster3D emulation flag */
      rt_trans_mode(scene, ph->transmode);
    } else if (!stringcmp(data, "TRANS_VMD")) {
      ph->transmode |= RT_TRANS_VMD;       /* add VMD transparency flag */
      rt_trans_mode(scene, ph->transmode);
    } else if (!stringcmp(data, "FOG_VMD")) {
      rt_fog_rendering_mode(scene, RT_FOG_VMD);
    } else if (!stringcmp(data, "AMBIENT_OCCLUSION")) {
      int aosamples;
      float aodirect;
      apicolor aoambient;

      rc |= GetString(ph, "AMBIENT_COLOR");
      fscanf(ph->ifp, "%f %f %f", &aoambient.r, &aoambient.g, &aoambient.b);

      rc |= GetString(ph, "RESCALE_DIRECT");
      fscanf(ph->ifp, "%f", &aodirect);

      rc |= GetString(ph, "SAMPLES");
      fscanf(ph->ifp, "%d", &aosamples);

      rt_rescale_lights(scene, aodirect);
      rt_ambient_occlusion(scene, aosamples, aoambient);
    } else {
      printf("Bad token '%s' while reading optional shader modes\n", data);
      return PARSEBADSYNTAX;
    }
  }

  return rc;
}

static errcode GetCamera(parsehandle * ph, SceneHandle scene) {
  apivector Ccenter, Cview, Cup;
  apiflt zoom, aspectratio;
  int raydepth, antialiasing;
  float a, b, c, d;
  errcode rc = PARSENOERR;
  char data[255];

  fscanf(ph->ifp, "%s", data);
  if (stringcmp(data, "PROJECTION") == 0) {
    fscanf(ph->ifp, "%s", data);
    if (stringcmp(data, "FISHEYE") == 0) {
      rt_camera_projection(scene, RT_PROJECTION_FISHEYE);
    } else if (stringcmp(data, "PERSPECTIVE") ==0) {
      rt_camera_projection(scene, RT_PROJECTION_PERSPECTIVE);
    } else if (stringcmp(data, "PERSPECTIVE_DOF") ==0) {
      rt_camera_projection(scene, RT_PROJECTION_PERSPECTIVE_DOF);

      rc |= GetString(ph, "FOCALLENGTH");
      fscanf(ph->ifp, "%f", &a);  

      rc |= GetString(ph, "APERTURE");
      fscanf(ph->ifp, "%f", &b);  

      rt_camera_dof(scene, a, b);
    } else if (stringcmp(data, "ORTHOGRAPHIC") ==0) {
      rt_camera_projection(scene, RT_PROJECTION_ORTHOGRAPHIC);
    }

    rc |= GetString(ph, "ZOOM");
    fscanf(ph->ifp, "%f", &a);  
    zoom=a;
  } else if (stringcmp(data, "ZOOM") == 0) {
    fscanf(ph->ifp, "%f", &a);
    zoom=a;
  } else {
    rc = PARSEBADSYNTAX;
    return rc;
  }

  rc |= GetString(ph, "ASPECTRATIO");
  fscanf(ph->ifp, "%f", &b);  
  aspectratio=b;

  rc |= GetString(ph, "ANTIALIASING");
  fscanf(ph->ifp, "%d", &antialiasing);

  rc |= GetString(ph, "RAYDEPTH");
  fscanf(ph->ifp, "%d", &raydepth);

  rc |= GetString(ph, "CENTER");
  fscanf(ph->ifp, "%f %f %f", &a, &b, &c);
  Ccenter.x = a;
  Ccenter.y = b;
  Ccenter.z = c;

  rc |= GetString(ph, "VIEWDIR");
  fscanf(ph->ifp, "%f %f %f", &a, &b, &c);
  Cview.x = a;
  Cview.y = b;
  Cview.z = c;

  rc |= GetString(ph, "UPDIR");
  fscanf(ph->ifp, "%f %f %f", &a, &b, &c);
  Cup.x = a;
  Cup.y = b;
  Cup.z = c;

  rt_camera_setup(scene, zoom, aspectratio, antialiasing, raydepth,
                  Ccenter, Cview, Cup);

  fscanf(ph->ifp, "%s", data);
  if (stringcmp(data, "FRUSTUM") == 0) {
    fscanf(ph->ifp, "%f %f %f %f", &a, &b, &c, &d);
    rt_camera_frustum(scene, a, b, c, d);
    fscanf(ph->ifp, "%s", data);
    if (stringcmp(data, "END_CAMERA") != 0) {
      rc |= PARSEBADSYNTAX;
      return rc;
    }
  } else if (stringcmp(data, "END_CAMERA") != 0) {
    rc |= PARSEBADSYNTAX;
    return rc;
  }


  return rc;
}

static errcode GetObject(parsehandle * ph, SceneHandle scene) {
  char objtype[256];
 
  if (fscanf(ph->ifp, "%s", objtype) == EOF) {
    return PARSEEOF;
  }
  if (!stringcmp(objtype, "TRI")) {
    return GetTri(ph, scene);
  }
  if (!stringcmp(objtype, "STRI")) {
    return GetSTri(ph, scene);
  }
  if (!stringcmp(objtype, "VCSTRI")) {
    return GetVCSTri(ph, scene);
  }
  if (!stringcmp(objtype, "VERTEXARRAY")) {
    return GetVertexArray(ph, scene);
  }
  if (!stringcmp(objtype, "SPHERE")) {
    return GetSphere(ph, scene);
  }
#if 0
  if (!stringcmp(objtype, "SPHEREARRAY")) {
    return GetSphereArray(ph, scene);
  }
#endif
  if (!stringcmp(objtype, "FCYLINDER")) {
    return GetFCylinder(ph, scene);
  }
  if (!stringcmp(objtype, "RING")) {
    return GetRing(ph, scene);
  }
  if (!stringcmp(objtype, "POLYCYLINDER")) {
    return GetPolyCylinder(ph, scene);
  }
  if (!stringcmp(objtype, "CYLINDER")) {
    return GetCylinder(ph, scene);
  }
  if (!stringcmp(objtype, "PLANE")) {
    return GetPlane(ph, scene);
  }
  if (!stringcmp(objtype, "BOX")) {
    return GetBox(ph, scene);
  }
  if (!stringcmp(objtype, "SCALARVOL")) {
    return GetVol(ph, scene);
  }
  if (!stringcmp(objtype, "IMAGEDEF")) {
    return GetImageDef(ph, scene);
  }	
  if (!stringcmp(objtype, "TEXDEF")) {
    return GetTexDef(ph, scene);
  }	
  if (!stringcmp(objtype, "TEXALIAS")) {
    return GetTexAlias(ph);
  }
  if (!stringcmp(objtype, "LIGHT")) {
    return GetLight(ph, scene);
  }
  if (!stringcmp(objtype, "DIRECTIONAL_LIGHT")) {
    return GetDirLight(ph, scene);
  }
  if (!stringcmp(objtype, "SKY_LIGHT")) {
    return GetSkyLight(ph, scene);
  }
  if (!stringcmp(objtype, "SPOTLIGHT")) {
    return GetSpotLight(ph, scene);
  }
  if (!stringcmp(objtype, "SCAPE")) {
    return GetLandScape(ph, scene);
  }
  if (!stringcmp(objtype, "SHADER_MODE")) {
    return GetShaderMode(ph, scene);
  }
  if (!stringcmp(objtype, "CAMERA")) {
    return GetCamera(ph, scene);
  }
  if (!stringcmp(objtype, "TPOLYFILE")) {
    return GetTPolyFile(ph, scene);
  }
  if (!stringcmp(objtype, "MGFFILE")) {
#ifdef USELIBMGF
    return GetMGFFile(ph, scene);
#else
    printf("MGF File Parsing is not available in this build.\n");
    return PARSEBADSYNTAX;
#endif
  }
  if (!stringcmp(objtype, "#")) {
    int c;
    while (1) {
      c=fgetc(ph->ifp);
      if (c == EOF || c == '\n')    /* eat comment text */
        return PARSENOERR;
    } 
  }
  if (!stringcmp(objtype, "BACKGROUND")) {
    return GetBackGnd(ph, scene);
  }
  if (!stringcmp(objtype, "BACKGROUND_GRADIENT")) {
    return GetBackGndGradient(ph, scene);
  }
  if (!stringcmp(objtype, "FOG")) {
    return GetFog(ph, scene);
  }
  if (!stringcmp(objtype, "INCLUDE")) {
    char includefile[FILENAME_MAX];
    fscanf(ph->ifp, "%s", includefile);
    return ReadIncludeFile(ph, includefile, scene);
  }
  if (!stringcmp(objtype, "START_CLIPGROUP")) {
    return GetClipGroup(ph, scene);
  }
  if (!stringcmp(objtype, "END_CLIPGROUP")) {
    return GetClipGroupEnd(ph, scene);
  }
  if (!stringcmp(objtype, "END_SCENE")) {
    return PARSEEOF; /* end parsing */
  }

  PrintSyntaxError(ph, "an object or other declaration", objtype);

  return PARSEBADSYNTAX;
}

static errcode GetInt(parsehandle * ph, int * i) {
  int a;
  if (fscanf(ph->ifp, "%d", &a) != 1) 
    return PARSEBADSYNTAX;

  *i = a;

  return PARSENOERR;
}

static errcode GetVector(parsehandle * ph, apivector * v1) {
  float a, b, c;
 
  if (fscanf(ph->ifp, "%f %f %f", &a, &b, &c) != 3) 
    return PARSEBADSYNTAX;

  v1->x=a;
  v1->y=b;
  v1->z=c;

  return PARSENOERR;
}

#if 0
static errcode GetFloat(parsehandle * ph, apiflt * f) {
  float a;
  if (fscanf(ph->ifp, "%f", &a) != 1) 
    return PARSEBADSYNTAX;

  *f = a;

  return PARSENOERR;
}
#endif

static errcode GetColor(parsehandle * ph, apicolor * c1) {
  float r, g, b;
  int rc; 

  rc = GetString(ph, "COLOR"); 
  fscanf(ph->ifp, "%f %f %f", &r, &g, &b);
  c1->r=r;
  c1->g=g;
  c1->b=b;

  return rc;
}

static errcode GetImageDef(parsehandle * ph, SceneHandle scene) {
  char texname[TEXNAMELEN];
  int x, y, z;
  int rc;
  unsigned char *rgb=NULL;
  int xres=0, yres=0, zres=0;

  fscanf(ph->ifp, "%s", texname);

  rc = GetString(ph, "FORMAT");
  rc |= GetString(ph, "RGB24");

  rc |= GetString(ph, "RESOLUTION");
  rc |= GetInt(ph, &xres);
  rc |= GetInt(ph, &yres);
  rc |= GetInt(ph, &zres);

  rgb = (unsigned char *) malloc(xres * yres * zres * 3);

  rc |= GetString(ph, "ENCODING");
  rc |= GetString(ph, "HEX");

  /* loop over inline pixel color definitions */
  for (z=0; z<zres; z++) {
    for (y=0; y<yres; y++) {
      for (x=0; x<xres; x++) {
        char clrstr[1024];
        int red, green, blue;
        int addr = ((z*xres*yres) + (y*xres) + x) * 3;
        int n=0;

        fscanf(ph->ifp, "%s", clrstr);

	/* parse colors stored as triplets of hexadecimal values with   */
        /* either one, two, three, or four nibbles of significant bits, */
        /* converting to an 8-bit per channel color representation.     */
	switch (strlen(clrstr)) {
          case 3: 
            n = sscanf(clrstr,"%01x%01x%01x", &red, &green, &blue);
            red   |= (red << 4);
            green |= (green << 4);
            blue  |= (blue << 4);
            break;

          case 6:
            n = sscanf(clrstr,"%02x%02x%02x", &red, &green, &blue);
            break;

          case 9:
            n = sscanf(clrstr,"%03x%03x%03x", &red, &green, &blue);
            red   >>= 4;
            green >>= 4;
            blue  >>= 4;
            break;

          case 12:
            n = sscanf(clrstr,"%04x%04x%04x", &red, &green, &blue);
            red   >>= 8;
            green >>= 8;
            blue  >>= 8;
            break;
        }

        if (n != 3 ) {
          rc |= PARSEBADSYNTAX; /* unparsed hex color occured */
        }

        /* save RGB data to texture map */
        rgb[addr    ] = red   & 0xff;
        rgb[addr + 1] = green & 0xff;
        rgb[addr + 2] = blue  & 0xff;
      }
    }
  }

  if (rc == PARSENOERR) {
    rt_define_teximage_rgb24(texname, xres, yres, zres, rgb);
  }

  return rc;
}

static errcode GetTexDef(parsehandle * ph, SceneHandle scene) {
  char texname[TEXNAMELEN];

  fscanf(ph->ifp, "%s", texname);
  add_texture(ph, GetTexBody(ph, scene, 0), texname); 

  return PARSENOERR;
}

static errcode GetTexAlias(parsehandle * ph) {
  char texname[TEXNAMELEN];
  char aliasname[TEXNAMELEN];

  fscanf(ph->ifp, "%s", texname);
  fscanf(ph->ifp, "%s", aliasname);
  add_texture(ph, find_texture(ph, aliasname), texname); 

  return PARSENOERR;
}


static errcode GetTexture(parsehandle * ph, SceneHandle scene, void ** tex) {
  char tmp[255];
  errcode rc = PARSENOERR;

  fscanf(ph->ifp, "%s", tmp);
  if (!stringcmp(tmp, "TEXTURE")) {	
    *tex = GetTexBody(ph, scene, 0);
  }
  else
    *tex = find_texture(ph, tmp);

  return rc;
}

void * GetTexBody(parsehandle * ph, SceneHandle scene, int modeflag) {
  char tmp[255];
  float a,b,c,d;
  int transmode;
  float outline, outlinewidth;
  float phong, phongexp;
  int phongtype;
  apitexture tex;
  void * voidtex; 
  errcode rc;

  transmode=RT_TRANS_ORIG;
  outline=0.0f;
  outlinewidth=0.0f;
  phong = 0.0f;
  phongexp = 100.0f;
  phongtype = RT_PHONG_PLASTIC;

  rc = GetString(ph, "AMBIENT");
  fscanf(ph->ifp, "%f", &a); 
  tex.ambient=a;

  rc |= GetString(ph, "DIFFUSE");
  fscanf(ph->ifp, "%f", &b);
  tex.diffuse=b;

  rc |= GetString(ph, "SPECULAR");
  fscanf(ph->ifp, "%f", &c);
  tex.specular=c;

  rc |= GetString(ph, "OPACITY");
  fscanf(ph->ifp, "%f", &d);  
  tex.opacity=d;

  fscanf(ph->ifp, "%s", tmp);
  if (!stringcmp(tmp, "TRANSMODE")) {
    fscanf(ph->ifp, "%s", tmp);
    if (!stringcmp(tmp, "R3D")) {
      transmode = RT_TRANS_RASTER3D;
    } 
    fscanf(ph->ifp, "%s", tmp); /* read next item */
  }

  if (!stringcmp(tmp, "OUTLINE")) {
    fscanf(ph->ifp, "%f", &outline);
    GetString(ph, "OUTLINE_WIDTH");
    fscanf(ph->ifp, "%f", &outlinewidth);
    fscanf(ph->ifp, "%s", tmp); /* read next item */
  }

  if (!stringcmp(tmp, "PHONG")) {
    fscanf(ph->ifp, "%s", tmp);
    if (!stringcmp(tmp, "METAL")) {
      phongtype = RT_PHONG_METAL;
    }
    else if (!stringcmp(tmp, "PLASTIC")) {
      phongtype = RT_PHONG_PLASTIC;
    }
    else {
      phongtype = RT_PHONG_PLASTIC;
    } 

    fscanf(ph->ifp, "%f", &phong);
    GetString(ph, "PHONG_SIZE");
    fscanf(ph->ifp, "%f", &phongexp);
    fscanf(ph->ifp, "%s", tmp);
  } else { 
    /* assume we found "COLOR" otherwise */
  }

  /* if we're processing normal objects, use the regular */
  /* texture definition pattern.                         */
  /* VCSTri objects skip the normal color and texture    */
  /* function definition since they are unused.          */ 
  if (modeflag == 0) { 
    if (stringcmp(tmp, "COLOR")) {
      rc |= PARSEBADSYNTAX;
    }

    fscanf(ph->ifp, "%f %f %f", &a, &b, &c);
    tex.col.r = a;
    tex.col.g = b;
    tex.col.b = c;
 
    rc |= GetString(ph, "TEXFUNC");

    /* this really ought to be a string, not a number... */
    fscanf(ph->ifp, "%d", &tex.texturefunc);

    switch (tex.texturefunc) {
      case RT_TEXTURE_CONSTANT:
      default: 
        break;

      case RT_TEXTURE_3D_CHECKER:
      case RT_TEXTURE_GRIT:
      case RT_TEXTURE_MARBLE:
      case RT_TEXTURE_WOOD:
      case RT_TEXTURE_GRADIENT:
      case RT_TEXTURE_CYLINDRICAL_CHECKER:
        rc |= GetString(ph, "CENTER");
        rc |= GetVector(ph, &tex.ctr);
        rc |= GetString(ph, "ROTATE");
        rc |= GetVector(ph, &tex.rot);
        rc |= GetString(ph, "SCALE");
        rc |= GetVector(ph, &tex.scale);
        break;

      case RT_TEXTURE_CYLINDRICAL_IMAGE:
      case RT_TEXTURE_SPHERICAL_IMAGE:
        fscanf(ph->ifp, "%s", tex.imap);
        rc |= GetString(ph, "CENTER");
        rc |= GetVector(ph, &tex.ctr);
        rc |= GetString(ph, "ROTATE");
        rc |= GetVector(ph, &tex.rot);
        rc |= GetString(ph, "SCALE");
        rc |= GetVector(ph, &tex.scale);
        break;
     
      case RT_TEXTURE_PLANAR_IMAGE:
        fscanf(ph->ifp, "%s", tex.imap);
        rc |= GetString(ph, "CENTER");
        rc |= GetVector(ph, &tex.ctr);
        rc |= GetString(ph, "ROTATE");
        rc |= GetVector(ph, &tex.rot);
        rc |= GetString(ph, "SCALE");
        rc |= GetVector(ph, &tex.scale);
        rc |= GetString(ph, "UAXIS");
        rc |= GetVector(ph, &tex.uaxs);
        rc |= GetString(ph, "VAXIS");
        rc |= GetVector(ph, &tex.vaxs);
        break;

      case RT_TEXTURE_VOLUME_IMAGE:
        fscanf(ph->ifp, "%s", tex.imap);
        rc |= GetString(ph, "CENTER");
        rc |= GetVector(ph, &tex.ctr);
        rc |= GetString(ph, "ROTATE");
        rc |= GetVector(ph, &tex.rot);
        rc |= GetString(ph, "SCALE");
        rc |= GetVector(ph, &tex.scale);
        rc |= GetString(ph, "UAXIS");
        rc |= GetVector(ph, &tex.uaxs);
        rc |= GetString(ph, "VAXIS");
        rc |= GetVector(ph, &tex.vaxs);
        rc |= GetString(ph, "WAXIS");
        rc |= GetVector(ph, &tex.waxs);
        break;
    }
  } else {
    if (stringcmp(tmp, "VCST")) {
      rc |= PARSEBADSYNTAX;
    }

    /* if we're processing VCSTri objects, set some defaults */
    tex.col.r = 1.0;
    tex.col.g = 1.0;
    tex.col.b = 1.0;
    tex.texturefunc = 0; /* set to none by default, gets reset anyway */
  }

  voidtex = rt_texture(scene, &tex);
  rt_tex_phong(voidtex, phong, phongexp, phongtype);
  rt_tex_transmode(voidtex, transmode);
  rt_tex_outline(voidtex, outline, outlinewidth);

  return voidtex;
}

static errcode GetDirLight(parsehandle * ph, SceneHandle scene) {
  char tmp[255];
  apivector dir;
  apitexture tex;
  float r, g, b;
  errcode rc;

  memset(&tex, 0, sizeof(apitexture)); 

  rc = GetString(ph, "DIRECTION"); 
  rc |= GetVector(ph, &dir); 

  fscanf(ph->ifp, "%s", tmp);
  if (!stringcmp(tmp, "COLOR")) {
    fscanf(ph->ifp, "%f %f %f", &r, &g, &b);
    tex.col.r=r;
    tex.col.g=g;
    tex.col.b=b;

    rt_directional_light(scene, rt_texture(scene, &tex), dir);
  }

  return rc;
}

static errcode GetLight(parsehandle * ph, SceneHandle scene) {
  char tmp[255];
  apiflt rad, Kc, Kl, Kq;
  apivector ctr;
  apitexture tex;
  float r, g, b, a;
  errcode rc;
  void * li;

  memset(&tex, 0, sizeof(apitexture)); 

  rc = GetString(ph, "CENTER"); 
  rc |= GetVector(ph, &ctr); 
  rc |= GetString(ph, "RAD");
  fscanf(ph->ifp, "%f", &a);  /* read in radius */ 
  rad=a;

  fscanf(ph->ifp, "%s", tmp);
  if (!stringcmp(tmp, "COLOR")) {
    fscanf(ph->ifp, "%f %f %f", &r, &g, &b);
    tex.col.r=r;
    tex.col.g=g;
    tex.col.b=b;

    li = rt_light(scene, rt_texture(scene, &tex), ctr, rad);
  }
  else { 
    if (stringcmp(tmp, "ATTENUATION"))
      return -1;

    rc |= GetString(ph, "CONSTANT");
    fscanf(ph->ifp, "%f", &a); 
    Kc=a;
    rc |= GetString(ph, "LINEAR");
    fscanf(ph->ifp, "%f", &a); 
    Kl=a;
    rc |= GetString(ph, "QUADRATIC");
    fscanf(ph->ifp, "%f", &a); 
    Kq=a;
    rc |= GetColor(ph, &tex.col);

    li = rt_light(scene, rt_texture(scene, &tex), ctr, rad);

    rt_light_attenuation(li, Kc, Kl, Kq);
  } 

  return rc;
}

static errcode GetSkyLight(parsehandle * ph, SceneHandle scene) {
  errcode rc;
  apicolor ambcol;
  int numsamples = 128;

  ambcol.r = 1.0;
  ambcol.g = 1.0;
  ambcol.b = 1.0;

  rc = GetString(ph, "NUMSAMPLES");
  fscanf(ph->ifp, "%d", &numsamples);

  rc |= GetString(ph, "COLOR");
  fscanf(ph->ifp, "%f %f %f", &ambcol.r, &ambcol.g, &ambcol.b);

  rt_ambient_occlusion(scene, numsamples, ambcol);

  return rc;
}

static errcode GetSpotLight(parsehandle * ph, SceneHandle scene) {
  char tmp[255];
  apiflt rad, Kc, Kl, Kq;
  apivector ctr;
  apitexture tex;
  apivector direction;
  apiflt start, end;
  float r, g, b, a;
  errcode rc;
  void * li;

  memset(&tex, 0, sizeof(apitexture)); 

  rc = GetString(ph, "CENTER"); 
  rc |= GetVector(ph, &ctr); 
  rc |= GetString(ph,"RAD");
  fscanf(ph->ifp, "%f", &a);  /* read in radius */ 
  rad=a;
 
  rc |= GetString(ph, "DIRECTION"); 
  rc |= GetVector(ph, &direction); 
  rc |= GetString(ph, "FALLOFF_START");
  fscanf(ph->ifp, "%f",&a);
  start=a;
  rc |= GetString(ph, "FALLOFF_END");
  fscanf(ph->ifp, "%f", &a);
  end=a;
   
  fscanf(ph->ifp, "%s", tmp);
  if (!stringcmp(tmp, "COLOR")) {
    fscanf(ph->ifp, "%f %f %f", &r, &g, &b);
    tex.col.r=r;
    tex.col.g=g;
    tex.col.b=b;

    li = rt_spotlight(scene, rt_texture(scene, &tex), ctr, rad, direction, start, end);
  } 
  else {
    if (stringcmp(tmp, "ATTENUATION"))
      return -1;
    rc |= GetString(ph, "CONSTANT");
    fscanf(ph->ifp, "%f", &a);
    Kc=a;
    rc |= GetString(ph, "LINEAR");
    fscanf(ph->ifp, "%f", &a);
    Kl=a;
    rc |= GetString(ph, "QUADRATIC");
    fscanf(ph->ifp, "%f", &a);
    Kq=a;
    rc |= GetColor(ph, &tex.col);

    li = rt_spotlight(scene, rt_texture(scene, &tex), ctr, rad, direction, start, end);
    rt_light_attenuation(li, Kc, Kl, Kq);
  }

  return rc;
}


static errcode GetFog(parsehandle * ph, SceneHandle scene) {
  char tmp[255];
  apicolor fogcol; 
  float start, end, density;
  errcode rc = PARSENOERR;
 
  fscanf(ph->ifp, "%s", tmp); 
  if (!stringcmp(tmp, "LINEAR")) {
    rt_fog_mode(scene, RT_FOG_LINEAR);
  } else if (!stringcmp(tmp, "EXP")) {
    rt_fog_mode(scene, RT_FOG_EXP);
  } else if (!stringcmp(tmp, "EXP2")) {
    rt_fog_mode(scene, RT_FOG_EXP2);
  } else if (!stringcmp(tmp, "OFF")) {
    rt_fog_mode(scene, RT_FOG_NONE);
  }

  rc |= GetString(ph, "START");
  fscanf(ph->ifp, "%f", &start);

  rc |= GetString(ph, "END");
  fscanf(ph->ifp, "%f", &end);

  rc |= GetString(ph, "DENSITY");
  fscanf(ph->ifp, "%f", &density);

  rc |= GetColor(ph, &fogcol);

  rt_fog_parms(scene, fogcol, start, end, density);

  return PARSENOERR;
}


static errcode GetBackGnd(parsehandle * ph, SceneHandle scene) {
  float r,g,b;
  apicolor scenebackcol; 
  
  fscanf(ph->ifp, "%f %f %f", &r, &g, &b);

  scenebackcol.r=r;
  scenebackcol.g=g;
  scenebackcol.b=b;
  rt_background(scene, scenebackcol);

  return PARSENOERR;
}

static errcode GetBackGndGradient(parsehandle * ph, SceneHandle scene) {
  char gradienttype[1024];
  float topval, botval;
  float topcol[3], botcol[3];
  float updir[3];
  int rc=0;

  fscanf(ph->ifp, "%s", gradienttype); /* read next array type */

  /* read vertex coordinates */
  if (!stringcmp(gradienttype, "SKY_SPHERE")) {
    rt_background_mode(scene, RT_BACKGROUND_TEXTURE_SKY_SPHERE);
  } else if (!stringcmp(gradienttype, "SKY_ORTHO_PLANE")) {
    rt_background_mode(scene, RT_BACKGROUND_TEXTURE_SKY_ORTHO_PLANE);
  } else {
    printf("Expected either a sky sphere or sky ortho plane background\n");
    printf("gradient texture definition.\n");
    return PARSEBADSYNTAX;
  }

  rc = GetString(ph, "UPDIR");
  fscanf(ph->ifp, "%f %f %f", &updir[0], &updir[1], &updir[2]);
 
  rc |= GetString(ph, "TOPVAL");
  fscanf(ph->ifp, "%f", &topval);

  rc |= GetString(ph, "BOTTOMVAL");
  fscanf(ph->ifp, "%f", &botval);
 
  rc |= GetString(ph, "TOPCOLOR");
  fscanf(ph->ifp, "%f %f %f", &topcol[0], &topcol[1], &topcol[2]);

  rc |= GetString(ph, "BOTTOMCOLOR");
  fscanf(ph->ifp, "%f %f %f", &botcol[0], &botcol[1], &botcol[2]);

  rt_background_gradient(scene,
                         rt_vector(updir[0], updir[1], updir[2]),
                         topval, botval,
                         rt_color(topcol[0], topcol[1], topcol[2]),
                         rt_color(botcol[0], botcol[1], botcol[2]));

  return rc;
}

static errcode GetCylinder(parsehandle * ph, SceneHandle scene) {
  apiflt rad;
  apivector ctr, axis;
  void * tex;
  float a;
  errcode rc;

  rc = GetString(ph, "CENTER");
  rc |= GetVector(ph, &ctr);
  rc |= GetString(ph, "AXIS");
  rc |= GetVector(ph, &axis);
  rc |= GetString(ph, "RAD");
  fscanf(ph->ifp, "%f", &a);
  rad=a;

  rc |= GetTexture(ph, scene, &tex);
  rt_cylinder(scene, tex, ctr, axis, rad); 

  return rc;
}

static errcode GetFCylinder(parsehandle * ph, SceneHandle scene) {
  apiflt rad;
  apivector ctr, axis;
  apivector pnt1={0,0,0};
  apivector pnt2={0,0,0};
  void * tex;
  float a;
  errcode rc;

  rc = GetString(ph, "BASE");
  rc |= GetVector(ph, &pnt1);
  rc |= GetString(ph, "APEX");
  rc |= GetVector(ph, &pnt2);

  ctr=pnt1;
  axis.x=pnt2.x - pnt1.x; 
  axis.y=pnt2.y - pnt1.y;
  axis.z=pnt2.z - pnt1.z;

  rc |= GetString(ph, "RAD");
  fscanf(ph->ifp, "%f", &a);
  rad=a;

  rc |= GetTexture(ph, scene, &tex);
  rt_fcylinder(scene, tex, ctr, axis, rad); 

  return rc;
}
 
static errcode GetPolyCylinder(parsehandle * ph, SceneHandle scene) {
  apiflt rad;
  apivector * temp;
  void * tex;
  float a;
  int numpts, i;
  errcode rc;

  rc = GetString(ph, "POINTS");
  fscanf(ph->ifp, "%d", &numpts);

  temp = (apivector *) malloc(numpts * sizeof(apivector));

  for (i=0; i<numpts; i++) {
    rc |= GetVector(ph, &temp[i]);
  }         

  rc |= GetString(ph, "RAD");
  fscanf(ph->ifp, "%f", &a);
  rad=a;

  rc |= GetTexture(ph, scene, &tex);
  rt_polycylinder(scene, tex, temp, numpts, rad); 

  free(temp);

  return rc;
}
 

static errcode GetSphere(parsehandle * ph, SceneHandle scene) {
  apiflt rad;
  apivector ctr;
  void * tex;
  float a;
  errcode rc;
 
  rc = GetString(ph, "CENTER");
  rc |= GetVector(ph, &ctr); 
  rc |= GetString(ph, "RAD");
  fscanf(ph->ifp, "%f", &a); 
  rad=a;

  rc |= GetTexture(ph, scene, &tex); 
 
  rt_sphere(scene, tex, ctr, rad);

  return rc;
}

static errcode GetPlane(parsehandle * ph, SceneHandle scene) {
  apivector normal;
  apivector ctr;
  void * tex;
  errcode rc;

  rc = GetString(ph, "CENTER");
  rc |= GetVector(ph, &ctr);
  rc |= GetString(ph, "NORMAL");
  rc |= GetVector(ph, &normal);
  rc |= GetTexture(ph, scene, &tex);

  rt_plane(scene, tex, ctr, normal);

  return rc;
}

static errcode GetVol(parsehandle * ph, SceneHandle scene) {
  apivector min, max;
  int x,y,z;  
  char fname[255];
  void * tex;
  errcode rc;
 
  rc = GetString(ph, "MIN");
  rc |= GetVector(ph, &min);
  rc |= GetString(ph, "MAX");
  rc |= GetVector(ph, &max);
  rc |= GetString(ph, "DIM");
  fscanf(ph->ifp, "%d %d %d ", &x, &y, &z);
  rc |= GetString(ph, "FILE");
  fscanf(ph->ifp, "%s", fname);  
  rc |= GetTexture(ph, scene, &tex);
 
  rt_scalarvol(scene, tex, min, max, x, y, z, fname, NULL); 

  return rc;
}

static errcode GetBox(parsehandle * ph, SceneHandle scene) {
  apivector min, max;
  void * tex;
  errcode rc;

  rc = GetString(ph, "MIN");
  rc |= GetVector(ph, &min);
  rc |= GetString(ph, "MAX");
  rc |= GetVector(ph, &max);
  rc |= GetTexture(ph, scene, &tex);

  rt_box(scene, tex, min, max);

  return rc;
}

static errcode GetRing(parsehandle * ph, SceneHandle scene) {
  apivector normal;
  apivector ctr;
  void * tex;
  float a,b;
  errcode rc;
 
  rc = GetString(ph, "CENTER");
  rc |= GetVector(ph, &ctr);
  rc |= GetString(ph, "NORMAL");
  rc |= GetVector(ph, &normal);
  rc |= GetString(ph, "INNER");
  fscanf(ph->ifp, " %f ", &a);
  rc |= GetString(ph, "OUTER");
  fscanf(ph->ifp, " %f ", &b);
  rc |= GetTexture(ph, scene, &tex);
 
  rt_ring(scene, tex, ctr, normal, a, b);

  return rc;
}

static errcode GetTri(parsehandle * ph, SceneHandle scene) {
  apivector v0,v1,v2;
  void * tex;
  errcode rc;

  rc = GetString(ph, "V0");
  rc |= GetVector(ph, &v0);

  rc |= GetString(ph, "V1");
  rc |= GetVector(ph, &v1);

  rc |= GetString(ph, "V2");
  rc |= GetVector(ph, &v2);

  rc |= GetTexture(ph, scene, &tex);

  rt_tri(scene, tex, v0, v1, v2);

  return rc;
}

static errcode GetSTri(parsehandle * ph, SceneHandle scene) {
  apivector v0,v1,v2,n0,n1,n2;
  void * tex;
  errcode rc;

  rc = GetString(ph, "V0");
  rc |= GetVector(ph, &v0);

  rc |= GetString(ph, "V1");
  rc |= GetVector(ph, &v1);

  rc |= GetString(ph, "V2");
  rc |= GetVector(ph, &v2);
  
  rc |= GetString(ph, "N0");
  rc |= GetVector(ph, &n0);

  rc |= GetString(ph, "N1");
  rc |= GetVector(ph, &n1);

  rc |= GetString(ph, "N2");
  rc |= GetVector(ph, &n2);

  rc |= GetTexture(ph, scene, &tex);
  
  rt_stri(scene, tex, v0, v1, v2, n0, n1, n2);

  return rc;
}

static errcode GetVCSTri(parsehandle * ph, SceneHandle scene) {
  apivector v0,v1,v2,n0,n1,n2;
  apicolor  c0,c1,c2;
  float a, b, c;

  void * tex;
  errcode rc;

  rc = GetString(ph, "V0");
  rc |= GetVector(ph, &v0);

  rc |= GetString(ph, "V1");
  rc |= GetVector(ph, &v1);

  rc |= GetString(ph, "V2");
  rc |= GetVector(ph, &v2);
  
  rc |= GetString(ph, "N0");
  rc |= GetVector(ph, &n0);

  rc |= GetString(ph, "N1");
  rc |= GetVector(ph, &n1);

  rc |= GetString(ph, "N2");
  rc |= GetVector(ph, &n2);

  rc |= GetString(ph, "C0");
  fscanf(ph->ifp, "%f %f %f", &a, &b, &c);
  c0.r = a;
  c0.g = b;
  c0.b = c;

  rc |= GetString(ph, "C1");
  fscanf(ph->ifp, "%f %f %f", &a, &b, &c);
  c1.r = a;
  c1.g = b;
  c1.b = c;

  rc |= GetString(ph, "C2");
  fscanf(ph->ifp, "%f %f %f", &a, &b, &c);
  c2.r = a;
  c2.g = b;
  c2.b = c;

  tex = GetTexBody(ph, scene, 1);
  
  rt_vcstri(scene, tex, v0, v1, v2, n0, n1, n2, c0, c1, c2);

  return rc;
}

static errcode GetSphereArray(parsehandle * ph, SceneHandle scene) {
  char arraytype[1024];
  int i;
  int spherecount=0;
  errcode rc=PARSENOERR;
  void * tex=NULL;
  float * v = NULL;
  float * r = NULL;
  float * c = NULL;

  rc |= GetString(ph, "NUMSPHERES");
  rc |= GetInt(ph, &spherecount);
  if (rc!= PARSENOERR)
    return rc;

  fscanf(ph->ifp, "%s", arraytype); /* read next array type */

  /* read sphere coordinates */
  if (!stringcmp(arraytype, "CENTERS")) {
    v = (float *) malloc(spherecount * 3 * sizeof(float));
    for (i=0; i<spherecount * 3; i+=3) {
      fscanf(ph->ifp, "%f %f %f", &v[i], &v[i+1], &v[i+2]); 
    }
  } else {
    printf("Expected sphere array centers block\n");
    return PARSEBADSYNTAX;
  }

  /* use the default texture until we parse a subsequent texture command */
  tex = ph->defaulttex.tex; 
  fscanf(ph->ifp, "%s", arraytype); /* read next array type */

  /* read sphere radii */
  if (!stringcmp(arraytype, "RADII")) {
    r = (float *) malloc(spherecount * sizeof(float));
    for (i=0; i<spherecount; i++) {
      fscanf(ph->ifp, "%f", &r[i]); 
    }
  } else {
    printf("Expected sphere array radii block\n");
    return PARSEBADSYNTAX;
  }

  /* read sphere colors */
  if (!stringcmp(arraytype, "COLORS")) {
    c = (float *) malloc(spherecount * 3 * sizeof(float));
    for (i=0; i<spherecount * 3; i+=3) {
      fscanf(ph->ifp, "%f %f %f", &c[i], &c[i+1], &c[i+2]); 
    }
  } else {
    printf("Expected sphere array colors block\n");
    return PARSEBADSYNTAX;
  }

  /* read sphere array texture, same for all spheres */
  if (!stringcmp(arraytype, "TEXTURE")) {
    /* read in texture to use for all sphere geometry     */
    /* don't require texture color since it won't be used */
    /* in the current implementation.                     */
    tex = GetTexBody(ph, scene, 0);
    if (tex == NULL) {
      printf("Failed to parse vertex array texture block\n");
      rc |=  PARSEBADSYNTAX;
    }
  }

  /* generate the spheres */
  if (rc == PARSENOERR) {
    for (i=0; i<spherecount * 3; i+=3) {
      rt_sphere3fv(scene, tex, &c[i], r[i]);
    }
  }

  return rc;
}


static errcode GetVertexArray(parsehandle * ph, SceneHandle scene) {
  char arraytype[1024];
  int done, i, texusecount;
  int vertexcount=0;
  errcode rc=PARSENOERR;
  void * tex=NULL;
  float * v = NULL;
  float * n = NULL;
  float * c = NULL;

  rc |= GetString(ph, "NUMVERTS");
  rc |= GetInt(ph, &vertexcount);
  if (rc!= PARSENOERR)
    return rc;

  fscanf(ph->ifp, "%s", arraytype); /* read next array type */

  /* read vertex coordinates */
  if (!stringcmp(arraytype, "COORDS")) {
    v = (float *) malloc(vertexcount * 3 * sizeof(float));
    for (i=0; i<vertexcount * 3; i+=3) {
      fscanf(ph->ifp, "%f %f %f", &v[i], &v[i+1], &v[i+2]); 
    }
    fscanf(ph->ifp, "%s", arraytype); /* read next array type */
  } else {
    printf("Expected vertex array coords block\n");
    return PARSEBADSYNTAX;
  }

  /* read vertex normals */
  if (!stringcmp(arraytype, "NORMALS")) {
    n = (float *) malloc(vertexcount * 3 * sizeof(float));
    for (i=0; i<vertexcount * 3; i+=3) {
      fscanf(ph->ifp, "%f %f %f", &n[i], &n[i+1], &n[i+2]); 
    }
    fscanf(ph->ifp, "%s", arraytype); /* read next array type */
  } else {
    free(v);
    printf("Expected vertex array normals block\n");
    return PARSEBADSYNTAX;
  }


  /* use the default texture until we parse a subsequent texture command */
  tex = ph->defaulttex.tex; 

  texusecount=1;
  done = 0;
  while (!done) {
    /* read vertex colors */
    if (!stringcmp(arraytype, "COLORS")) {
      c = (float *) malloc(vertexcount * 3 * sizeof(float));
      for (i=0; i<vertexcount * 3; i+=3) {
        fscanf(ph->ifp, "%f %f %f", &c[i], &c[i+1], &c[i+2]); 
      }
#if 0
    } else if (!stringcmp(arraytype, "TEXCOORDS3")) {
      /* read vertex texture coordinates */
      texmode = 3;
      t = (float *) malloc(vertexcount * 3 * sizeof(float));
      for (i=0; i<vertexcount * 3; i++) {
        fscanf(ph->ifp, "%f %f %f", &t[i], &t[i+1], &t[i+2]); 
      }
    } else if (!stringcmp(arraytype, "TEXCOORDS2")) {
      texmode = 2;
      t = (float *) malloc(vertexcount * 2 * sizeof(float));
      for (i=0; i<vertexcount * 3; i++) {
        fscanf(ph->ifp, "%f %f", &t[i], &t[i+1]); 
      }
#endif
    } else if (!stringcmp(arraytype, "TEXTURE")) {
      /* read in texture to use for all following geometry  */
      /* don't require texture color since it won't be used */
      /* in the current implementation.                     */
      tex = GetTexBody(ph, scene, 0);
      if (tex == NULL) {
        printf("Failed to parse vertex array texture block\n");
        rc |=  PARSEBADSYNTAX;
        done = 1;
      }
      texusecount=0;
    } else if (!stringcmp(arraytype, "TRISTRIP")) {
      int t;
      int numv=0;
      int stripaddr[2][3] = { {0, 1, 2}, {1, 0, 2} };
      int * facets=NULL;

      rc |= GetInt(ph, &numv);
      if (rc!= PARSENOERR)
        return PARSEBADSYNTAX;

      facets = (int *) malloc(numv * sizeof(int));
      for (i=0; i<numv; i++) {
        fscanf(ph->ifp, "%d", &facets[i]); 
      }

      /* render triangle strips one triangle at a time        */
      /* triangle winding order is:                           */
      /*   v0, v1, v2, then v2, v1, v3, then v2, v3, v4, etc. */

      /* loop over all triangles in this triangle strip       */
      for (t=0; t < (numv - 2); t++) {
        /* render one triangle, using lookup table to fix winding order */
        int v0 = facets[t + (stripaddr[t & 0x01][0])];
        int v1 = facets[t + (stripaddr[t & 0x01][1])];
        int v2 = facets[t + (stripaddr[t & 0x01][2])];

        if ((v0 >= 0) && (v0 < vertexcount) &&
            (v1 >= 0) && (v1 < vertexcount) &&
            (v2 >= 0) && (v2 < vertexcount)) {
          v0 *= 3;
          v1 *= 3;
          v2 *= 3;

          if (c != NULL) {
            if (texusecount > 0)
              tex = rt_texture_copy_vcstri(scene, tex); /* create a new one */

            rt_vcstri(scene, tex, 
                      rt_vector(v[v0], v[v0 + 1], v[v0 + 2]),
                      rt_vector(v[v1], v[v1 + 1], v[v1 + 2]),
                      rt_vector(v[v2], v[v2 + 1], v[v2 + 2]),
                      rt_vector(n[v0], n[v0 + 1], n[v0 + 2]),
                      rt_vector(n[v1], n[v1 + 1], n[v1 + 2]),
                      rt_vector(n[v2], n[v2 + 1], n[v2 + 2]),
                       rt_color(c[v0], c[v0 + 1], c[v0 + 2]),
                       rt_color(c[v1], c[v1 + 1], c[v1 + 2]),
                       rt_color(c[v2], c[v2 + 1], c[v2 + 2]));
          } else {
            rt_stri(scene, tex, 
                    rt_vector(v[v0], v[v0 + 1], v[v0 + 2]),
                    rt_vector(v[v1], v[v1 + 1], v[v1 + 2]),
                    rt_vector(v[v2], v[v2 + 1], v[v2 + 2]),
                    rt_vector(n[v0], n[v0 + 1], n[v0 + 2]),
                    rt_vector(n[v1], n[v1 + 1], n[v1 + 2]),
                    rt_vector(n[v2], n[v2 + 1], n[v2 + 2]));
          }
        } else {
          printf("tristrip error: skipping invalid strip vertex %d\n", t);
          printf("  vertexcount: %d\n", vertexcount);
          printf("  verts: %d %d %d\n", v0, v1, v2);
          rc |=  PARSEBADSYNTAX;
          done = 1;
          break;
        }

        texusecount++; /* don't reuse textures */
      }

      free(facets);
    } else if (!stringcmp(arraytype, "TRIMESH")) {
      int numfacets=0;
      int * facets;

      rc |= GetInt(ph, &numfacets);
      if (rc!= PARSENOERR)
        return PARSEBADSYNTAX;
        
      facets = (int *) malloc(numfacets * 3 * sizeof(int));
      for (i=0; i<numfacets*3; i+=3) {
        fscanf(ph->ifp, "%d %d %d", &facets[i], &facets[i+1], &facets[i+2]); 
      }

      /* loop over all triangles in this mesh */
      for (i=0; i < numfacets*3; i+=3) {
        int v0 = facets[i    ];
        int v1 = facets[i + 1];
        int v2 = facets[i + 2];


        if ((v0 >= 0) && (v0 < vertexcount) &&
            (v1 >= 0) && (v1 < vertexcount) &&
            (v2 >= 0) && (v2 < vertexcount)) {
          v0 *= 3;
          v1 *= 3;
          v2 *= 3;

          if (c != NULL) {
            if (texusecount > 0)
              tex = rt_texture_copy_vcstri(scene, tex); /* create a new one */
            rt_vcstri(scene, tex, 
                      rt_vector(v[v0], v[v0 + 1], v[v0 + 2]),
                      rt_vector(v[v1], v[v1 + 1], v[v1 + 2]),
                      rt_vector(v[v2], v[v2 + 1], v[v2 + 2]),
                      rt_vector(n[v0], n[v0 + 1], n[v0 + 2]),
                      rt_vector(n[v1], n[v1 + 1], n[v1 + 2]),
                      rt_vector(n[v2], n[v2 + 1], n[v2 + 2]),
                      rt_color(c[v0], c[v0 + 1], c[v0 + 2]),
                      rt_color(c[v1], c[v1 + 1], c[v1 + 2]),
                      rt_color(c[v2], c[v2 + 1], c[v2 + 2]));
          } else {
            rt_stri(scene, tex, 
                    rt_vector(v[v0], v[v0 + 1], v[v0 + 2]),
                    rt_vector(v[v1], v[v1 + 1], v[v1 + 2]),
                    rt_vector(v[v2], v[v2 + 1], v[v2 + 2]),
                    rt_vector(n[v0], n[v0 + 1], n[v0 + 2]),
                    rt_vector(n[v1], n[v1 + 1], n[v1 + 2]),
                    rt_vector(n[v2], n[v2 + 1], n[v2 + 2]));
          }
        } else {
          printf("trimesh error: skipping invalid vertex in facet %d\n", i/3);
          printf("  numfacets: %d  vertexcount: %d\n", numfacets, vertexcount);
          printf("  verts: %d %d %d\n", v0, v1, v2);
          rc |=  PARSEBADSYNTAX;
          done = 1;
          break;
        }

        texusecount++; /* don't reuse textures */
      }

      free(facets);
    } else if (!stringcmp(arraytype, "END_VERTEXARRAY")) {
      done = 1;
    } else {
      printf("Unrecognized vertex array block `%s`\n", arraytype);
      rc |=  PARSEBADSYNTAX;
      done = 1;
    }

    if (!done) {
      fscanf(ph->ifp, "%s", arraytype); /* read next array type */
    }
  }  

  if (v != NULL)
    free(v);
  if (n != NULL)
    free(n);
  if (c != NULL)
    free(c);

  return rc;
}


static errcode GetLandScape(parsehandle * ph, SceneHandle scene) {
  void * tex;
  apivector ctr;
  apiflt wx, wy;
  int m, n;
  float a,b;
  errcode rc;

  rc = GetString(ph, "RES");
  fscanf(ph->ifp, "%d %d", &m, &n);

  rc |= GetString(ph, "SCALE");
  fscanf(ph->ifp, "%f %f", &a, &b);   
  wx=a;
  wy=b;

  rc |= GetString(ph, "CENTER");
  rc |= GetVector(ph, &ctr);

  rc |= GetTexture(ph, scene, &tex);

  rt_landscape(scene, tex, m, n, ctr, wx, wy);

  return rc;
}

static errcode GetTPolyFile(parsehandle * ph, SceneHandle scene) {
  void * tex;
  char ifname[255];
  FILE *ifp;
  int v;
  RotMat RotA;
  errcode rc=0;
  int totalpolys=0;
  apivector ctr={0,0,0};
  apivector rot={0,0,0};
  apivector scale={0,0,0};
  apivector v0={0,0,0};
  apivector v1={0,0,0};
  apivector v2={0,0,0};

  rc = GetString(ph, "SCALE"); 
  rc |= GetVector(ph, &scale);

  rc |= GetString(ph, "ROT");
  rc |= GetVector(ph, &rot);

  degvectoradvec(&rot); 
  InitRot3d(&RotA, rot.x, rot.y, rot.z);

  rc |= GetString(ph, "CENTER");
  rc |= GetVector(ph, &ctr);

  rc |= GetString(ph, "FILE");
  fscanf(ph->ifp, "%s", ifname);

  rc |= GetTexture(ph, scene, &tex);

  if ((ifp=fopen(ifname, "r")) == NULL) {
    printf("Can't open data file %s for input!! Aborting...\n", ifname);
    return PARSEBADSUBFILE;
  }

  while (!feof(ifp)) {
    fscanf(ifp, "%d", &v);
    if (v != 3) { break; }

    totalpolys++;
    v=0; 
     
    rc |= GetVector(ph, &v0);
    rc |= GetVector(ph, &v1);
    rc |= GetVector(ph, &v2);

    Scale3d(&scale, &v0);
    Scale3d(&scale, &v1);
    Scale3d(&scale, &v2);

    Rotate3d(&RotA, &v0); 
    Rotate3d(&RotA, &v1); 
    Rotate3d(&RotA, &v2); 

    Trans3d(&ctr, &v0);
    Trans3d(&ctr, &v1);
    Trans3d(&ctr, &v2);

    rt_tri(scene, tex, v1, v0, v2);
  }

  fclose(ifp);

  return rc;
}


static errcode GetClipGroup(parsehandle * ph, SceneHandle scene) {
  char objtype[256];

  if (fscanf(ph->ifp, "%s", objtype) == EOF) {
    return PARSEEOF;
  }

  if (!stringcmp(objtype, "NUMPLANES")) {
    int numplanes, i; 
    float * planes;

    if (fscanf(ph->ifp, "%d", &numplanes) != 1)
      return PARSEBADSYNTAX;

    planes = (float *) malloc(numplanes * 4 * sizeof(float));

    for (i=0; i<(numplanes * 4); i++) {
      if (fscanf(ph->ifp, "%f", &planes[i]) != 1)
        return PARSEBADSYNTAX;
    } 

    rt_clip_fv(scene, numplanes, planes);
    free(planes);

    return PARSENOERR;
  }

  return PARSEBADSYNTAX;
}


static errcode GetClipGroupEnd(parsehandle * ph, SceneHandle scene) {
  rt_clip_off(scene);
  return PARSENOERR;
}


#ifdef USELIBMGF
static errcode GetMGFFile(parsehandle * ph, SceneHandle scene) {
  char ifname[255];

  fscanf(ph->ifp, "%s", ifname); /* get MGF filename */
  if (ParseMGF(ifname, scene, 0) == MGF_NOERR)
    return PARSENOERR;
  
  return PARSENOERR; /* XXX hack */ 
}
#endif



 
