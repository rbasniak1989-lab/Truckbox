#include "../../../j1708_17pid/src/tbx_j1708_policy.h"
#include "tb.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define CONFIG_SCHEMA 1
#define CONFIG_JSON_MAX 8192
#define CONFIG_NAMESPACE "tb_cfg"
#define CONFIG_KEY "config"

static const char *TAG = "tb_config";
static tb_config_t s_config;
static tb_config_t s_saved_config;
static char s_device_uid[32];
static bool s_ready;

typedef struct {
    const char *name;
    size_t offset;
    size_t capacity;
} string_field_t;

typedef struct {
    const char *name;
    size_t offset;
} bool_field_t;

#define STRING_FIELD(name) { #name, offsetof(tb_config_t, name), sizeof(((tb_config_t *)0)->name) }
#define BOOL_FIELD(name) { #name, offsetof(tb_config_t, name) }

static const string_field_t STRING_FIELDS[] = {
    STRING_FIELD(wifi_ssid), STRING_FIELD(wifi_password),
    STRING_FIELD(admin_token), STRING_FIELD(ap_password), STRING_FIELD(pair_key),
    STRING_FIELD(core_ssid), STRING_FIELD(core_password), STRING_FIELD(core_host),
    STRING_FIELD(uplink_url), STRING_FIELD(uplink_token),
    STRING_FIELD(cloud_device_uid), STRING_FIELD(gps_url),
    STRING_FIELD(gps_device_uid), STRING_FIELD(gps_token),
    STRING_FIELD(apn), STRING_FIELD(apn_user), STRING_FIELD(apn_password),
};

static const bool_field_t BOOL_FIELDS[] = {
    BOOL_FIELD(j1587_odometer_validated), BOOL_FIELD(j1587_fuel250_validated), BOOL_FIELD(capture_enabled), BOOL_FIELD(can_enabled), BOOL_FIELD(j1708_enabled), BOOL_FIELD(full_decode_enabled), BOOL_FIELD(sd_enabled), BOOL_FIELD(fram_enabled),
    BOOL_FIELD(modem_enabled), BOOL_FIELD(gnss_enabled), BOOL_FIELD(repaired_power_gates),
};

static esp_err_t invalid(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0) {
        snprintf(error, capacity, "%s", message);
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t field_invalid(char *error, size_t capacity, const char *name,
                               const char *reason)
{
    if (error != NULL && capacity > 0) {
        snprintf(error, capacity, "%s: %s", name, reason);
    }
    return ESP_ERR_INVALID_ARG;
}

static bool clean_string(const char *value)
{
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; ++p) {
        if (*p < 0x20 || *p == 0x7f) {
            return false;
        }
    }
    return true;
}

static bool hex_string(const char *value)
{
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; ++p) {
        if (!isxdigit(*p)) {
            return false;
        }
    }
    return true;
}

static bool token_string(const char *value)
{
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; ++p) {
        if (!(isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~')) {
            return false;
        }
    }
    return true;
}

static bool wifi_password_valid(const char *password)
{
    const size_t length = strlen(password);
    if (length == 0) {
        return true; /* Deliberately configured open network. */
    }
    return (length >= 8 && length <= 63) || (length == 64 && hex_string(password));
}

static bool https_url_valid(const char *url)
{
    if (*url == '\0') {
        return true; /* Unconfigured cellular upload is a valid initial state. */
    }
    if (strncmp(url, "https://", 8) != 0) {
        return false;
    }
    const char *authority = url + 8;
    const char *end = authority + strcspn(authority, "/?#");
    if (end == authority || memchr(authority, '@', (size_t)(end - authority)) != NULL ||
        strchr(url, '#') != NULL) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)url; *p != '\0'; ++p) {
        if (isspace(*p)) {
            return false;
        }
    }
    return true;
}

static void random_hex(char *output, size_t digits)
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < digits; i += 2) {
        uint8_t random_byte;
        esp_fill_random(&random_byte, sizeof(random_byte));
        output[i] = HEX[random_byte >> 4];
        output[i + 1] = HEX[random_byte & 15];
    }
    output[digits] = '\0';
}

