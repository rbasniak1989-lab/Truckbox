/* Included by store.c: same mutex as writer/recovery/batch/ACK. No persistent
 * wipe intent exists: startup, OTA, retry and reboot NEVER invoke formatting. */
#define MAINTENANCE_TTL_US UINT64_C(120000000)
static struct {
    char challenge[33];
    uint64_t expires_us;
    unsigned attempts, successes;
    bool running, probe_durable;
    const char *phase, *stage;
    int result, error;
} maintenance;

#ifndef TB_STORE_HOST_TEST
static const esp_partition_t *maintenance_partition(void)
{
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_FAT,"storage");
    return p && !p->readonly && p->address==0x620000 && p->size==0x1e0000 ? p:NULL;
}
static bool maintenance_target_valid(void)
{
    esp_ota_img_states_t status;
    const esp_partition_t *running=esp_ota_get_running_partition();
    return maintenance_partition() && state.nvs_open &&
        (!state.medium || strcmp(state.medium,"sd")) && !state.sd_card &&
        (!state.previous_medium[0] || !strcmp(state.previous_medium,"flash")) &&
        running && esp_ota_get_state_partition(running,&status)==ESP_OK && status==ESP_OTA_IMG_VALID;
}

/* Existing registered VFS/WL stays in place. All FILE/DIR users hold the
 * caller's store mutex; only the FatFs volume is detached and recreated. */
static int maintenance_reformat_volume(wl_handle_t handle)
{
    unsigned char drive=ff_diskio_get_pdrv_wl(handle);
    if(drive==0xff)return FR_INVALID_DRIVE;
    char volume[5];snprintf(volume,sizeof(volume),"%u:",(unsigned)drive);
    FATFS *fs=NULL;DWORD free_clusters;
    FRESULT result=f_getfree(volume,&free_clusters,&fs);
    if(!fs)return result==FR_OK?FR_INVALID_OBJECT:result;
    void *work=malloc(4096);if(!work)return FR_NOT_ENOUGH_CORE;
    maintenance.stage="UNMOUNT";
    result=f_mount(NULL,volume,0);
    if(result==FR_OK){
        maintenance.stage="MKFS";
        const MKFS_PARM options={.fmt=FM_FAT|FM_SFD,.n_fat=2,.n_root=512,.au_size=4096};
        result=f_mkfs(volume,&options,work,4096);
        FRESULT mounted=f_mount(fs,volume,1);
        if(result==FR_OK && mounted!=FR_OK){maintenance.stage="REMOUNT";result=mounted;}
    }
    free(work);return result;
}

static esp_err_t maintenance_format_media(void)
{
    if(state.flash_handle!=WL_INVALID_HANDLE){
        int result=maintenance_reformat_volume(state.flash_handle);
        maintenance.result=result;return result==FR_OK?ESP_OK:ESP_FAIL;
    }
    /* Explicit confirmation only: also recover an unmountable volume after a
     * power cut during formatting. This config is NEVER used by boot/retry. */
    maintenance.stage="FORMAT_UNMOUNTED";
    esp_vfs_fat_mount_config_t config={.format_if_mount_failed=true,.max_files=5,.allocation_unit_size=4096};
    esp_err_t result=esp_vfs_fat_spiflash_format_cfg_rw_wl("/tbflash","storage",&config);
    if(result!=ESP_OK)return result;
    config.format_if_mount_failed=false;
    result=esp_vfs_fat_spiflash_mount_rw_wl("/tbflash","storage",&config,&state.flash_handle);
    if(result!=ESP_OK)return result;
    state.base_path="/tbflash";state.medium="flash";
    state.maximum_segments=37;state.segment_bytes=STORE_FLASH_SEGMENT_BYTES;
    state.medium_initialized=true;
    return ESP_OK;
}
#endif

/* Kept as evidence on the freshly formatted volume. Not a Cloud sample and
 * never offered/ACKed. Exclusive creation + full durability + readback. */
