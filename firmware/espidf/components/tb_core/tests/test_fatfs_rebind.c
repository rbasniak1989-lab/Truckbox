/* Real ESP-IDF 5.5.2 FatFs on a disposable, native RAM disk, never a device. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"

#define SECTORS 512u
#define SECTOR_SIZE 4096u
static unsigned char disk[SECTORS*SECTOR_SIZE];
static bool fail_writes,fail_reads;
static unsigned disk_writes;
PARTITION VolToPart[FF_VOLUMES]={{0,0},{1,0}};
DSTATUS disk_initialize(BYTE d){return d==0?0:STA_NOINIT;}
DSTATUS disk_status(BYTE d){return d==0?0:STA_NOINIT;}
DRESULT disk_read(BYTE d,BYTE *out,LBA_t sector,UINT count){
    if(d!=0 || sector+count>SECTORS)return RES_PARERR;
    if(fail_reads)return RES_ERROR;
    memcpy(out,disk+sector*SECTOR_SIZE,count*SECTOR_SIZE);return RES_OK;
}
DRESULT disk_write(BYTE d,const BYTE *in,LBA_t sector,UINT count){
    if(d!=0 || sector+count>SECTORS)return RES_PARERR;
    if(fail_writes)return RES_ERROR;
    ++disk_writes;memcpy(disk+sector*SECTOR_SIZE,in,count*SECTOR_SIZE);return RES_OK;
}
DRESULT disk_ioctl(BYTE d,BYTE cmd,void *p){
    if(d!=0)return RES_PARERR;
    switch(cmd){
    case CTRL_SYNC:return fail_writes?RES_ERROR:RES_OK;
    case GET_SECTOR_COUNT:*(LBA_t *)p=SECTORS;return RES_OK;
    case GET_SECTOR_SIZE:*(WORD *)p=SECTOR_SIZE;return RES_OK;
    case GET_BLOCK_SIZE:*(DWORD *)p=1;return RES_OK;
    case CTRL_TRIM:return RES_OK;
    default:return RES_PARERR;
    }
}
DWORD get_fattime(void){return (46u<<25)|(11u<<21)|(13u<<16);}
void *ff_memalloc(unsigned n){return malloc(n);}
void ff_memfree(void *p){free(p);}
int ff_mutex_create(int n){(void)n;return 1;}
void ff_mutex_delete(int n){(void)n;}
int ff_mutex_take(int n){(void)n;return 1;}
void ff_mutex_give(int n){(void)n;}
typedef int wl_handle_t;
static unsigned char ff_diskio_get_pdrv_wl(wl_handle_t h){return h==17?0:0xff;}
#include "rebind_under_test.h"

static void put(const char *name,const char *s){
    FIL file;UINT n;assert(f_open(&file,name,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&file,s,(UINT)strlen(s),&n)==FR_OK && n==strlen(s));
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void expect(const char *name,const char *s){
    FIL file;char text[80]={0};UINT n;
    assert(f_open(&file,name,FA_READ)==FR_OK);
    assert(f_read(&file,text,sizeof(text)-1,&n)==FR_OK);
    assert(n==strlen(s) && !strcmp(text,s));assert(f_close(&file)==FR_OK);
}
int main(void){
    FATFS fs;unsigned char work[SECTOR_SIZE];
    /* Formatting is ONLY for this disposable RAM fixture. Recovery below uses
     * the extracted production function, which never calls f_mkfs. */
    MKFS_PARM options={.fmt=FM_FAT|FM_SFD,.n_fat=2,.au_size=SECTOR_SIZE};
    assert(f_mkfs("0:",&options,work,sizeof(work))==FR_OK);
    assert(f_mount(&fs,"0:",1)==FR_OK);
    put("0:/00000001.LOG","CONFIRMED\n");put("0:/00000002.BAD","EVIDENCE\n");
    FIL file;UINT n;
    assert(f_open(&file,"0:/00000001.LOG",FA_WRITE|FA_OPEN_APPEND)==FR_OK);
    assert(f_write(&file,"RETAINED\n",9,&n)==FR_OK && n==9);
    fail_writes=true;
    assert(f_sync(&file)==FR_DISK_ERR && f_close(&file)==FR_DISK_ERR);
    /* IDF's VFS close discards its FIL after this error, while FatFs' sharing
     * entry remains locked. The old read-only recovery cannot clear it. */
    memset(&file,0,sizeof(file));fail_writes=false;
    assert(f_open(&file,"0:/00000001.LOG",FA_READ)==FR_LOCKED);
    unsigned before=disk_writes;
    assert(rebind_flash_volume(17)==FR_OK && disk_writes==before);
    expect("0:/00000001.LOG","CONFIRMED\n");expect("0:/00000002.BAD","EVIDENCE\n");
    assert(f_open(&file,"0:/00000001.LOG",FA_WRITE|FA_OPEN_APPEND)==FR_OK);
    assert(f_write(&file,"RETAINED\n",9,&n)==FR_OK && n==9);
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
    expect("0:/00000001.LOG","CONFIRMED\nRETAINED\n");
    /* A persistent electrical/media write failure remains a failure after
     * rebind; recovery must never pretend that a scan made this row durable. */
    fail_writes=true;
    for(unsigned retry=0;retry<3;retry++){
        assert(rebind_flash_volume(17)==FR_OK);
        assert(f_open(&file,"0:/00000001.LOG",FA_WRITE|FA_OPEN_APPEND)==FR_OK);
        assert(f_write(&file,"UNCONFIRMED\n",12,&n)==FR_OK && n==12);
        assert(f_sync(&file)==FR_DISK_ERR && f_close(&file)==FR_DISK_ERR);
        memset(&file,0,sizeof(file));
    }
    fail_writes=false;assert(rebind_flash_volume(17)==FR_OK);
    expect("0:/00000001.LOG","CONFIRMED\nRETAINED\n");
    fail_reads=true;assert(rebind_flash_volume(17)!=FR_OK);
    assert(rebind_flash_volume(17)!=FR_OK);fail_reads=false;
    assert(rebind_flash_volume(17)==FR_OK);
    expect("0:/00000001.LOG","CONFIRMED\nRETAINED\n");
    expect("0:/00000002.BAD","EVIDENCE\n");
    assert(rebind_flash_volume(99)==FR_INVALID_DRIVE);
    assert(f_mount(NULL,"0:",0)==FR_OK);
    puts("Actual IDF FatFs: failed sync/close leaves FR_LOCKED; same-volume rebind clears it without writes, preserves confirmed bytes/archives, persistent IO stays failed PASS");
    return 0;
}