static void defaults(tb_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->j1708_timeout_ms=450;
    for(unsigned i=0;i<17;i++)config->j1708_intervals_ms[i]=tbx_j1708_policy()[i].period_ms;
    config->can_baud = 250000;
    config->gnss_baud = 9600;
    config->capture_enabled = true;
    config->can_enabled=false;config->j1708_enabled=true;
    config->j1587_odometer_mid=128;config->j1587_fuel250_mid=128;
    config->full_decode_enabled = false; /* Rev B1: prioritize capture and avoid RAW flash backlog */
    config->gnss_enabled = true;
    snprintf(config->core_host, sizeof(config->core_host), "truckbox.local");
    snprintf(config->uplink_url, sizeof(config->uplink_url),
             "https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-ingest");
    snprintf(config->gps_url, sizeof(config->gps_url),
             "https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-gps");
    /* Config initialization precedes Wi-Fi/BT/ADC initialization. Enable the
       documented early-boot entropy source while creating access credentials. */
    bootloader_random_enable();
    random_hex(config->admin_token, 32);
    random_hex(config->ap_password, 16);
    random_hex(config->pair_key, 64);
    bootloader_random_disable();
}

static bool known_key(const char *name, bool loading)
{
    if (name == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(STRING_FIELDS) / sizeof(STRING_FIELDS[0]); ++i) {
        if (strcmp(name, STRING_FIELDS[i].name) == 0) {
            return true;
        }
    }
    for (size_t i = 0; i < sizeof(BOOL_FIELDS) / sizeof(BOOL_FIELDS[0]); ++i) {
        if (strcmp(name, BOOL_FIELDS[i].name) == 0) {
            return true;
        }
    }
    return strcmp(name,"j1587_odometer_mid")==0 || strcmp(name,"j1587_fuel250_mid")==0 || strcmp(name,"j1708_intervals_ms")==0 || strcmp(name,"j1708_timeout_ms")==0 || strcmp(name, "can_baud") == 0 || strcmp(name, "gnss_baud") == 0 ||
           strcmp(name, "sequence_floor") == 0 ||
           (loading && strcmp(name, "schema") == 0);
}

static esp_err_t validate_config(const tb_config_t *config, char *error, size_t capacity)
{
    if (!wifi_password_valid(config->wifi_password)) {
        return invalid(error, capacity, "wifi_password: use vazia, 8 a 63 caracteres ou PSK hexadecimal de 64 caracteres");
    }
    if (!wifi_password_valid(config->core_password)) {
        return invalid(error, capacity, "core_password: use 8 a 63 caracteres ou PSK hexadecimal de 64 caracteres");
    }
    const size_t admin_length = strlen(config->admin_token);
    if (admin_length < 16 || admin_length > 32 || !token_string(config->admin_token)) {
        return invalid(error, capacity, "admin_token: use 16 a 32 letras, numeros ou -_.~");
    }
    const size_t ap_length = strlen(config->ap_password);
    if (ap_length < 8 || ap_length > 16) {
        return invalid(error, capacity, "ap_password: use 8 a 16 caracteres");
    }
    const size_t pair_length = strlen(config->pair_key);
    if (pair_length < 32 || pair_length > 64 || !hex_string(config->pair_key)) {
        return invalid(error, capacity, "pair_key: use 32 a 64 caracteres hexadecimais");
    }
    if (!https_url_valid(config->uplink_url)) {
        return invalid(error, capacity, "uplink_url: use HTTPS sem credenciais na URL, fragmento ou espacos");
    }
    if (!https_url_valid(config->gps_url)) {
        return invalid(error, capacity, "gps_url: use HTTPS sem credenciais na URL, fragmento ou espacos");
    }
#ifdef CONFIG_TB_J1708_ONLY
    if(config->can_enabled)return invalid(error,capacity,"J1708_ONLY: CAN requires a dual-network firmware build");
#endif
    if (config->can_baud != 250000 && config->can_baud != 500000) {
        return invalid(error, capacity, "can_baud: use 250000 ou 500000");
    }
    if (config->gnss_baud != 9600 && config->gnss_baud != 19200 &&
        config->gnss_baud != 38400 && config->gnss_baud != 57600 &&
        config->gnss_baud != 115200) {
        return invalid(error, capacity, "gnss_baud: use 9600, 19200, 38400, 57600 ou 115200");
    }
    if (config->sd_enabled && !config->repaired_power_gates) {
        return invalid(error, capacity, "sd_enabled: confirme primeiro o reparo dos controles de alimentacao");
    }
    if (!config->core_host[0]) return invalid(error, capacity, "core_host: informe o hostname ou IPv4 do Core");
    for (const unsigned char *p = (const unsigned char *)config->core_host; *p; ++p) {
        if (!isalnum(*p) && *p != '.' && *p != '-')
            return invalid(error, capacity, "core_host: use somente hostname ou IPv4, sem http, porta ou caminho");
    }
    if (tb_role() == TB_ROLE_LINK && *config->core_ssid != '\0' &&
        *config->core_password == '\0') {
        return invalid(error, capacity, "core_password: o enlace interno requer a senha WPA2 do Core");
    }
    return ESP_OK;
}

