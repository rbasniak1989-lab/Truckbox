#include "tb.h"
#include "tb_image_policy.h"

#include <limits.h>
#include <inttypes.h>
#include "esp_heap_caps.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"

#define TB_MAX_CONFIG_BYTES 4096U
#define TB_MAX_OTA_BYTES (3U * 1024U * 1024U)
#define TB_OTA_CHUNK_SIZE 4096U
#define TB_UPLOAD_DEADLINE_US (240LL * 1000000LL)
#define TB_MAX_LINK_RESPONSE 2048U

static const char *TAG = "tb_web";
static httpd_handle_t s_server;
static atomic_bool s_ota_busy = ATOMIC_VAR_INIT(false);

extern const unsigned char index_html_start[] asm("_binary_index_html_start");
extern const unsigned char index_html_end[] asm("_binary_index_html_end");

void tb_web_set_ota_busy(bool busy)
{
    atomic_store(&s_ota_busy, busy);
}

bool tb_web_ota_busy(void)
{
    return atomic_load(&s_ota_busy);
}

static bool begin_upload(void)
{
    bool expected = false;
    return atomic_compare_exchange_strong(&s_ota_busy, &expected, true);
}

static void response_headers(httpd_req_t *request)
{
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
}

static esp_err_t health_reply(httpd_req_t *request,bool low_memory)
{
    /* No cJSON allocation: diagnostics remain possible after an allocation
     * failure, and report largest block as well as total/minimum free RAM. */
    char body[512];
    int n=snprintf(body,sizeof(body),"{\"ok\":%s,\"diagnostic_partial\":true,\"error\":\"%s\",\"memory_kind\":\"RAM\",\"role\":\"%s\",\"device_uid\":\"%s\",\"version\":\"%s\",\"uptime_ms\":%" PRIu64 ",\"heap_free\":%u,\"heap_minimum\":%u,\"heap_largest_block\":%u}",
        low_memory?"false":"true",low_memory?"Sem memoria RAM para diagnostico completo":"",tb_role_name(),tb_device_uid(),tb_version(),
        (uint64_t)esp_timer_get_time()/1000u,(unsigned)esp_get_free_heap_size(),(unsigned)esp_get_minimum_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if(n<=0 || (size_t)n>=sizeof(body))return ESP_FAIL;
    if(low_memory)httpd_resp_set_status(request,"503 Service Unavailable");
    response_headers(request);httpd_resp_set_type(request,"application/json; charset=utf-8");
    return httpd_resp_send(request,body,n);
}
static esp_err_t health_handler(httpd_req_t *request){return health_reply(request,false);}

static esp_err_t send_error(httpd_req_t *request, const char *status, const char *message)
{
    httpd_resp_set_status(request, status);
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        response_headers(request);
        httpd_resp_set_type(request, "application/json; charset=utf-8");
        return health_reply(request,true);
    }
    cJSON_AddBoolToObject(body, "ok", false);
    cJSON_AddStringToObject(body, "error", message);
    return tb_http_send_json(request, body);
}

static esp_err_t close_with_error(httpd_req_t *request, const char *status, const char *message)
{
    send_error(request, status, message);
    return ESP_FAIL;
}

typedef struct{httpd_req_t *request;char bytes[1024];size_t used;esp_err_t error;}json_http_t;
static bool json_http_sink(const char *bytes,size_t n,void *context)
{
    json_http_t *w=context;
    while(n){
        size_t room=sizeof(w->bytes)-w->used,take=n<room?n:room;
        memcpy(w->bytes+w->used,bytes,take);w->used+=take;bytes+=take;n-=take;
        if(w->used==sizeof(w->bytes)){
            w->error=httpd_resp_send_chunk(w->request,w->bytes,w->used);w->used=0;
            if(w->error!=ESP_OK)return false;
        }
    }
    return true;
}
esp_err_t tb_http_send_json(httpd_req_t *request,cJSON *owned_json)
{
    if(!owned_json || tb_json_measure(owned_json,SIZE_MAX)==SIZE_MAX){cJSON_Delete(owned_json);return health_reply(request,true);}
    response_headers(request);httpd_resp_set_type(request,"application/json; charset=utf-8");
    json_http_t writer={.request=request,.error=ESP_OK};
    if(!tb_json_write_owned(owned_json,json_http_sink,&writer))return writer.error==ESP_OK?ESP_FAIL:writer.error;
    if(writer.used){writer.error=httpd_resp_send_chunk(request,writer.bytes,writer.used);if(writer.error!=ESP_OK)return writer.error;}
    return httpd_resp_send_chunk(request,NULL,0);
}

