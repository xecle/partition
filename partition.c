/*
 * =====================================================================================
 *
 *       Filename:  partition.c
 *
 *    Description:  patition a disk to only one patition
 *
 *        Version:  1.0
 *        Created:  08/05/2013 04:39:21 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xecle , xecle.zhang@infotmic.com.com
 *        Company:  Shanghai Infotmic Co.,Ltd.
 *
 * =====================================================================================
 */

#include	<errno.h>
#include	<math.h>  
#include	<stdio.h> 
#include	<stdint.h> 
#include	<stdlib.h>
#include	<string.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<sys/ioctl.h>
#include	<sys/stat.h>
#include	<linux/fs.h>
#include	<linux/hdreg.h>

#define LOGE(...)   fprintf(stderr, "partition E:\t"__VA_ARGS__)
#define LOGD(...)   fprintf(stdout, "partition D:\t"__VA_ARGS__)

#define DISK_SIG    0x1b8
#define PART_ENTRY  0x1be
#define DISK_END    0x1fe

typedef struct {
    uint8_t head;
    uint8_t sector;
    uint8_t cylinder;
} __attribute__((__packed__)) Chs;

typedef struct {
    uint8_t active;
    Chs     chs_st;
    uint8_t type;
    Chs     chs_end;
    uint32_t start;
    uint32_t length;
}__attribute__((__packed__)) PartEntry;

struct part_info {
    int index;
    uint32_t start;
    uint32_t length;
    uint8_t active;
    uint8_t type;
};
struct disk_info {
    char *device;
    int fd;
    uint32_t id;
    uint64_t dsize;
    uint32_t start;
    uint32_t size;
    int     flags;
    int     sect_size;
    int     sect_num;
    int     head_num;
    int     cylinders;
    struct part_info parts[4];
};

struct write_list;
struct write_list {
    size_t offset;
    size_t length;
    void* data;
    struct write_list* next;
};

int write_to(int fd, struct write_list *list)
{
    struct write_list* wl = list;
    int ret;
    while(wl) {
        lseek(fd, wl->offset, SEEK_SET);
        ret = write(fd, wl->data, wl->length);
        free(wl->data);
        if(ret < 0) {
            LOGE("write to 0x%02lx failed %s\n", wl->offset, strerror(errno));
            break;
        }
        LOGD("write to 0x%02lx length %ld\n", wl->offset, wl->length);
        wl = wl->next;
        free(list);
        list = wl;
    }
    return ret;
}

void count_chs(struct disk_info* disk, Chs *chs, uint32_t len)
{
    if(len > 0xFFFFFF) {
        chs->head = 0xFE;
        chs->sector = 0xFF;
        chs->cylinder = 0xFF;
        return;
    }
    if(disk->sect_num == 0 || disk->head_num == 0) {
        return;
    }
    uint32_t l = len + disk->start;
    chs->sector = (uint8_t) ((l%disk->sect_num) & 0x3f);
    l /= disk->sect_num;
    chs->head = (uint8_t) ((l%disk->head_num) & 0xff);
    l /= disk->head_num;
    chs->cylinder = (uint8_t) (l&0xff);
    chs->sector |= (uint8_t) ((l>>2)&0xc0);
    LOGD("convert a length %d to chs %02x%02x%02x \n", len+disk->start, chs->head, chs->sector, chs->cylinder);
}

struct write_list* part2wl(struct disk_info* disk, struct part_info *part) {
    struct write_list* wl = malloc(sizeof(struct write_list));
    wl->offset = PART_ENTRY + part->index * sizeof(PartEntry);
    wl->length = sizeof(PartEntry);
    PartEntry* entry = malloc(sizeof(PartEntry));
    wl->data = entry;
    entry->active = part->active?0x80:0;
    count_chs(disk, &entry->chs_st, part->start+1);
    entry->type = part->type;
    if(part->length <= 0) {
        part->length = disk->size - (disk->start + part->start);
        LOGD("use full disk, length %d\n", part->length);
    }
    count_chs(disk, &entry->chs_end, part->start+part->length);
    entry->start = disk->start+part->start;
    entry->length = part->length;