static esp_err_t apply_fields(const cJSON *json, tb_config_t *config, bool loading,
                              char *error, size_t capacity)
{
    if (!cJSON_IsObject(json)) {
        return invalid(error, capacity, "Configuracao deve ser um objeto JSON");
    }
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, json) {
        if (!known_key(item->string, loading)) {
            return invalid(error, capacity, "Campo de configuracao desconhecido");
        }
        /* Duplicate JSON keys have ambiguous semantics and are never accepted. */
        for (const cJSON *other = item->next; other != NULL; other = other->next) {
            if (other->string != NULL && strcmp(item->string, other->string) == 0) {
                return invalid(error, capacity, "Campo de configuracao duplicado");
            }
        }
    }
    for (size_t i = 0; i < sizeof(STRING_FIELDS) / sizeof(STRING_FIELDS[0]); ++i) {
        const string_field_t *field = &STRING_FIELDS[i];
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(json, field->name);
        if (value == NULL) {
            continue;
        }
        if (!cJSON_IsString(value) || value->valuestring == NULL) {
            return field_invalid(error, capacity, field->name, "deve ser texto");
        }
        const size_t length = strlen(value->valuestring);
        if (length >= field->capacity || !clean_string(value->valuestring)) {
            return field_invalid(error, capacity, field->name, "tamanho invalido ou caracteres de controle");
        }
        char *destination = (char *)config + field->offset;
        memcpy(destination, value->valuestring, length + 1);
    }
    for (size_t i = 0; i < sizeof(BOOL_FIELDS) / sizeof(BOOL_FIELDS[0]); ++i) {
        const bool_field_t *field = &BOOL_FIELDS[i];
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(json, field->name);
        if (value == NULL) {
            continue;
        }
        if (!cJSON_IsBool(value)) {
            return field_invalid(error, capacity, field->name, "deve ser booleano");
        }
        *(bool *)((char *)config + field->offset) = cJSON_IsTrue(value);
    }
    const char *number_names[] = { "can_baud", "gnss_baud" };
    uint32_t *numbers[] = { &config->can_baud, &config->gnss_baud };
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(json, number_names[i]);
        if (value == NULL) {
            continue;
        }
        if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
            value->valuedouble < 0 || value->valuedouble > UINT32_MAX ||
            value->valuedouble != (double)(uint32_t)value->valuedouble) {
            return field_invalid(error, capacity, number_names[i], "deve ser inteiro positivo");
        }
        *numbers[i] = (uint32_t)value->valuedouble;
    }
    const char *mid_names[]={"j1587_odometer_mid","j1587_fuel250_mid"};
    uint8_t *mids[]={&config->j1587_odometer_mid,&config->j1587_fuel250_mid};
    for(unsigned i=0;i<2;i++){
        const cJSON *mid=cJSON_GetObjectItemCaseSensitive(json,mid_names[i]);if(!mid)continue;
        if(!cJSON_IsNumber(mid) || mid->valuedouble<128 || mid->valuedouble>255 || floor(mid->valuedouble)!=mid->valuedouble)return invalid(error,capacity,"MID do contador: inteiro128..255");
        *mids[i]=(uint8_t)mid->valueint;
    }
    const cJSON *timeout=cJSON_GetObjectItemCaseSensitive(json,"j1708_timeout_ms");
    if(timeout){
        if(!cJSON_IsNumber(timeout) || timeout->valuedouble<100 || timeout->valuedouble>5000 || floor(timeout->valuedouble)!=timeout->valuedouble)
            return invalid(error,capacity,"j1708_timeout_ms: inteiro100..5000");
        config->j1708_timeout_ms=(uint32_t)timeout->valueint;
    }
    const cJSON *periods=cJSON_GetObjectItemCaseSensitive(json,"j1708_intervals_ms");
    if(periods){
        if(!cJSON_IsObject(periods))return invalid(error,capacity,"j1708_intervals_ms: objeto PID:milissegundos");
        const cJSON *entry;
        cJSON_ArrayForEach(entry,periods){
            for(const cJSON *other=entry->next;other;other=other->next)if(!strcmp(entry->string,other->string))return invalid(error,capacity,"PID duplicado no intervalo J1708");
            bool found=false;
            for(unsigned i=0;i<17;i++){
                char key[8];snprintf(key,sizeof(key),"%u",tbx_j1708_policy()[i].pid);
                if(!strcmp(key,entry->string)){
                    if(!cJSON_IsNumber(entry) || entry->valuedouble<60000 || entry->valuedouble>3600000 || floor(entry->valuedouble)!=entry->valuedouble)
                        return invalid(error,capacity,"intervalo J1708: inteiro60000..3600000");
                    config->j1708_intervals_ms[i]=(uint32_t)entry->valueint;found=true;break;
                }
            }
            if(!found)return invalid(error,capacity,"PID J1708 desconhecido no intervalo");
        }
    }
    const cJSON *floor = cJSON_GetObjectItemCaseSensitive(json, "sequence_floor");
    if (floor != NULL) {
        if (!cJSON_IsNumber(floor) || !isfinite(floor->valuedouble) ||
            floor->valuedouble < 0 || floor->valuedouble > 9007199254740991.0 ||
            floor->valuedouble != (double)(uint64_t)floor->valuedouble) {
            return invalid(error, capacity, "sequence_floor: use inteiro de 0 a 9007199254740991");
        }
        config->sequence_floor = (uint64_t)floor->valuedouble;
    }
    return validate_config(config, error, capacity);
}

