/* Host tests use the actual configuration implementation and actual IDF cJSON.
   Only NVS, identity and entropy hardware APIs are replaced by bounded mocks. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../config.c"

static char *persisted;
static char *pending;
static char *extension_saved,*extension_pending,*auto_saved,*auto_pending;
static char *extra_saved,*extra_pending,*policy_saved,*policy_pending;
static bool target_mac;
static esp_err_t flash_error;
static esp_err_t write_error;
static unsigned writes;
static bool entropy_enabled;
static unsigned entropy_counter;

const char *esp_err_to_name(esp_err_t error)
{
    return error == ESP_OK ? "ESP_OK" : "test-error";
}

void bootloader_random_enable(void)
{
    assert(!entropy_enabled);
    entropy_enabled = true;
}

void bootloader_random_disable(void)
{
    assert(entropy_enabled);
    entropy_enabled = false;
}

void esp_fill_random(void *buffer, size_t size)
{
    assert(entropy_enabled);
    for (size_t i = 0; i < size; ++i) {
        ((uint8_t *)buffer)[i] = (uint8_t)++entropy_counter;
    }
}

esp_err_t esp_read_mac(uint8_t *mac, int type)
{
    assert(type == ESP_MAC_WIFI_STA);
    const uint8_t known_mac[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
    memcpy(mac, known_mac, sizeof(known_mac));
    if(target_mac){const uint8_t actual[]={0xd4,0x05,0x92,0xb9,0x19,0x0c};memcpy(mac,actual,6);}
    return ESP_OK;
}

esp_err_t nvs_flash_init(void)
{
    return flash_error;
}

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "tb_cfg") == 0);
    *handle = 1;
    return mode == NVS_READONLY && persisted == NULL ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *value, size_t *size)
{
    assert(handle == 1);
    if(!strcmp(key,"v223")){
        if(!policy_saved)return ESP_ERR_NVS_NOT_FOUND;
        size_t n=strlen(policy_saved)+1;if(value){assert(*size>=n);memcpy(value,policy_saved,n);}*size=n;return ESP_OK;
    }
    if(!strcmp(key,"v217")){
        if(!extra_saved)return ESP_ERR_NVS_NOT_FOUND;
        size_t n=strlen(extra_saved)+1;if(value){assert(*size>=n);memcpy(value,extra_saved,n);}*size=n;return ESP_OK;
    }
    if(!strcmp(key,"v216")){
        if(!auto_saved)return ESP_ERR_NVS_NOT_FOUND;
        size_t n=strlen(auto_saved)+1;if(value){assert(*size>=n);memcpy(value,auto_saved,n);}*size=n;return ESP_OK;
    }
    if(!strcmp(key,"v212")){
        if(!extension_saved)return ESP_ERR_NVS_NOT_FOUND;
        size_t n=strlen(extension_saved)+1;
        if(value){assert(*size>=n);memcpy(value,extension_saved,n);}*size=n;return ESP_OK;
    }
    assert(!strcmp(key,"config"));
    if (persisted == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    size_t required = strlen(persisted) + 1;
    if (value != NULL) {
        assert(*size >= required);
        memcpy(value, persisted, required);
    }
    *size = required;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    assert(handle == 1);
    if(!strcmp(key,"v223")){if(write_error!=ESP_OK)return write_error;free(policy_pending);policy_pending=strdup(value);assert(policy_pending);return ESP_OK;}
    if(!strcmp(key,"v217")){if(write_error!=ESP_OK)return write_error;free(extra_pending);extra_pending=strdup(value);assert(extra_pending);return ESP_OK;}
    if(!strcmp(key,"v216")){if(write_error!=ESP_OK)return write_error;free(auto_pending);auto_pending=strdup(value);assert(auto_pending);return ESP_OK;}
    if(!strcmp(key,"v212")){if(write_error!=ESP_OK)return write_error;free(extension_pending);extension_pending=strdup(value);assert(extension_pending);return ESP_OK;}
    assert(!strcmp(key,"config"));
    if (write_error != ESP_OK) {
        return write_error;
    }
    free(pending);
    pending = strdup(value);
    assert(pending != NULL);
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1 && pending != NULL);
    if(policy_pending){free(policy_saved);policy_saved=policy_pending;policy_pending=NULL;}
    if(extra_pending){free(extra_saved);extra_saved=extra_pending;extra_pending=NULL;}
    if(auto_pending){free(auto_saved);auto_saved=auto_pending;auto_pending=NULL;}
    if(extension_pending){free(extension_saved);extension_saved=extension_pending;extension_pending=NULL;}
    free(persisted);
    persisted = pending;
    pending = NULL;
    ++writes;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == 1);
}

static void reboot(void)
{
    s_ready = false;
    memset(&s_config, 0, sizeof(s_config));
    memset(&s_saved_config, 0, sizeof(s_saved_config));
}

static void clear_storage(void)
{
    free(persisted);
    free(pending);free(extension_saved);free(extension_pending);extension_saved=extension_pending=NULL;
    free(extra_saved);free(extra_pending);extra_saved=extra_pending=NULL;
    free(policy_saved);free(policy_pending);policy_saved=policy_pending=NULL;
    free(auto_saved);free(auto_pending);auto_saved=auto_pending=NULL;target_mac=false;
    persisted = pending = NULL;
    flash_error = write_error = ESP_OK;
    writes = 0;
    reboot();
}

static esp_err_t apply(const char *text)
{
    cJSON *json = cJSON_Parse(text);
    assert(json != NULL);
    char message[192];
    esp_err_t result = tb_config_apply_json(json, message, sizeof(message));
    if (result != ESP_OK) {
        assert(message[0] != '\0');
    }
    cJSON_Delete(json);
    return result;
}

static void test_first_boot_and_public_data(void)
{
    clear_storage();
    assert(tb_config_init() == ESP_OK);
    assert(!entropy_enabled && writes == 1);
    assert(strcmp(tb_device_uid(), "tb-c6-021122334455") == 0);
    assert(strlen(tb_config()->admin_token) == 32);
    assert(strlen(tb_config()->ap_password) == 16);
    assert(strlen(tb_config()->pair_key) == 64);
    assert(tb_config()->capture_enabled && tb_config()->gnss_enabled);
    assert(!tb_config()->sd_enabled && !tb_config()->modem_enabled);
    assert(!strcmp(tb_config()->core_host, "truckbox.local"));
    assert(tb_auth_admin(tb_config()->admin_token));
    assert(tb_auth_pair(tb_config()->pair_key));
    assert(!tb_auth_admin(NULL) && !tb_auth_admin("wrong"));
    cJSON *public = tb_config_public_json();
    assert(public != NULL);
    const char *private_fields[] = { "admin_token", "ap_password", "pair_key",
        "wifi_password", "core_password", "uplink_token", "gps_token", "apn_password" };
    for (size_t i = 0; i < sizeof(private_fields) / sizeof(private_fields[0]); ++i) {
        assert(cJSON_GetObjectItemCaseSensitive(public, private_fields[i]) == NULL);
    }
    char *encoded = cJSON_PrintUnformatted(public);
    assert(encoded != NULL && strstr(encoded, tb_config()->admin_token) == NULL);
    assert(strstr(encoded, tb_config()->ap_password) == NULL);
    assert(strstr(encoded, tb_config()->pair_key) == NULL);
    free(encoded);
    cJSON_Delete(public);
    const tb_config_t saved = *tb_config();
    reboot();
    assert(tb_config_init() == ESP_OK);
    assert(writes == 1 && memcmp(&saved, tb_config(), sizeof(saved)) == 0);
}

static void test_save_reboot_and_partial_updates(void)
{
    clear_storage();
    assert(tb_config_init() == ESP_OK);
    const tb_config_t before = *tb_config();
    assert(apply("{\"wifi_ssid\":\"Truck WiFi\",\"wifi_password\":\"long-password\","
                 "\"cloud_device_uid\":\"vehicle-a\",\"core_host\":\"192.168.68.105\",\"sequence_floor\":12345}") == ESP_OK);
    assert(memcmp(&before, tb_config(), sizeof(before)) == 0);
    assert(apply("{\"can_baud\":500000,\"gps_device_uid\":\"gps-a\"}") == ESP_OK);
    reboot();
    assert(tb_config_init() == ESP_OK);
    assert(strcmp(tb_config()->wifi_ssid, "Truck WiFi") == 0);
    assert(strcmp(tb_config()->wifi_password, "long-password") == 0);
    assert(strcmp(tb_config()->cloud_device_uid, "vehicle-a") == 0);
    assert(strcmp(tb_config()->core_host, "192.168.68.105") == 0);
    assert(strcmp(tb_config()->gps_device_uid, "gps-a") == 0);
    assert(tb_config()->can_baud == 500000 && tb_config()->sequence_floor == 12345);
    assert(apply("{\"wifi_password\":\"\",\"sd_enabled\":true,\"repaired_power_gates\":true}") == ESP_OK);
    reboot();
    assert(tb_config_init() == ESP_OK);
    assert(tb_config()->wifi_password[0] == '\0');
    assert(tb_config()->sd_enabled && tb_config()->repaired_power_gates);
}

static void test_invalid_inputs_never_persist(void)
{
    clear_storage();
    assert(tb_config_init() == ESP_OK);
    const char *invalid[] = {
        "[]", "{\"unknown\":true}", "{\"capture_enabled\":1}",
        "{\"admin_token\":\"\"}", "{\"admin_token\":\"contains spaces here\"}",
        "{\"ap_password\":\"short\"}", "{\"pair_key\":\"not-hex\"}",
        "{\"wifi_password\":\"short\"}", "{\"wifi_ssid\":\"bad\\nssid\"}",
        "{\"uplink_url\":\"http://example.org/upload\"}",
        "{\"gps_url\":\"https://user:secret@example.org/upload\"}",
        "{\"uplink_url\":\"https://example.org/#fragment\"}",
        "{\"can_baud\":125000}", "{\"can_baud\":250000.5}",
        "{\"can_baud\":250000,\"can_baud\":500000}",
        "{\"gnss_baud\":10000}", "{\"sequence_floor\":-1}",
        "{\"sequence_floor\":1.5}", "{\"sequence_floor\":9007199254740992}",
        "{\"sd_enabled\":true}",
        "{\"core_host\":\"http://192.168.4.1\"}", "{\"core_host\":\"\"}",
        "{\"core_host\":\"truckbox.local:80/path\"}",
    };
    const unsigned initial_writes = writes;
    const char *stored = strdup(persisted);
    assert(stored != NULL);
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(apply(invalid[i]) == ESP_ERR_INVALID_ARG);
        assert(writes == initial_writes && strcmp(stored, persisted) == 0);
    }
    free((void *)stored);
    write_error = ESP_FAIL;
    assert(apply("{\"can_baud\":500000}") == ESP_FAIL);
    assert(tb_config()->can_baud == 250000 && writes == initial_writes);
}

static void test_storage_corruption_and_upgrade(void)
{
    clear_storage();
    persisted = strdup("{\"schema\":77}");
    assert(tb_config_init() == ESP_ERR_NOT_SUPPORTED);
    assert(writes == 0 && strcmp(persisted, "{\"schema\":77}") == 0);
    free(persisted);
    persisted = strdup("{bad-json");
    assert(tb_config_init() == ESP_ERR_INVALID_ARG && writes == 0);
    free(persisted);
    persisted = strdup("{\"schema\":1}trailing");
    assert(tb_config_init() == ESP_ERR_INVALID_ARG && writes == 0);
    free(persisted);
    persisted = strdup("{\"wifi_ssid\":\"legacy\",\"admin_token\":\"abcdef0123456789abcdef0123456789\","
                       "\"ap_password\":\"legacy-AP-pass\",\"pair_key\":\"0123456789abcdef0123456789abcdef\"}");
    assert(tb_config_init() == ESP_OK && writes == 1);
    assert(strcmp(tb_config()->wifi_ssid, "legacy") == 0);
    assert(strcmp(tb_config()->admin_token, "abcdef0123456789abcdef0123456789") == 0);
    assert(strcmp(tb_config()->ap_password, "legacy-AP-pass") == 0);
    assert(strstr(persisted, "\"schema\":1") != NULL);
    reboot();
    flash_error = ESP_FAIL;
    const unsigned previous_writes = writes;
    assert(tb_config_init() == ESP_FAIL && writes == previous_writes);
}

static void test_v212_extension_rollback_compatibility(void)
{
    clear_storage();assert(tb_config_init()==ESP_OK);
    assert(!tb_config()->can_enabled && tb_config()->j1708_enabled);
    assert(tb_config()->j1587_odometer_mid==128 && tb_config()->j1587_fuel250_mid==128);
    assert(tb_config()->j1708_timeout_ms==450);
    for(unsigned i=0;i<17;i++)assert(tb_config()->j1708_intervals_ms[i]==tbx_j1708_policy()[i].period_ms);
    char credentials[33];strcpy(credentials,tb_config()->admin_token);
    assert(apply("{\"j1708_intervals_ms\":{\"26\":120000},\"j1708_timeout_ms\":700,\"j1587_odometer_validated\":true}")==ESP_OK);
    cJSON *legacy=cJSON_Parse(persisted);const cJSON *field;
    cJSON_ArrayForEach(field,legacy){assert(!v212_key(field->string));}
    cJSON_Delete(legacy);
    assert(extension_saved && strstr(extension_saved,"120000"));reboot();assert(tb_config_init()==ESP_OK);
    assert(!strcmp(credentials,tb_config()->admin_token) && tb_config()->j1708_intervals_ms[0]==120000 && tb_config()->j1708_timeout_ms==700);
    assert(tb_config()->j1587_odometer_validated);
    assert(apply("{\"j1708_intervals_ms\":{\"26\":1000}}")==ESP_ERR_INVALID_ARG);
    assert(apply("{\"j1708_intervals_ms\":{\"999\":60000}}")==ESP_ERR_INVALID_ARG);
    assert(apply("{\"j1708_timeout_ms\":99}")==ESP_ERR_INVALID_ARG);
#ifdef CONFIG_TB_J1708_ONLY
    assert(apply("{\"can_enabled\":true}")==ESP_ERR_INVALID_ARG);
#endif
    free(extension_saved);extension_saved=strdup("{bad-extension");reboot();assert(tb_config_init()==ESP_OK);
    assert(!strcmp(credentials,tb_config()->admin_token) && !tb_config()->can_enabled && tb_config()->j1708_enabled);
    assert(tb_config()->j1708_timeout_ms==450); /* Corrupt optional extension cannot block passive collection. */
}