static bool maintenance_probe(void)
{
    char path[96];snprintf(path,sizeof(path),"%s/TBPROBE.CHK",state.base_path);
    const char expected[]="TRUCKBOX_FLASH_PROBE_V1\n";
    maintenance.stage="PROBE_OPEN";errno=0;
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0){maintenance.error=errno;return false;}
    maintenance.stage="PROBE_FDOPEN";errno=0;
    FILE *file=fdopen(fd,"wb");if(!file){maintenance.error=errno;close(fd);return false;}
    maintenance.stage="PROBE_WRITE";errno=0;
    bool ok=fwrite(expected,1,sizeof(expected)-1,file)==sizeof(expected)-1;
    if(!ok)maintenance.error=errno;
    if(ok){maintenance.stage="PROBE_FLUSH";errno=0;ok=fflush(file)==0;if(!ok)maintenance.error=errno;}
    if(ok){maintenance.stage="PROBE_SYNC";errno=0;ok=fsync(fileno(file))==0;if(!ok)maintenance.error=errno;}
    if(ok){maintenance.stage="PROBE_CLOSE";errno=0;ok=fclose(file)==0;if(!ok)maintenance.error=errno;}
    else fclose(file); /* preserve the first failure */
    if(!ok)return false;
    maintenance.stage="PROBE_READ_OPEN";errno=0;
    char actual[sizeof(expected)]={0};file=fopen(path,"rb");if(!file){maintenance.error=errno;return false;}
    maintenance.stage="PROBE_READ";errno=0;
    ok=fread(actual,1,sizeof(actual),file)==sizeof(expected)-1 && !ferror(file) &&
        !memcmp(actual,expected,sizeof(expected)-1);
    if(!ok)maintenance.error=errno;
    if(ok){maintenance.stage="PROBE_READ_CLOSE";errno=0;ok=fclose(file)==0;if(!ok)maintenance.error=errno;}
    else fclose(file);
    return ok;
}

static bool maintenance_empty_volume(void)
{
    DIR *directory=opendir(state.base_path);if(!directory)return false;
    bool empty=true;struct dirent *entry;
    for(;;){
        errno=0;entry=readdir(directory);
        if(!entry){if(errno)empty=false;break;}
        if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")){empty=false;break;}
    }
    if(closedir(directory)!=0)empty=false;
    return empty;
}

esp_err_t tb_store_maintenance_prepare(char challenge[33])
{
    if(!challenge || !maintenance_target_valid())return ESP_ERR_INVALID_STATE;
    if(!state.mutex || xSemaphoreTake(state.mutex,pdMS_TO_TICKS(200))!=pdTRUE)return ESP_ERR_TIMEOUT;
    if(maintenance.running || state.rollback_paused || !maintenance_target_valid()){
        xSemaphoreGive(state.mutex);return ESP_ERR_INVALID_STATE;
    }
    unsigned char random[16];esp_fill_random(random,sizeof(random));
    for(unsigned i=0;i<sizeof(random);i++)snprintf(maintenance.challenge+i*2,3,"%02x",random[i]);
    memcpy(challenge,maintenance.challenge,33);
    maintenance.expires_us=(uint64_t)esp_timer_get_time()+MAINTENANCE_TTL_US;
    maintenance.phase="PREPARED";
    xSemaphoreGive(state.mutex);return ESP_OK;
}

