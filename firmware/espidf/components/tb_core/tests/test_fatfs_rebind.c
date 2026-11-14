/* Real ESP-IDF 5.5.2 FatFs on a disposable, native RAM disk, never a device. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "ff.h"
#include "diskio.h"

#define SECTORS 512u
#define SECTOR_SIZE 4096u
static unsigned char disk[SECTORS*SECTOR_SIZE];
static bool fail_writes,fail_reads,write_protected;
static unsigned disk_writes;
PARTITION VolToPart[FF_VOLUMES]={{0,0},{1,0}};
DSTATUS disk_initialize(BYTE d){return d==0?0:STA_NOINIT;}
DSTATUS disk_status(BYTE d){return d==0?(write_protected?STA_PROTECT:0):STA_NOINIT;}
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
typedef void *TaskHandle_t;
static TaskHandle_t current_task=(void *)17;
static TaskHandle_t xTaskGetCurrentTaskHandle(void){return current_task;}
typedef struct{
    bool fatfs_called,stat_measured,partition_readonly;int fatfs_result,fatfs_flags,stat_result,file_attributes;
    uint32_t file_bytes,filesystem_type,root_entries;char path[96];
}store_io_failure_t;
static struct{const char *medium,*base_path;wl_handle_t flash_handle;}state={"flash","/tbflash",17};
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_FAT 2
typedef struct{bool readonly;}esp_partition_t;
static const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label){
    static const esp_partition_t partition={false};assert(type==1 && subtype==2 && !strcmp(label,"storage"));return &partition;
}
#include "rebind_under_test.h"
static void put(const char *name,const char *s);
static void expect(const char *name,const char *s);

static void access_denied_cases(void){
    FATFS fs;unsigned char work[SECTOR_SIZE];FIL file;FILINFO info;UINT n;
    MKFS_PARM options={.fmt=FM_FAT|FM_SFD,.n_fat=2,.n_root=128,.au_size=SECTOR_SIZE};
    memset(disk,0xff,sizeof(disk));assert(f_mkfs("0:",&options,work,sizeof(work))==FR_OK);
    assert(f_mount(&fs,"0:",1)==FR_OK);
    put("0:/00000001.LOG","PRESERVED\n");
    assert(f_chmod("0:/00000001.LOG",AM_RDO,AM_RDO)==FR_OK);
    assert(f_stat("0:/00000001.LOG",&info)==FR_OK && (info.fattrib&AM_RDO));
    assert(get_stat_mode(false)&S_IWUSR); /* stock IDF Unix mode cannot detect AM_RDO */
    assert(recovered_file_readonly("/tbflash/00000001.LOG"));
    for(unsigned i=0;i<24;i++){
        assert(rebind_flash_volume(17)==FR_OK);
        expect("0:/00000001.LOG","PRESERVED\n");
        store_io_failure_t traced={0};open_trace_begin();
        FRESULT result=f_open(&file,"0:/00000001.LOG",fat_mode_conv(O_RDWR|O_APPEND|O_CREAT));
        open_trace_end(&traced);
        assert(result==FR_DENIED && fresult_to_errno(result)==EACCES);
        assert(traced.fatfs_called && traced.fatfs_result==FR_DENIED && traced.fatfs_flags==(FA_READ|FA_WRITE|FA_OPEN_ALWAYS));
        strcpy(traced.path,"/tbflash/00000001.LOG");inspect_open_failure(&traced);
        assert(traced.stat_measured && traced.stat_result==FR_OK && (traced.file_attributes&AM_RDO) && traced.file_bytes==10);
    }
    assert(f_open(&file,"0:/00000002.LOG",fat_mode_conv(O_RDWR|O_APPEND|O_CREAT|O_EXCL))==FR_OK);
    assert(f_write(&file,"NEW_DURABLE\n",12,&n)==FR_OK && n==12);
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
    expect("0:/00000001.LOG","PRESERVED\n");expect("0:/00000002.LOG","NEW_DURABLE\n");
    assert(f_stat("0:/00000001.LOG",&info)==FR_OK && (info.fattrib&AM_RDO));
    assert(f_open(&file,"0:/00000002.LOG",fat_mode_conv(O_RDWR|O_APPEND|O_CREAT|O_EXCL))==FR_EXIST);
    expect("0:/00000002.LOG","NEW_DURABLE\n");
    write_protected=true;
    FRESULT result=f_open(&file,"0:/00000002.LOG",FA_WRITE);
    assert(result==FR_WRITE_PROTECTED && fresult_to_errno(result)==EACCES);
    write_protected=false;
    open_trace_begin();current_task=(void *)18;
    assert(f_open(&file,"0:/00000002.LOG",FA_READ)==FR_OK && f_close(&file)==FR_OK);
    current_task=(void *)17;store_io_failure_t ignored={0};open_trace_end(&ignored);assert(!ignored.fatfs_called);
    assert(f_mount(NULL,"0:",0)==FR_OK);
    /* Separate disposable fixture: free data clusters do not imply free FAT
     * directory entries. This case CANNOT be fixed by deleting protected data. */
    memset(disk,0xff,sizeof(disk));assert(f_mkfs("0:",&options,work,sizeof(work))==FR_OK);
    assert(f_mount(&fs,"0:",1)==FR_OK);
    DWORD before,after;FATFS *mounted=NULL;
    assert(f_getfree("0:",&before,&mounted)==FR_OK && before>100);
    for(unsigned i=0;i<fs.n_rootdir;i++){
        char path[24];snprintf(path,sizeof(path),"0:/F%07u.TMP",i);put(path,"");
    }
    assert(f_getfree("0:",&after,&mounted)==FR_OK && before==after);
    assert(f_stat("0:/00000001.LOG",&info)==FR_NO_FILE);
    result=f_open(&file,"0:/00000001.LOG",FA_READ|FA_WRITE|FA_CREATE_NEW);
    assert(result==FR_DENIED && fresult_to_errno(result)==EACCES);
    store_io_failure_t missing={0};strcpy(missing.path,"/tbflash/00000001.LOG");inspect_open_failure(&missing);
    assert(missing.stat_measured && missing.stat_result==FR_NO_FILE && missing.root_entries==128);
    assert(rebind_flash_volume(17)==FR_OK);
    assert(f_open(&file,"0:/00000001.LOG",FA_READ|FA_WRITE|FA_CREATE_NEW)==FR_DENIED);
    assert(f_mount(NULL,"0:",0)==FR_OK);
    puts("Actual FatFs: 24 successful scans/remounts still OPEN/EACCES on AM_RDO; new exclusive segment works, old bytes/attrs remain; full directory with free clusters stays denied; exact FRESULT trace PASS");
}

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