static void test_automatic_persistence(void)
{
    clear_storage();target_mac=true;assert(tb_config_init()==ESP_OK);
#if CONFIG_TB_ROLE_LINK
    assert(!tb_config_j1708_auto_enabled());
#else
    assert(tb_config_j1708_auto_enabled() && tb_config_j1708_auto_persisted());
    assert(tb_config()->j1708_local_mid==172 && tb_config()->j1708_topology_attested);
#endif
    assert(auto_saved && !strstr(persisted,"j1708_auto") && !strstr(extension_saved,"j1708_auto"));
    unsigned count=writes;reboot();assert(tb_config_init()==ESP_OK && writes==count);
    assert(tb_config_set_j1708_auto(false)==ESP_OK && !tb_config_j1708_auto_enabled());
    cJSON *public=tb_config_public_json();assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(public,"j1708_auto_enabled")));cJSON_Delete(public);
    count=writes;reboot();assert(tb_config_init()==ESP_OK && writes==count && !tb_config_j1708_auto_enabled());
    for(unsigned i=0;i<17;i++)assert(tb_config()->j1708_auto_intervals_ms[i]==tbx_j1708_production_period(tbx_j1708_policy()[i].pid,false));
    assert(apply("{\"j1708_auto_intervals_ms\":{\"33\":7000},\"j1708_launch_intervals_ms\":{\"33\":2500}}") == ESP_OK);
    reboot();assert(tb_config_init()==ESP_OK && tb_config()->j1708_auto_intervals_ms[1]==7000 && tb_config()->j1708_launch_intervals_ms[1]==2500);
