/*
 * dencopy.c
 *
 * Copy a density file.  The destination file is in the native byte ordering,
 * even if the source file is not.
 *
 * Usage: dencopy src.den dst.den
 * Compilation: cc -o dencopy dencopy.c denfile.c
 */

#include <stdio.h>

main(argc, argv)
int argc;
char **argv;
{
    unsigned char *data, *read_den();
    int xlen, ylen, zlen;

    if (argc != 3) {
	fprintf(stderr, "Usage: dencopy src.den dst.den\n");
	exit(1);
    }
    if ((data = read_den(argv[1], &xlen, &ylen, &zlen)) == NULL) {
	fprintf(stderr, "dencopy: read failed\n");
	exit(1);
    }
    if (!write_den(argv[2], data, xlen, ylen, zlen)) {
	fprintf(stderr, "dencopy: write failed\n");
	exit(1);
    }
    return(0);
}