static void add_j1708_periods(cJSON *json,const tb_config_t *config)
{
    cJSON_AddNumberToObject(json,"j1708_timeout_ms",config->j1708_timeout_ms);
    cJSON_AddNumberToObject(json,"j1587_odometer_mid",config->j1587_odometer_mid);
    cJSON_AddNumberToObject(json,"j1587_fuel250_mid",config->j1587_fuel250_mid);
    cJSON *periods=cJSON_AddObjectToObject(json,"j1708_intervals_ms");
    for(unsigned i=0;i<17;i++){
        char key[8];snprintf(key,sizeof(key),"%u",tbx_j1708_policy()[i].pid);
        cJSON_AddNumberToObject(periods,key,config->j1708_intervals_ms[i]);
    }
}

static bool v212_key(const char *name)
{
    return !strcmp(name,"can_enabled") || !strcmp(name,"j1708_enabled") ||
        !strcmp(name,"j1587_odometer_validated") || !strcmp(name,"j1587_fuel250_validated") ||
        !strcmp(name,"j1587_odometer_mid") || !strcmp(name,"j1587_fuel250_mid") ||
        !strcmp(name,"j1708_timeout_ms") || !strcmp(name,"j1708_intervals_ms");
}
static cJSON *extension_json(const tb_config_t *config)
{
    cJSON *o=cJSON_CreateObject();if(!o)return NULL;
    for(size_t i=0;i<sizeof(BOOL_FIELDS)/sizeof(BOOL_FIELDS[0]);i++){
        const bool_field_t *f=&BOOL_FIELDS[i];
        if(v212_key(f->name) && !cJSON_AddBoolToObject(o,f->name,*(const bool *)((const char *)config+f->offset))){cJSON_Delete(o);return NULL;}
    }
    add_j1708_periods(o,config);return o;
}
/* Extensions use another key, so the strict0.2.11 configuration loader can
 * still boot with the original config key during automatic/manual rollback. */