static unsigned visible_root_entries(void){
    FF_DIR directory;FILINFO info;unsigned count=0;
    assert(f_opendir(&directory,"0:/")==FR_OK);
    for(;;){assert(f_readdir(&directory,&info)==FR_OK);if(!info.fname[0])break;++count;}
    assert(f_closedir(&directory)==FR_OK);return count;
}

/* Boot 559: FS_FAT12, 512 root slots, absent 00000001.LOG, CREATE_NEW=7,
 * OPEN=FR_DENIED, stat=FR_NO_FILE, no recognized LOG segments. This test
 * deliberately constructs two DIFFERENT RAM-only directory states; it does
 * not assert which state exists on the truck and never changes production. */
static void field_root_denied_cases(void){
    FATFS fs;unsigned char work[SECTOR_SIZE];FIL file;FILINFO info;
    MKFS_PARM options={.fmt=FM_FAT|FM_SFD,.n_fat=2,.n_root=512,.au_size=SECTOR_SIZE};
    for(unsigned corrupt=0;corrupt<2;corrupt++){
        memset(disk,0xff,sizeof(disk));assert(f_mkfs("0:",&options,work,sizeof(work))==FR_OK);
        assert(f_mount(&fs,"0:",1)==FR_OK);
        assert(fs.fs_type==FS_FAT12 && fs.n_rootdir==512);
        DWORD free_before,free_after;FATFS *mounted=NULL;
        assert(f_getfree("0:",&free_before,&mounted)==FR_OK && free_before>100);
        size_t root_offset=(size_t)fs.dirbase*SECTOR_SIZE,root_bytes=(size_t)fs.n_rootdir*32;
        assert(root_offset+root_bytes<=sizeof(disk));
        if(!corrupt){
            for(unsigned i=0;i<512;i++){
                char path[24];snprintf(path,sizeof(path),"0:/F%07u.TMP",i);put(path,"");
            }
            assert(visible_root_entries()==512);
        }else{
            /* Orphaned LFN slots are not visible as files, but are not free
             * slots either. Inject only into this disposable, unmounted RAM
             * fixture. A root scan can report zero LOGs without an empty root. */
            assert(f_mount(NULL,"0:",0)==FR_OK);
            memset(disk+root_offset,0xff,root_bytes);
            for(unsigned i=0;i<512;i++){
                unsigned char *entry=disk+root_offset+i*32;
                entry[0]=0x41;entry[11]=0x0f; /* LFN attribute, internal AM_LFN */
                entry[12]=0;entry[26]=entry[27]=0;
            }
            assert(f_mount(&fs,"0:",1)==FR_OK && visible_root_entries()==0);
        }
        assert(f_getfree("0:",&free_after,&mounted)==FR_OK && free_after==free_before);
        unsigned char *preserved=malloc(root_bytes);assert(preserved);
        memcpy(preserved,disk+root_offset,root_bytes);
        unsigned writes_before=disk_writes;
        for(unsigned retry=0;retry<3;retry++){
            store_io_failure_t traced={0};open_trace_begin();
            FRESULT result=f_open(&file,"0:/00000001.LOG",fat_mode_conv(O_RDWR|O_APPEND|O_CREAT|O_EXCL));
            open_trace_end(&traced);
            assert(result==FR_DENIED && fresult_to_errno(result)==EACCES);
            assert(traced.fatfs_called && traced.fatfs_result==7 && traced.fatfs_flags==7);
            assert(f_stat("0:/00000001.LOG",&info)==FR_NO_FILE);
            strcpy(traced.path,"/tbflash/00000001.LOG");inspect_open_failure(&traced);
            assert(traced.stat_measured && traced.stat_result==4 && traced.filesystem_type==1 && traced.root_entries==512);
            assert(!traced.partition_readonly && !recovered_file_readonly(traced.path));
            assert(rebind_flash_volume(17)==FR_OK);
            assert(disk_writes==writes_before && !memcmp(preserved,disk+root_offset,root_bytes));
        }
        free(preserved);assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    puts("Boot 559 reproduction: FAT12/512 slots, CREATE_NEW flags7, absent LOG, FR_DENIED7/EACCES13/stat4 with free clusters; full foreign directory OR invisible orphan LFNs; three rebinds do not fix either; zero writes and exact root bytes preserved PASS");
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
    access_denied_cases();
    field_root_denied_cases();
    puts("Actual IDF FatFs: failed sync/close leaves FR_LOCKED; same-volume rebind clears it without writes, preserves confirmed bytes/archives, persistent IO stays failed PASS");
    return 0;
}