esp_err_t tb_store_maintenance_format(const char *challenge)
{
    if(!maintenance_target_valid())return ESP_ERR_INVALID_STATE;
    if(!state.mutex || xSemaphoreTake(state.mutex,pdMS_TO_TICKS(200))!=pdTRUE)return ESP_ERR_TIMEOUT;
    uint64_t now=(uint64_t)esp_timer_get_time();
    bool valid=challenge && strlen(challenge)==32 && maintenance.challenge[0] &&
        !strcmp(challenge,maintenance.challenge) && now<maintenance.expires_us &&
        !maintenance.running && !state.rollback_paused && maintenance_target_valid();
    if(!valid){xSemaphoreGive(state.mutex);return ESP_ERR_INVALID_STATE;}
    /* Consume BEFORE the first destructive operation. A lost HTTP response or
     * power cut cannot replay this permission or authorize a second format. */
    memset(maintenance.challenge,0,sizeof(maintenance.challenge));maintenance.expires_us=0;
    maintenance.running=true;maintenance.probe_durable=false;maintenance.phase="FORMATTING";
    ++maintenance.attempts;maintenance.stage="NVS_FENCE";maintenance.result=0;maintenance.error=0;
    esp_err_t result=nvs_set_u32(state.nvs,"keep_through",0);
    if(result==ESP_OK)result=nvs_commit(state.nvs);
    if(result==ESP_OK){
        state.preserve_through=0;state.preserve_fence_set=true;
        state.ready=false;state.write_confirmed=false;state.recovery_complete=false;
        state.highest_offered=0;state.io_paused=true;state.initializing=true;
        result=maintenance_format_media();
        if(result==ESP_OK){
            maintenance.stage="EMPTY_VOLUME";
            if(!maintenance_empty_volume())result=ESP_FAIL;
        }
        if(result==ESP_OK){
            maintenance.stage="DURABLE_PROBE";
            if(!maintenance_probe())result=ESP_FAIL;else maintenance.probe_durable=true;
        }
        if(result==ESP_OK){
            maintenance.stage="RECOVER_NEW_VOLUME";
            if(!recover_records())result=ESP_FAIL;
        }
        state.initializing=false;
        if(result==ESP_OK){
            state.ready=true;state.medium_initialized=true;state.volume_rebind_required=false;
            state.io_paused=false;state.consecutive_io_errors=state.recovery_failures=0;
            state.write_confirmed=false;state.recovery_awaits_write=true;
            ++maintenance.successes;maintenance.phase="AWAITING_TELEMETRY";maintenance.stage="COMPLETE";
            maintenance.result=0;
        }else{
            state.ready=false;state.io_paused=true;state.recovery_complete=false;
            snprintf(state.error,sizeof(state.error),"maintenance failed at %s; explicit authorization required",maintenance.stage);
        }
    }
    if(result!=ESP_OK){maintenance.phase="FAILED";if(!maintenance.result)maintenance.result=result;}
    maintenance.running=false;
    xSemaphoreGive(state.mutex);return result;
}

#ifndef TB_STORE_HOST_TEST
esp_err_t tb_store_flash_backup(httpd_req_t *request)
{
    const esp_partition_t *p=maintenance_partition();
    if(!p)return ESP_ERR_INVALID_STATE;
    if(!state.mutex || xSemaphoreTake(state.mutex,pdMS_TO_TICKS(200))!=pdTRUE)return ESP_ERR_TIMEOUT;
    unsigned char *chunk=malloc(1024);esp_err_t result=chunk?ESP_OK:ESP_ERR_NO_MEM;
    httpd_resp_set_type(request,"application/octet-stream");
    httpd_resp_set_hdr(request,"Content-Disposition","attachment; filename=truckbox-storage-WL-backup.bin");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    for(size_t offset=0;result==ESP_OK && offset<p->size;offset+=1024){
        size_t length=p->size-offset;if(length>1024)length=1024;
        result=esp_partition_read(p,offset,chunk,length);
        if(result==ESP_OK)result=httpd_resp_send_chunk(request,(const char *)chunk,length);
        if((offset&0x3fff)==0)vTaskDelay(1);
    }
    if(result==ESP_OK)result=httpd_resp_send_chunk(request,NULL,0);
    free(chunk);xSemaphoreGive(state.mutex);return result;
}
#endif

static void maintenance_json(cJSON *storage)
{
    cJSON *o=cJSON_AddObjectToObject(storage,"maintenance");if(!o)return;
    cJSON_AddBoolToObject(o,"available",true);
    cJSON_AddStringToObject(o,"phase",maintenance.running?"FORMATTING":
        maintenance.phase && !strcmp(maintenance.phase,"AWAITING_TELEMETRY") && state.write_confirmed?"COMPLETE":maintenance.phase?maintenance.phase:"IDLE");
    cJSON_AddNumberToObject(o,"attempts",maintenance.attempts);
    cJSON_AddNumberToObject(o,"successes",maintenance.successes);
    cJSON_AddBoolToObject(o,"probe_durable",maintenance.probe_durable);
    cJSON_AddNumberToObject(o,"result",maintenance.result);
    if(maintenance.error)cJSON_AddNumberToObject(o,"errno",maintenance.error);else cJSON_AddNullToObject(o,"errno");
    cJSON_AddStringToObject(o,"stage",maintenance.stage?maintenance.stage:"NONE");
    cJSON_AddBoolToObject(o,"automatic_format",false);
}
