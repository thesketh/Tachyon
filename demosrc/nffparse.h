/*
 * definitions for parsing NFF model files
 * 
 *  $Id: nffparse.h,v 1.4 2011/02/02 06:10:39 johns Exp $
 */

#define NFFNOERR     0
#define NFFBADFILE   1
#define NFFBADSYNTAX 2
#define NFFEOF       3

unsigned int ParseNFF(char *nffname, SceneHandle scene);