    wl->next = NULL;
    LOGD("make a write list %p\n", wl);
    return wl;
}
static char* unit[]  = {"B", "K", "M", "G", "T", "P"};
static char human_read_size[10];
char* len2hrs(uint64_t len)
{
    int u = 0;
    while(len > 10240) {
        len >>= 10;
        u++;
    }
    float v;
    if(len>1024) {
        v = (float)len/1024.0f;
        u++;
    }else
        v = len;
    if(v>10) {
        sprintf(human_read_size, "%d%s", (int)v, unit[u]);
    } else {
        sprintf(human_read_size, "%.1f%s", v, unit[u]);
    }
    return human_read_size;
}


int main ( int argc, char *argv[] )
{
    uint32_t START; // start offset in MB

    struct disk_info disk;
    struct write_list *wl = NULL;
    struct hd_geometry geo;
    int sect_size = 0;

    disk.device = strdup(argv[1]);
    disk.fd = open(disk.device, O_RDWR);
    if(disk.fd<0){
        LOGE("can not open device %s : %s\n", disk.device, strerror(errno));
        goto err_1;
    }

    if (ioctl(disk.fd, BLKSSZGET, &sect_size) < 0) {
        LOGE("can not get block sector size\n");
        goto err_1;
    }
    if(sect_size)
        disk.sect_size = sect_size;
    else
        disk.sect_size = 512;
    LOGD("Disk sector size %d\n", disk.sect_size);

    if(argc > 2)
        START = atoi(argv[2]) * (1024*1024/sect_size);
    else
        START = 2048;
    LOGD("start is %d\n", START);

    if (ioctl(disk.fd, BLKGETSIZE64, &disk.dsize) < 0) {
        LOGE("can not get block size\n");
        goto err_1;
    }
    LOGD("Disk size %s\n", len2hrs(disk.dsize));

    disk.size = (uint32_t)disk.dsize/disk.sect_size;
    disk.dsize = (uint64_t)disk.size * (uint64_t)disk.sect_size;
    LOGD("Disk sectors %d\n", disk.size);

    if (ioctl(disk.fd, HDIO_GETGEO, &geo) < 0) {
        LOGD("Can not get geometry\n");
        goto err_1;
    }

    if(geo.sectors) {
        disk.sect_num = geo.sectors;
        disk.head_num = geo.heads;
    } else {
        disk.sect_num = 62;
        disk.head_num = 122;
    }
    LOGD("Disk geometry sector %d, head %d, cylinder %d\n", geo.sectors, geo.heads, geo.cylinders);

    disk.start = START;
    disk.flags = 0;
    lseek(disk.fd, DISK_SIG, SEEK_SET);
    read(disk.fd, &disk.id, 4);
    if(disk.id == 0) {
        srand(time(0));
        disk.id= rand();
        wl = malloc(sizeof(struct write_list));
        wl->offset = DISK_SIG;
        wl->length = 4;
        wl->data = malloc(4);
        wl->next = NULL;
        memcpy(wl->data, &disk.id, 4);
    }
    LOGD("Disk id is %08x\n", disk.id);

    struct part_info* p0 = &disk.parts[0];
    p0->start = 0;
    p0->index = 0;
    p0->length = 0;
    p0->active = 0;
    p0->type = 0x83;

    struct write_list* wltmp = part2wl(&disk, p0);
    wltmp->next = wl;
    wl = wltmp;
    
    /* clear left partition entry */
    wltmp = malloc(sizeof(struct write_list));
    wltmp->offset = PART_ENTRY+sizeof(PART_ENTRY);
    wltmp->length = 3*sizeof(PartEntry);
    wltmp->data = malloc(wltmp->length);
    memset(wltmp->data, 0, wltmp->length);
    wltmp->next = wl;
    wl = wltmp;


    /* disk end flags */
    wltmp = malloc(sizeof(struct write_list));
    wltmp->offset = DISK_END;
    wltmp->length = 2;
    wltmp->data = malloc(2);
    *((uint16_t*)wltmp->data) = (uint16_t)0xaa55;
    wltmp->next = wl;
    wl = wltmp;

    write_to(disk.fd, wl);
    syncfs(disk.fd);

    ioctl(disk.fd, BLKRRPART, NULL);
    close(disk.fd);

err_1:
    free(disk.device);
    return 0;
}

