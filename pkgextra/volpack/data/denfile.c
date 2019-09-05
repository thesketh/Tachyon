/*
 * denfile.c
 *
 * Routines to read and write .den files.
 * See the file dencopy.c for sample usage.
 */

#include <stdio.h>

#ifndef MIN
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#endif

/* constants for .den magic number (map_version field) */
#define	MAP_CUR_VERSION		1	/* current version */
#define	MAP_CUR_VERSION_SWAB	0x0100	/* byte-swapped current version */

#define MAX_READ_SIZE	8192	/* maximum # of bytes per read(2) call */

/*
 * read_den
 *
 * Load density data from a file.  Return an array of bytes on success, or
 * NULL on failure.  The array dimensions are stored in xptr, yptr and zptr.
 */

unsigned char *
read_den(filename, xptr, yptr, zptr)
char *filename;		/* name of file containing data */
int *xptr, *yptr, *zptr;/* volume dimensions (output parameters) */
{
    int fd;			/* file descriptor */
    unsigned char *data;	/* data array */
    int swapbytes;		/* true if header must be byte-swapped */
    short map_version;		/* Version of this .den file                 */
    short orig_min[3];		/* Dimensions of original data file          */
    short orig_max[3];
    short orig_len[3];
    short extr_min[3];		/* Extracted portion of original file        */
    short extr_max[3];		/*   (mins and maxes will be subset of       */
    short extr_len[3];		/*    orig and lengths will be <= orig)      */
    short map_min[3];		/* Dimensions of this map                    */
    short map_max[3];		/*   (mins will be 0 in this program and     */
    short map_len[3];		/*    lens may be != extr if warps > 0)      */
    short map_warps;		/* Number of warps since extraction          */
				/*   (0 = none)                              */
    int map_length;		/* Total number of densities in map          */
				/*   (= product of lens)                     */

    /* open the file */
    if ((fd = open(filename, 0)) < 0) {
	fprintf(stderr, "cannot open file %s\n", filename);
	return(NULL);
    }

    /* read the magic number */
    if (!read_shorts(fd, &map_version, 1, 0)) {
	fprintf(stderr, "read failed on file %s (empty file?)\n", filename);
	return(NULL);
    }
    if (map_version == MAP_CUR_VERSION) {
	swapbytes = 0;
    } else if (map_version == MAP_CUR_VERSION_SWAB) {
	swapbytes = 1;
    } else {
	fprintf(stderr, "file %s is not a density file\n", filename);
	return(NULL);
    }

    /* read the volume size information */
    if (!read_shorts(fd, orig_min, 3, swapbytes) ||
	!read_shorts(fd, orig_max, 3, swapbytes) ||
	!read_shorts(fd, orig_len, 3, swapbytes) ||
	!read_shorts(fd, extr_min, 3, swapbytes) ||
	!read_shorts(fd, extr_max, 3, swapbytes) ||
	!read_shorts(fd, extr_len, 3, swapbytes) ||
	!read_shorts(fd, map_min, 3, swapbytes) ||
	!read_shorts(fd, map_max, 3, swapbytes) ||
	!read_shorts(fd, map_len, 3, swapbytes) ||
	!read_shorts(fd, &map_warps, 1, swapbytes) ||
	!read_words(fd, &map_length, 1, swapbytes)) {
	fprintf(stderr, "read failed on file %s (truncated file?)\n",filename);
	return(NULL);
    }
    if (map_length != map_len[0]*map_len[1]*map_len[2]) {
	fprintf(stderr, "density file %s has an inconsistent header\n",
		filename);
	return(NULL);
    }

    /* allocate array for data */
    data = (unsigned char *)malloc(map_length);
    if (data == NULL) {
	fprintf(stderr, "out of memory\n");
	return(NULL);
    }

    /* copy the data from the file */
    if (!read_bytes(fd, (char *)data, map_length)) {
	fprintf(stderr, "read failed on file %s\n", filename);
	close(fd);
	free(data);
	return(NULL);
    }

    /* finish up */
    close(fd);
    *xptr = map_len[0];
    *yptr = map_len[1];
    *zptr = map_len[2];
    return(data);
}