static bool request_authenticated(httpd_req_t *request, bool admin, bool pair)
{
    char authorization[96];
    size_t length = httpd_req_get_hdr_value_len(request, "Authorization");
    if (!length || length >= sizeof(authorization) ||
        httpd_req_get_hdr_value_str(request, "Authorization", authorization, sizeof(authorization)) != ESP_OK ||
        strncasecmp(authorization, "Bearer ", 7)) {
        return false;
    }
    const char *token = authorization + 7;
    return (admin && tb_auth_admin(token)) || (pair && tb_auth_pair(token));
}

static bool require_auth(httpd_req_t *request, bool admin, bool pair)
{
    if (request_authenticated(request, admin, pair)) {
        return true;
    }
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Bearer realm=\"Truckbox\"");
    send_error(request, "401 Unauthorized", "Informe a senha de administração ou a chave de vínculo correta.");
    return false;
}

bool tb_http_require_admin(httpd_req_t *request)
{
    return require_auth(request, true, false);
}

bool tb_http_require_pair(httpd_req_t *request)
{
    return require_auth(request, false, true);
}

/* A timeout or disconnect must fail the operation, never be treated as EOF. */
static int receive_body(httpd_req_t *request, char *buffer, size_t capacity, int64_t deadline)
{
    unsigned timeouts = 0;
    while (esp_timer_get_time() < deadline) {
        int count = httpd_req_recv(request, buffer, capacity);
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) {
            continue;
        }
        return count;
    }
    return HTTPD_SOCK_ERR_TIMEOUT;
}

/* Bound cJSON's recursive parser stack even if a peer sends deeply nested JSON.
 * Quoted/escaped braces are data, not structural nesting. cJSON does full syntax validation. */
static bool bounded_json_depth(const char *json, size_t length)
{
    unsigned depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (size_t i = 0; i < length; ++i) {
        char value = json[i];
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (value == '\\') {
                escaped = true;
            } else if (value == '"') {
                quoted = false;
            }
            continue;
        }
        if (value == '"') {
            quoted = true;
        } else if (value == '{' || value == '[') {
            if (++depth > 24) {
                return false;
            }
        } else if (value == '}' || value == ']') {
            if (depth == 0) {
                return false;
            }
            --depth;
        }
    }
    return !quoted && depth == 0;
}

esp_err_t tb_http_read_json(httpd_req_t *request, size_t max_size, cJSON **json)
{
    *json = NULL;
    if (!request->content_len || request->content_len > max_size) {
        send_error(request, "413 Payload Too Large", "Corpo JSON vazio ou maior que o limite permitido.");
        return ESP_ERR_INVALID_SIZE;
    }
    char *buffer = malloc(request->content_len + 1);
    if (!buffer) {
        send_error(request, "500 Internal Server Error", "Sem memória disponível.");
        return ESP_ERR_NO_MEM;
    }
    size_t used = 0;
    int64_t deadline = esp_timer_get_time() + 30LL * 1000000LL;
    while (used < request->content_len) {
        int count = receive_body(request, buffer + used, request->content_len - used, deadline);
        if (count <= 0) {
            free(buffer);
            send_error(request, "408 Request Timeout", "A transmissão foi interrompida. Tente novamente.");
            return ESP_ERR_TIMEOUT;
        }
        used += (size_t)count;
    }
    buffer[used] = '\0';
    if (memchr(buffer, '\0', used) || !bounded_json_depth(buffer, used)) {
        free(buffer);
        send_error(request, "400 Bad Request", "JSON inválido ou com estruturas aninhadas demais.");
        return ESP_ERR_INVALID_ARG;
    }
    const char *end = NULL;
    cJSON *parsed = cJSON_ParseWithLengthOpts(buffer, used + 1, &end, true);
    free(buffer);
    if (!parsed || !cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        send_error(request, "400 Bad Request", "JSON inválido; envie um objeto JSON.");
        return ESP_ERR_INVALID_ARG;
    }
    *json = parsed;
    return ESP_OK;
}