static char extension_error[96];
static void load_extension(void)
{
    extension_error[0]=0;nvs_handle_t h;esp_err_t err=nvs_open(CONFIG_NAMESPACE,NVS_READONLY,&h);
    if(err!=ESP_OK)return;
    size_t n=0;err=nvs_get_str(h,"v212",NULL,&n);
    if(err==ESP_ERR_NVS_NOT_FOUND){nvs_close(h);return;}
    char *text=err==ESP_OK && n>=2 && n<=4096?malloc(n):NULL;
    cJSON *o=NULL;
    if(text && nvs_get_str(h,"v212",text,&n)==ESP_OK)o=cJSON_ParseWithLengthOpts(text,n,NULL,true);
    free(text);nvs_close(h);
    bool keys=cJSON_IsObject(o);const cJSON *entry;
    cJSON_ArrayForEach(entry,o)if(!entry->string || !v212_key(entry->string))keys=false;
    tb_config_t candidate=s_config;char error[96]={0};
    if(!keys || apply_fields(o,&candidate,true,error,sizeof(error))!=ESP_OK){
        snprintf(extension_error,sizeof(extension_error),"v212 extension invalid; safe defaults retained");
        ESP_LOGW(TAG,"%s",extension_error);
    }else s_config=candidate;
    cJSON_Delete(o);
}

static cJSON *private_json(const tb_config_t *config)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }
    if (cJSON_AddNumberToObject(json, "schema", CONFIG_SCHEMA) == NULL) {
        cJSON_Delete(json);
        return NULL;
    }
    for (size_t i = 0; i < sizeof(STRING_FIELDS) / sizeof(STRING_FIELDS[0]); ++i) {
        const string_field_t *field = &STRING_FIELDS[i];
        if (cJSON_AddStringToObject(json, field->name,
                                   (const char *)config + field->offset) == NULL) {
            cJSON_Delete(json);
            return NULL;
        }
    }
    for (size_t i = 0; i < sizeof(BOOL_FIELDS) / sizeof(BOOL_FIELDS[0]); ++i) {
        const bool_field_t *field = &BOOL_FIELDS[i];
        if(v212_key(field->name))continue;
        if (cJSON_AddBoolToObject(json, field->name,
                                 *(const bool *)((const char *)config + field->offset)) == NULL) {
            cJSON_Delete(json);
            return NULL;
        }
    }
    if (cJSON_AddNumberToObject(json, "can_baud", config->can_baud) == NULL ||
        cJSON_AddNumberToObject(json, "gnss_baud", config->gnss_baud) == NULL ||
        cJSON_AddNumberToObject(json, "sequence_floor", (double)config->sequence_floor) == NULL) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