/*
 * write_den
 *
 * Write an array of volume data to a .den file.  Return 1 on success,
 * 0 on failure.
 */

int
write_den(filename, data, xlen, ylen, zlen)
char *filename;		/* name of file to create */
unsigned char *data;	/* volume data */
int xlen, ylen, zlen;	/* volume dimensions */
{
    int fd;			/* file descriptor */
    short map_version;		/* Version of this .den file                 */
    short orig_min[3];		/* Dimensions of original data file          */
    short orig_max[3];
    short orig_len[3];
    short extr_min[3];		/* Extracted portion of original file        */
    short extr_max[3];		/*   (mins and maxes will be subset of       */
    short extr_len[3];		/*    orig and lengths will be <= orig)      */
    short map_min[3];		/* Dimensions of this map                    */
    short map_max[3];		/*   (mins will be 0 in this program and     */
    short map_len[3];		/*    lens may be != extr if warps > 0)      */
    short map_warps;		/* Number of warps since extraction          */
				/*   (0 = none)                              */
    int map_length;		/* Total number of densities in map          */
				/*   (= product of lens)                     */

    /* open file */
    if ((fd = creat(filename, 0644)) < 0) {
	fprintf(stderr, "cannot open file %s\n", filename);
	return(0);
    }

    /* write the header */
    map_version = MAP_CUR_VERSION;
    orig_min[0] = 0;
    orig_min[1] = 0;
    orig_min[2] = 0;
    orig_max[0] = xlen - 1;
    orig_max[1] = ylen - 1;
    orig_max[2] = zlen - 1;
    orig_len[0] = xlen;
    orig_len[1] = ylen;
    orig_len[2] = zlen;
    extr_min[0] = 0;
    extr_min[1] = 0;
    extr_min[2] = 0;
    extr_max[0] = xlen - 1;
    extr_max[1] = ylen - 1;
    extr_max[2] = zlen - 1;
    extr_len[0] = xlen;
    extr_len[1] = ylen;
    extr_len[2] = zlen;
    map_min[0] = 0;
    map_min[1] = 0;
    map_min[2] = 0;
    map_max[0] = xlen - 1;
    map_max[1] = ylen - 1;
    map_max[2] = zlen - 1;
    map_len[0] = xlen;
    map_len[1] = ylen;
    map_len[2] = zlen;
    map_warps = 0;
    map_length = xlen * ylen * zlen;
    if (!write_bytes(fd, (char *)&map_version, sizeof(short)) ||
	!write_bytes(fd, (char *)orig_min, 3*sizeof(short)) ||
	!write_bytes(fd, (char *)orig_max, 3*sizeof(short)) ||
	!write_bytes(fd, (char *)orig_len, 3*sizeof(short)) ||
	!write_bytes(fd, (char *)extr_min, 3*sizeof(short)) ||
	!write_bytes(fd, (char *)extr_max, 3*sizeof(short)) ||
	!write_bytes(fd, (char *)extr_len, 3*sizeof(short)) ||
	!write_bytes(fd, (char *)map_min, 3*sizeof(short)) ||
	!write_bytes(fd, (char *)map_max, 3*sizeof(short)) ||
	!write_bytes(fd, (char *)map_len, 3*sizeof(short)) ||
	!write_bytes(fd, (char *)&map_warps, sizeof(short)) ||
	!write_bytes(fd, (char *)&map_length, sizeof(unsigned)) ||
	!write_bytes(fd, (char *)data, map_length)) {
	close(fd);
	fprintf(stderr, "cannot write to file %s\n", filename);
	return(0);
    }
    close(fd);
    return(1);
}

/*
 * read_bytes
 *
 * Read data from a file.  The data is assumed to be a sequence of bytes,
 * so no byte-swapping is needed.  If the read request is a large one then
 * it is broken up into chunks so the user can interrupt.
 *
 * Return value is 1 if the read was succesful or 0 if there was a read
 * error or the full number of bytes could not be read.
 */

read_bytes(fd, buf, bytecount)
int fd;		/* file descriptor to read from */
char *buf;	/* memory in which to store data */
int bytecount;	/* number of bytes to read */
{
    int n;

    while (bytecount > 0) {
	n = MIN(bytecount, MAX_READ_SIZE);
	if (read(fd, buf, n) != n)
	    return(0);
	buf += n;
	bytecount -= n;
    }
    return(1);
}