#if !CONFIG_TB_ROLE_LINK
    /* Real 0.2.22 shape: OFF persisted after a technical fault, no marker.
     * Migrate desired policy once; preserve MIDs, cadences and credentials. */
    assert(!tb_config_j1708_auto_enabled());char credential[33];strcpy(credential,tb_config()->admin_token);
    free(policy_saved);policy_saved=NULL;count=writes;reboot();
    assert(tb_config_init()==ESP_OK && writes==count+1 && tb_config_j1708_auto_enabled());
    assert(!strcmp(credential,tb_config()->admin_token) && tb_config()->j1708_auto_intervals_ms[1]==7000);
    assert(tb_config_set_j1708_auto(false)==ESP_OK);count=writes;reboot();
    assert(tb_config_init()==ESP_OK && writes==count && !tb_config_j1708_auto_enabled());
#endif
    /* Existing 0.2.15 install: absent extension migrates exactly once. */
    free(auto_saved);auto_saved=NULL;count=writes;reboot();assert(tb_config_init()==ESP_OK && writes==count+1);
    count=writes;reboot();assert(tb_config_init()==ESP_OK && writes==count);
    free(auto_saved);auto_saved=NULL;write_error=ESP_FAIL;count=writes;reboot();
    assert(tb_config_init()==ESP_OK && !tb_config_j1708_auto_enabled() && !tb_config_j1708_auto_persisted() && writes==count);
    write_error=ESP_OK;reboot();assert(tb_config_init()==ESP_OK && tb_config_j1708_auto_persisted());
    free(auto_saved);auto_saved=strdup("{\"broken\":true}");count=writes;reboot();
    assert(tb_config_init()==ESP_OK && !tb_config_j1708_auto_enabled() && !tb_config_j1708_auto_persisted() && writes==count);
    clear_storage();
}