static esp_err_t persist(const tb_config_t *config)
{
    cJSON *json = private_json(config);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    char *serialized = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (serialized == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON *extension=extension_json(config);
    char *ext=extension?cJSON_PrintUnformatted(extension):NULL;cJSON_Delete(extension);
    if(!ext){volatile char *clear=serialized;size_t size=strlen(serialized);for(size_t i=0;i<size;i++)clear[i]=0;free(serialized);return ESP_ERR_NO_MEM;}
    nvs_handle_t handle;
    esp_err_t result = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_str(handle, CONFIG_KEY, serialized);
        if(result==ESP_OK)result=nvs_set_str(handle,"v212",ext);
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    /* This buffer contains credentials and should not linger in a freed heap block. */
    volatile char *clear = serialized;
    const size_t length = strlen(serialized);
    for (size_t i = 0; i < length; ++i) {
        clear[i] = 0;
    }
    free(serialized);free(ext);
    return result;
}

tb_role_t tb_role(void)
{
#if CONFIG_TB_ROLE_LINK
    return TB_ROLE_LINK;
#else
    return TB_ROLE_CORE;
#endif
}

const char *tb_role_name(void)
{
    return tb_role() == TB_ROLE_LINK ? "link" : "core";
}

const char *tb_device_uid(void)
{
    return s_device_uid;
}

const tb_config_t *tb_config(void)
{
    return &s_config;
}

esp_err_t tb_config_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "NVS indisponivel (%s); dados existentes nao foram apagados", esp_err_to_name(result));
        return result;
    }
    uint8_t mac[6];
    result = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (result != ESP_OK) {
        return result;
    }
    snprintf(s_device_uid, sizeof(s_device_uid), "tb-c6-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    defaults(&s_config);

    nvs_handle_t handle;
    result = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle);
    bool first_boot = result == ESP_ERR_NVS_NOT_FOUND;
    bool upgrade = false;
    bool created_access_credentials = first_boot;
    if (result != ESP_OK && !first_boot) {
        return result;
    }
    if (!first_boot) {
        size_t size = 0;
        result = nvs_get_str(handle, CONFIG_KEY, NULL, &size);
        if (result == ESP_ERR_NVS_NOT_FOUND) {
            first_boot = true;
        } else if (result == ESP_OK) {
            if (size < 3 || size > CONFIG_JSON_MAX) {
                nvs_close(handle);
                return ESP_ERR_INVALID_SIZE;
            }
            char *serialized = malloc(size);
            if (serialized == NULL) {
                nvs_close(handle);
                return ESP_ERR_NO_MEM;
            }
            result = nvs_get_str(handle, CONFIG_KEY, serialized, &size);
            cJSON *json = result == ESP_OK ? cJSON_ParseWithLengthOpts(serialized, size, NULL, true) : NULL;
            free(serialized);
            if (result == ESP_OK && !cJSON_IsObject(json)) {
                result = ESP_ERR_INVALID_ARG;
            }
            if (result == ESP_OK) {
                const cJSON *schema = cJSON_GetObjectItemCaseSensitive(json, "schema");
                if (schema != NULL && (!cJSON_IsNumber(schema) || schema->valuedouble < 0 ||
                                       schema->valuedouble > CONFIG_SCHEMA ||
                                       schema->valuedouble != (double)(unsigned)schema->valuedouble)) {
                    result = ESP_ERR_NOT_SUPPORTED;
                } else {
                    upgrade = schema == NULL || schema->valuedouble < CONFIG_SCHEMA;
                    created_access_credentials =
                        cJSON_GetObjectItemCaseSensitive(json, "admin_token") == NULL ||
                        cJSON_GetObjectItemCaseSensitive(json, "ap_password") == NULL;
                    char error[128];
                    result = apply_fields(json, &s_config, true, error, sizeof(error));
                    if (result != ESP_OK) {
                        ESP_LOGE(TAG, "Configuracao persistida invalida: %s; sem apagar NVS", error);
                    }
                    for (size_t i = 0; i < sizeof(STRING_FIELDS) / sizeof(STRING_FIELDS[0]); ++i) {
                        upgrade |= cJSON_GetObjectItemCaseSensitive(json, STRING_FIELDS[i].name) == NULL;
                    }
                    for (size_t i = 0; i < sizeof(BOOL_FIELDS) / sizeof(BOOL_FIELDS[0]); ++i) {
                        /* Missing opt-in flag means LEGACY; no extra NVS write
                         * solely to introduce a field on an existing board. */
                        if (!v212_key(BOOL_FIELDS[i].name) && strcmp(BOOL_FIELDS[i].name, "full_decode_enabled") != 0)
                            upgrade |= cJSON_GetObjectItemCaseSensitive(json, BOOL_FIELDS[i].name) == NULL;
                    }
                    upgrade |= cJSON_GetObjectItemCaseSensitive(json, "can_baud") == NULL;
                    upgrade |= cJSON_GetObjectItemCaseSensitive(json, "gnss_baud") == NULL;
                    upgrade |= cJSON_GetObjectItemCaseSensitive(json, "sequence_floor") == NULL;
                }
            }
            cJSON_Delete(json);
        }
        nvs_close(handle);
        if (result != ESP_OK && !first_boot) {
            return result;
        }
    }

    load_extension();
    if (first_boot || upgrade) {
        result = persist(&s_config);
        if (result != ESP_OK) {
            return result;
        }
    }
    s_saved_config = s_config;
    s_ready = true;
    if (first_boot || created_access_credentials) {
        ESP_LOGW(TAG, "Credenciais criadas para %s. Guarde os valores abaixo em local privado.", s_device_uid);
        ESP_LOGW(TAG, "Senha da rede de configuracao: %s", s_config.ap_password);
        ESP_LOGW(TAG, "Senha/token de administracao: %s", s_config.admin_token);
        ESP_LOGW(TAG, "As credenciais acima sao exibidas somente na primeira inicializacao.");
    }
    return ESP_OK;
}