static esp_err_t home_handler(httpd_req_t *request)
{
    response_headers(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
                       "default-src 'none'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; "
                       "connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'self'; frame-ancestors 'none'");
    return httpd_resp_send(request, (const char *)index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t state_handler(httpd_req_t *request)
{
    cJSON *body = cJSON_CreateObject();
    if (!body) return health_reply(request,true);
    cJSON_AddBoolToObject(body, "ok", true);
    tb_base_add_json(body);
    tb_network_add_json(body);
    char query[48],compact[4];
    bool small=httpd_req_get_url_query_str(request,query,sizeof(query))==ESP_OK && httpd_query_key_value(query,"compact",compact,sizeof(compact))==ESP_OK && !strcmp(compact,"1");
    if(small)tb_application_add_json_compact(body);else tb_application_add_json(body);
    cJSON_AddBoolToObject(body,"diagnostic_compact",small);
    cJSON_AddNumberToObject(body,"heap_largest_block",(double)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if(!cJSON_AddBoolToObject(body,"ota_busy",tb_web_ota_busy())){
        cJSON_Delete(body);return health_reply(request,true);
    }
    return tb_http_send_json(request, body);
}

static esp_err_t config_get_handler(httpd_req_t *request)
{
    if (!tb_http_require_admin(request)) {
        return ESP_OK;
    }
    return tb_http_send_json(request, tb_config_public_json());
}

static esp_err_t config_post_handler(httpd_req_t *request)
{
    if (!tb_http_require_admin(request)) {
        return ESP_FAIL; /* close connection: do not leave an unread request body */
    }
    if (tb_web_ota_busy()) {
        return close_with_error(request, "409 Conflict", "Uma atualização está em andamento.");
    }
    cJSON *config = NULL;
    if (tb_http_read_json(request, TB_MAX_CONFIG_BYTES, &config) != ESP_OK) {
        return ESP_FAIL;
    }
    char error[192] = {0};
    esp_err_t result = tb_config_apply_json(config, error, sizeof(error));
    cJSON_Delete(config);
    if (result != ESP_OK) {
        return send_error(request, "400 Bad Request", error[0] ? error : "Não foi possível salvar a configuração.");
    }
    cJSON *body = cJSON_CreateObject();
    if (body) {
        cJSON_AddBoolToObject(body, "ok", true);
        cJSON_AddBoolToObject(body, "restarting", true);
        cJSON_AddStringToObject(body, "message", "Configuração salva. A placa será reiniciada.");
    }
    result = tb_http_send_json(request, body);
    tb_schedule_restart(1500);
    return result;
}

static esp_err_t pairing_handler(httpd_req_t *request)
{
    if (!tb_http_require_admin(request)) {
        return ESP_OK;
    }
    if (tb_role() != TB_ROLE_CORE) {
        return send_error(request, "404 Not Found", "O código de vínculo é fornecido pelo Core.");
    }
    tb_network_status_t network;
    tb_network_status(&network);
    const tb_config_t *config = tb_config();
    cJSON *body = cJSON_CreateObject();
    if (body) {
        cJSON_AddBoolToObject(body, "ok", true);
        cJSON_AddStringToObject(body, "ssid", network.ap_ssid);
        cJSON_AddStringToObject(body, "password", config->ap_password);
        cJSON_AddStringToObject(body, "pair_key", config->pair_key);
        char link_host[16];
        tb_network_link_host(link_host, sizeof(link_host));
        cJSON_AddStringToObject(body, "host", link_host);
        cJSON_AddStringToObject(body, "core_host", network.station_connected ? network.station_ip : TB_CORE_AP_IP);
    }
    return tb_http_send_json(request, body);
}

static bool upload_size_check(httpd_req_t *request, size_t limit)
{
    char content_type[80] = {0};
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/octet-stream")) {
        send_error(request, "415 Unsupported Media Type", "Envie o arquivo como application/octet-stream.");
        return false;
    }
    if (request->content_len < TB_IMAGE_PREFIX_SIZE || request->content_len > limit || request->content_len > INT_MAX) {
        send_error(request, "413 Payload Too Large", "Arquivo vazio, incompleto ou maior que a partição OTA.");
        return false;
    }
    return true;
}

static esp_err_t receive_prefix(httpd_req_t *request, uint8_t *prefix, int64_t deadline)
{
    size_t used = 0;
    while (used < TB_IMAGE_PREFIX_SIZE) {
        int count = receive_body(request, (char *)prefix + used, TB_IMAGE_PREFIX_SIZE - used, deadline);
        if (count <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        used += (size_t)count;
    }
    return ESP_OK;
}

static esp_err_t ota_local_handler(httpd_req_t *request)
{
    if (!require_auth(request, true, tb_role() == TB_ROLE_LINK)) {
        return ESP_FAIL;
    }
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!partition || !running || partition->address == running->address) {
        return close_with_error(request, "503 Service Unavailable", "A partição OTA de reserva não está disponível.");
    }
    if (!upload_size_check(request, partition->size)) {
        return ESP_FAIL;
    }
    if (!begin_upload()) {
        return close_with_error(request, "409 Conflict", "Uma atualização já está em andamento.");
    }

    esp_err_t result = ESP_FAIL;
    const char *failure_status = "400 Bad Request";
    const char *failure = "A transmissão do firmware foi interrompida; a versão anterior foi preservada.";
    esp_ota_handle_t handle = 0;
    uint8_t *chunk = malloc(TB_OTA_CHUNK_SIZE);
    uint8_t prefix[TB_IMAGE_PREFIX_SIZE];
    esp_app_desc_t description;
    char policy_error[192];
    int64_t deadline = esp_timer_get_time() + TB_UPLOAD_DEADLINE_US;
    if (!chunk) {
        failure_status = "500 Internal Server Error";
        failure = "Sem memória disponível para receber o firmware.";
        goto cleanup;
    }
    if (receive_prefix(request, prefix, deadline) != ESP_OK) {
        failure_status = "408 Request Timeout";
        goto cleanup;
    }
    if (!tb_image_prefix_valid(prefix, sizeof(prefix), tb_role() == TB_ROLE_LINK,
                               &description, policy_error, sizeof(policy_error))) {
        failure = policy_error;
        goto cleanup;
    }

    result = esp_ota_begin(partition, request->content_len, &handle);
    if (result != ESP_OK) {
        failure_status = "409 Conflict";
        failure = "Não foi possível iniciar a OTA. Verifique a confirmação da versão atual e a partição de reserva.";
        goto cleanup;
    }
    result = esp_ota_write(handle, prefix, sizeof(prefix));
    if (result != ESP_OK) {
        failure_status = "500 Internal Server Error";
        failure = "Falha ao escrever na partição de reserva.";
        goto cleanup;
    }
    size_t received = sizeof(prefix);
    while (received < request->content_len) {
        size_t needed = request->content_len - received;
        if (needed > TB_OTA_CHUNK_SIZE) {
            needed = TB_OTA_CHUNK_SIZE;
        }
        int count = receive_body(request, (char *)chunk, needed, deadline);
        if (count <= 0) {
            failure_status = "408 Request Timeout";
            goto cleanup;
        }
        result = esp_ota_write(handle, chunk, (size_t)count);
        if (result != ESP_OK) {
            failure_status = "500 Internal Server Error";
            failure = "Falha ao escrever na partição de reserva.";
            goto cleanup;
        }
        received += (size_t)count;
    }
    result = esp_ota_end(handle);
    handle = 0; /* esp_ota_end releases the handle even when validation fails. */
    if (result != ESP_OK) {
        failure = "A imagem falhou na verificação de integridade ou de compatibilidade. A versão anterior foi preservada.";
        goto cleanup;
    }
    result = esp_ota_set_boot_partition(partition);
    if (result != ESP_OK) {
        failure_status = "500 Internal Server Error";
        failure = "Não foi possível selecionar a nova imagem. A versão atual permanece ativa.";
        goto cleanup;
    }

    free(chunk);
    /* Keep the lock until the scheduled reboot: the selected boot slot must not
     * be erased by another upload during the response/restart interval. */
    cJSON *body = cJSON_CreateObject();
    if (body) {
        cJSON_AddBoolToObject(body, "ok", true);
        cJSON_AddBoolToObject(body, "restarting", true);
        cJSON_AddStringToObject(body, "target", tb_role_name());
        cJSON_AddStringToObject(body, "version", description.version);
        cJSON_AddStringToObject(body, "message", "Imagem recebida e validada. Reiniciando para testar a nova versão.");
    }
    ESP_LOGI(TAG, "OTA validada: %s %s (%u bytes)", tb_role_name(), description.version,
             (unsigned)request->content_len);
    result = tb_http_send_json(request, body);
    tb_schedule_restart(1500);
    return result;

cleanup:
    if (handle) {
        esp_ota_abort(handle);
    }
    free(chunk);
    tb_web_set_ota_busy(false);
    ESP_LOGW(TAG, "OTA local cancelada (%s)", esp_err_to_name(result));
    send_error(request, failure_status, failure);
    return ESP_FAIL; /* closes unread/failed HTTP bodies */
}

static esp_err_t client_write_all(esp_http_client_handle_t client, const uint8_t *bytes, size_t length, int64_t deadline)
{
    size_t sent = 0;
    while (sent < length && esp_timer_get_time() < deadline) {
        int count = esp_http_client_write(client, (const char *)bytes + sent, (int)(length - sent));
        if (count <= 0) {
            return ESP_FAIL;
        }
        sent += (size_t)count;
    }
    return sent == length ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t ota_link_handler(httpd_req_t *request)
{
    if (!tb_http_require_admin(request)) {
        return ESP_FAIL;
    }
    if (tb_role() != TB_ROLE_CORE) {
        return close_with_error(request, "404 Not Found", "Atualização encaminhada está disponível apenas no Core.");
    }
    if (!upload_size_check(request, TB_MAX_OTA_BYTES)) {
        return ESP_FAIL;
    }
    if (!begin_upload()) {
        return close_with_error(request, "409 Conflict", "Uma atualização já está em andamento.");
    }
    esp_err_t result = ESP_FAIL;
    const char *failure_status = "502 Bad Gateway";
    const char *failure = "O Link não confirmou a atualização. Confira alimentação, vínculo e conexão Wi-Fi interna.";
    esp_http_client_handle_t client = NULL;
    uint8_t *chunk = malloc(TB_OTA_CHUNK_SIZE);
    uint8_t prefix[TB_IMAGE_PREFIX_SIZE];
    esp_app_desc_t description;
    char policy_error[192];
    char response[TB_MAX_LINK_RESPONSE + 1];
    int64_t deadline = esp_timer_get_time() + TB_UPLOAD_DEADLINE_US;
    if (!chunk) {
        failure_status = "500 Internal Server Error";
        failure = "Sem memória disponível para encaminhar o firmware.";
        goto cleanup;
    }
    if (receive_prefix(request, prefix, deadline) != ESP_OK) {
        failure_status = "408 Request Timeout";
        failure = "A transmissão do arquivo foi interrompida.";
        goto cleanup;
    }
    if (!tb_image_prefix_valid(prefix, sizeof(prefix), true, &description, policy_error, sizeof(policy_error))) {
        failure_status = "400 Bad Request";
        failure = policy_error;
        goto cleanup;
    }
    if (!tb_config()->pair_key[0]) {
        failure_status = "409 Conflict";
        failure = "Cadastre o vínculo entre Core e Link antes de atualizar o Link.";
        goto cleanup;
    }
    char link_host[16], link_url[64];
    tb_network_link_host(link_host, sizeof(link_host));
    snprintf(link_url, sizeof(link_url), "http://%s/api/ota", link_host);
    const esp_http_client_config_t config = {
        .url = link_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
        .disable_auto_redirect = true,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    client = esp_http_client_init(&config);
    if (!client) {
        failure_status = "500 Internal Server Error";
        failure = "Sem memória disponível para conectar ao Link.";
        goto cleanup;
    }
    char authorization[80];
    snprintf(authorization, sizeof(authorization), "Bearer %s", tb_config()->pair_key);
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    result = esp_http_client_open(client, (int)request->content_len);
    if (result != ESP_OK || client_write_all(client, prefix, sizeof(prefix), deadline) != ESP_OK) {
        goto cleanup;
    }
    size_t received = sizeof(prefix);
    while (received < request->content_len) {
        size_t needed = request->content_len - received;
        if (needed > TB_OTA_CHUNK_SIZE) {
            needed = TB_OTA_CHUNK_SIZE;
        }
        int count = receive_body(request, (char *)chunk, needed, deadline);
        if (count <= 0) {
            failure_status = "408 Request Timeout";
            failure = "A transmissão foi interrompida. O Link manterá a imagem anterior se não concluir a validação.";
            goto cleanup;
        }
        result = client_write_all(client, chunk, (size_t)count, deadline);
        if (result != ESP_OK) {
            goto cleanup;
        }
        received += (size_t)count;
    }
    int64_t response_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (response_length < 0 || response_length > TB_MAX_LINK_RESPONSE || status != 200) {
        goto cleanup;
    }
    size_t used = 0;
    while (used < TB_MAX_LINK_RESPONSE && esp_timer_get_time() < deadline) {
        int count = esp_http_client_read(client, response + used, TB_MAX_LINK_RESPONSE - used);
        if (count < 0) {
            goto cleanup;
        }
        if (count == 0) {
            break;
        }
        used += (size_t)count;
        if (esp_http_client_is_complete_data_received(client)) {
            break;
        }
    }
    if (!esp_http_client_is_complete_data_received(client)) {
        goto cleanup;
    }
    response[used] = '\0';
    cJSON *remote = cJSON_ParseWithLengthOpts(response, used + 1, NULL, true);
    bool confirmed = remote && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(remote, "ok"));
    cJSON_Delete(remote);
    if (!confirmed) {
        goto cleanup;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(chunk);
    tb_web_set_ota_busy(false);
    cJSON *body = cJSON_CreateObject();
    if (body) {
        cJSON_AddBoolToObject(body, "ok", true);
        cJSON_AddBoolToObject(body, "forwarded", true);
        cJSON_AddStringToObject(body, "target", "link");
        cJSON_AddStringToObject(body, "version", description.version);
        cJSON_AddStringToObject(body, "message", "O Link validou a imagem e será reiniciado. O Core continua funcionando.");
    }
    return tb_http_send_json(request, body);

cleanup:
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    free(chunk);
    tb_web_set_ota_busy(false);
    ESP_LOGW(TAG, "OTA encaminhada cancelada (%s)", esp_err_to_name(result));
    send_error(request, failure_status, failure);
    return ESP_FAIL;
}

static esp_err_t not_found_handler(httpd_req_t *request, httpd_err_code_t error)
{
    (void)error;
    if (request->content_len) {
        return close_with_error(request, "404 Not Found", "Página ou operação não encontrada.");
    }
    return send_error(request, "404 Not Found", "Página ou operação não encontrada.");
}

esp_err_t tb_web_start(void)
{
    if (s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 10240;
    config.max_uri_handlers = 32;
    config.max_open_sockets = 6;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 8;
    config.send_wait_timeout = 8;
    esp_err_t result = httpd_start(&s_server, &config);
    if (result != ESP_OK) {
        return result;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = home_handler },
        { .uri = "/api/health", .method = HTTP_GET, .handler = health_handler },
        { .uri = "/api/state", .method = HTTP_GET, .handler = state_handler },
        { .uri = "/api/config", .method = HTTP_GET, .handler = config_get_handler },
        { .uri = "/api/config", .method = HTTP_POST, .handler = config_post_handler },
        { .uri = "/api/pairing", .method = HTTP_GET, .handler = pairing_handler },
        { .uri = "/api/ota", .method = HTTP_POST, .handler = ota_local_handler },
        { .uri = "/api/ota/link", .method = HTTP_POST, .handler = ota_link_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        result = httpd_register_uri_handler(s_server, &routes[i]);
        if (result != ESP_OK) {
            goto failure;
        }
    }
    result = httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, not_found_handler);
    if (result != ESP_OK) {
        goto failure;
    }
    result = tb_application_register_routes(s_server);
    if (result != ESP_OK) {
        goto failure;
    }
    ESP_LOGI(TAG, "Interface web pronta, porta %u", config.server_port);
    return ESP_OK;

failure:
    httpd_stop(s_server);
    s_server = NULL;
    return result;
}