static void test_expansion_persistence(void)
{
    clear_storage();target_mac=true;assert(tb_config_init()==ESP_OK);
    assert(extra_saved && !strstr(persisted,"j1708_expansion"));
#if CONFIG_TB_ROLE_LINK
    assert(!tb_config()->j1708_expansion_enabled);
#else
    assert(tb_config()->j1708_expansion_enabled);
#endif
    assert(tb_config()->j1708_stop_dwell_s==15 && tb_config()->j1708_parked_capture_s==300);
    assert(apply("{\"j1708_expansion_enabled\":false,\"j1708_stop_dwell_s\":20,\"j1708_parked_capture_s\":600}")==ESP_OK);
    unsigned count=writes;reboot();assert(tb_config_init()==ESP_OK && writes==count);
    assert(!tb_config()->j1708_expansion_enabled && tb_config()->j1708_stop_dwell_s==20 && tb_config()->j1708_parked_capture_s==600);
    assert(apply("{\"j1708_stop_dwell_s\":1}")==ESP_ERR_INVALID_ARG);
    assert(apply("{\"j1708_parked_capture_s\":59}")==ESP_ERR_INVALID_ARG);
    bool automatic=tb_config_j1708_auto_enabled();
    free(extra_saved);extra_saved=strdup("{bad");reboot();assert(tb_config_init()==ESP_OK && writes==count);
    assert(!tb_config()->j1708_expansion_enabled && tb_config()->j1708_enabled && tb_config_j1708_auto_enabled()==automatic);
    free(extra_saved);extra_saved=NULL;reboot();assert(tb_config_init()==ESP_OK && writes==count+1);
    count=writes;reboot();assert(tb_config_init()==ESP_OK && writes==count);
    clear_storage();
}

int main(void)
{
    test_expansion_persistence();
    test_automatic_persistence();
    test_v212_extension_rollback_compatibility();
    test_first_boot_and_public_data();
    test_save_reboot_and_partial_updates();
    test_invalid_inputs_never_persist();
    test_storage_corruption_and_upgrade();
    clear_storage();
    printf("config host tests passed (%s)\n", tb_role_name());
    return 0;
}