esp_err_t tb_config_apply_json(const cJSON *json, char *error, size_t error_size)
{
    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    if (!s_ready) {
        return invalid(error, error_size, "Configuracao ainda nao inicializada");
    }
    tb_config_t candidate = s_saved_config;
    esp_err_t result = apply_fields(json, &candidate, false, error, error_size);
    if (result == ESP_OK) {
        result = persist(&candidate);
        if (result == ESP_OK) {
            s_saved_config = candidate;
        }
        if (result != ESP_OK && error != NULL && error_size > 0) {
            snprintf(error, error_size, "Falha ao salvar configuracao: %s", esp_err_to_name(result));
        }
    }
    /* Runtime readers retain the boot-time snapshot until the scheduled restart. */
    memset(&candidate, 0, sizeof(candidate));
    return result;
}

cJSON *tb_config_public_json(void)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(json, "wifi_ssid", s_config.wifi_ssid);
    cJSON_AddStringToObject(json, "core_ssid", s_config.core_ssid);
    cJSON_AddStringToObject(json, "core_host", s_config.core_host);
    cJSON_AddStringToObject(json, "uplink_url", s_config.uplink_url);
    cJSON_AddStringToObject(json, "cloud_device_uid", s_config.cloud_device_uid);
    cJSON_AddStringToObject(json, "gps_url", s_config.gps_url);
    cJSON_AddStringToObject(json, "gps_device_uid", s_config.gps_device_uid);
    cJSON_AddStringToObject(json, "apn", s_config.apn);
    cJSON_AddStringToObject(json, "apn_user", s_config.apn_user);
    cJSON_AddNumberToObject(json, "can_baud", s_config.can_baud);
    cJSON_AddNumberToObject(json, "gnss_baud", s_config.gnss_baud);
    add_j1708_periods(json,&s_config);
    cJSON_AddStringToObject(json,"capture_profile",s_config.can_enabled?"DUAL":"J1708_ONLY");
    cJSON_AddNumberToObject(json, "sequence_floor", (double)s_config.sequence_floor);
    cJSON_AddBoolToObject(json, "wifi_password_configured", s_config.wifi_password[0] != '\0');
    cJSON_AddBoolToObject(json, "core_password_configured", s_config.core_password[0] != '\0');
    cJSON_AddBoolToObject(json, "uplink_token_configured", s_config.uplink_token[0] != '\0');
    cJSON_AddBoolToObject(json, "gps_token_configured", s_config.gps_token[0] != '\0');
    cJSON_AddBoolToObject(json, "apn_password_configured", s_config.apn_password[0] != '\0');
    cJSON_AddBoolToObject(json, "admin_token_configured", s_config.admin_token[0] != '\0');
    cJSON_AddBoolToObject(json, "pair_key_configured", s_config.pair_key[0] != '\0');
    for (size_t i = 0; i < sizeof(BOOL_FIELDS) / sizeof(BOOL_FIELDS[0]); ++i) {
        const bool_field_t *field = &BOOL_FIELDS[i];
        cJSON_AddBoolToObject(json, field->name,
                             *(const bool *)((const char *)&s_config + field->offset));
    }
    if(extension_error[0])cJSON_AddStringToObject(json,"j1708_config_error",extension_error);
    return json;
}

static bool equal_token(const char *candidate, const char *expected)
{
    if (!s_ready || candidate == NULL) {
        return false;
    }
    const size_t expected_length = strlen(expected);
    if (expected_length == 0 || strlen(candidate) != expected_length) {
        return false;
    }
    volatile unsigned char difference = 0;
    for (size_t i = 0; i < expected_length; ++i) {
        difference |= (unsigned char)candidate[i] ^ (unsigned char)expected[i];
    }
    return difference == 0;
}

bool tb_auth_admin(const char *token)
{
    return equal_token(token, s_config.admin_token);
}

bool tb_auth_pair(const char *token)
{
    return equal_token(token, s_config.pair_key);
}