/*
 * read_shorts
 *
 * Read data from a file.  The data is assumed to be a sequence of shorts,
 * and byte-swapping is performed if requested.  If the read request is a
 * large one then it is broken up into chunks so the user can interrupt.
 *
 * Return value is 1 if the read was succesful or 0 if there was a read
 * error or the full number of shorts could not be read.
 */

read_shorts(fd, sbuf, shortcount, swap)
int fd;		/* file descriptor to read from */
short *sbuf;	/* memory in which to store data */
int shortcount;	/* number of shorts to read */
int swap;	/* if nonzero then swap bytes */
{
    int n, c;
    int bytecount = shortcount * 2;
    char tmp0, tmp1, tmp2, tmp3;
    char *buf = (char *)sbuf;

    while (bytecount > 0) {
	n = MIN(bytecount, MAX_READ_SIZE);
	if (read(fd, buf, n) != n)
	    return(0);
	bytecount -= n;
	if (swap) {
	    c = n / 8;
	    n -= c * 8;
	    for (; c > 0; c--) {
		tmp0 = buf[0]; buf[0] = buf[1]; buf[1] = tmp0;
		tmp1 = buf[2]; buf[2] = buf[3]; buf[3] = tmp1;
		tmp2 = buf[4]; buf[4] = buf[5]; buf[5] = tmp2;
		tmp3 = buf[6]; buf[6] = buf[7]; buf[7] = tmp3;
		buf += 8;
	    }
	    for (; n > 0; n -= 2) {
		tmp0 = buf[0]; buf[0] = buf[1]; buf[1] = tmp0;
		buf += 2;
	    }
	} else {
	    buf += n;
	}
    }
    return(1);
}

/*
 * read_words
 *
 * Read data from a file.  The data is assumed to be a sequence of words,
 * and byte-swapping is performed if requested.  If the read request is a
 * large one then it is broken up into chunks so the user can interrupt.
 *
 * Return value is 1 if the read was succesful or 0 if there was a read
 * error or the full number of words could not be read.
 */

read_words(fd, wbuf, wordcount, swap)
int fd;		/* file descriptor to read from */
int *wbuf;	/* memory in which to store data */
int wordcount;	/* number of words to read */
int swap;	/* if nonzero then swap bytes */
{
    int n, c;
    int bytecount = wordcount * 4;
    char tmp0, tmp1, tmp2, tmp3;
    char *buf = (char *)wbuf;

    while (bytecount > 0) {
	n = MIN(bytecount, MAX_READ_SIZE);
	if (read(fd, buf, n) != n)
	    return(0);
	bytecount -= n;
	if (swap) {
	    c = n / 8;
	    n -= c * 8;
	    for (; c > 0; c--) {
		tmp0 = buf[0]; buf[0] = buf[3]; buf[3] = tmp0;
		tmp1 = buf[1]; buf[1] = buf[2]; buf[2] = tmp1;
		tmp2 = buf[4]; buf[4] = buf[7]; buf[7] = tmp2;
		tmp3 = buf[5]; buf[5] = buf[6]; buf[6] = tmp3;
		buf += 8;
	    }
	    for (; n > 0; n -= 4) {
		tmp0 = buf[0]; buf[0] = buf[3]; buf[3] = tmp0;
		tmp1 = buf[1]; buf[1] = buf[2]; buf[2] = tmp1;
		buf += 4;
	    }
	} else {
	    buf += n;
	}
    }
    return(1);
}


/*
 * write_bytes
 *
 * Write data to a file.  If the write request is a large one then
 * it is broken up into chunks so the user can interrupt.
 *
 * Return value is 1 if the write was succesful or 0 if there was an error.
 */

write_bytes(fd, buf, bytecount)
int fd;		/* file descriptor to write to */
char *buf;	/* memory containing data */
int bytecount;	/* number of bytes to write */
{
    int n;

    while (bytecount > 0) {
	n = MIN(bytecount, MAX_READ_SIZE);
	if (write(fd, buf, n) != n)
	    return(0);
	buf += n;
	bytecount -= n;
    }
    return(1);
}
