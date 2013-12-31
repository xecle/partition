/*
 * =====================================================================================
 *
 *       Filename:  geometry.c
 *
 *    Description:  test geometry
 *
 *        Version:  1.0
 *        Created:  07/29/2013 10:21:57 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xecle , xecle.zhang@infotmic.com.com
 *        Company:  Shanghai Infotmic Co.,Ltd.
 *
 * =====================================================================================
 */

#include	<errno.h>
#include	<stdio.h> 
#include	<stdlib.h>
#include	<string.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<sys/ioctl.h>
#include	<linux/fs.h>
#include	<linux/hdreg.h>
#include	<sys/stat.h>


int main ( int argc, char *argv[] )
{
    struct hd_geometry geo;
    int fd = open(argv[1], O_RDWR);
    int ret = ioctl(fd, HDIO_GETGEO, &geo);
    if(ret < 0) {
        printf("error %s !\n", strerror(errno));
    }
    close(fd);

    printf("geometry : head %d, sector %d, cylinder %d, start %ld\n", 
            geo.heads, geo.sectors, geo.cylinders, geo.start);

    return EXIT_SUCCESS;
}

