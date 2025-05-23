#include <stdio.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <thingsboard_provision_response_serde.h>

#include "coap_client.h"
#include "tb_internal.h"

LOG_MODULE_REGISTER(tb_provision, CONFIG_THINGSBOARD_LOG_LEVEL);

static thingsboard_provisiong_callback prov_cb;
static char access_token[30];

#define THINGSBOARD_TOKEN_SETTINGS_KEY "thingsboard/token"

static int token_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next = NULL;
	int err;

	if (settings_name_steq(name, "token", &next) && !next) {
		LOG_INF("Retrieving access token from flash");
		if (len > sizeof(access_token)) {
			return -EINVAL;
		}
		err = read_cb(cb_arg, access_token, len);
		if (err < 0) {
			LOG_ERR("Failed to read token: %d", err);
			return err;
		}
		LOG_DBG("Access token retreived, %d B", err);
		return 0;
	}

	return -ENOENT;
}

static int token_settings_export(int (*storage_func)(const char *name, const void *value,
						     size_t val_len))
{
	LOG_INF("Persisting access token");
	return storage_func(THINGSBOARD_TOKEN_SETTINGS_KEY, access_token, sizeof(access_token));
}
static SETTINGS_STATIC_HANDLER_DEFINE(token_settings_conf, "thingsboard", NULL, token_settings_set,
				      NULL, token_settings_export);

static int client_handle_prov_resp(struct coap_client_request *req, struct coap_packet *response)
{
	uint8_t *payload;
	uint16_t payload_len;
	struct thingsboard_provision_response result = {0};
	int err;
	size_t tkl;

	payload = (uint8_t *)coap_packet_get_payload(response, &payload_len);
	if (!payload_len) {
		LOG_WRN("Received empty provisioning response");
		return -ENOMSG;
	}

	err = thingsboard_provision_response_from_json(payload, payload_len, &result);
	if (err < 0) {
		LOG_HEXDUMP_ERR(payload, payload_len, "Could not parse payload");
		return err;
	}

	if (!result.has_status) {
		LOG_ERR("Provisioning response incomplete");
		return -EBADMSG;
	}

	if (strcmp(result.status, "SUCCESS") != 0) {
		LOG_ERR("Provisioning was not successful: \"%s\"", result.status);
		return -EBADMSG;
	}

	if (!result.has_credentialsType) {
		LOG_ERR("Provisioning response incomplete");
		return -EBADMSG;
	}

	if (strcmp(result.credentialsType, "ACCESS_TOKEN") != 0) {
		LOG_ERR("Got unexpected credentials type \"%s\"", result.credentialsType);
		return -EBADMSG;
	}

	if (!result.has_credentialsValue) {
		LOG_ERR("Provisioning response incomplete");
		return -EBADMSG;
	}

	tkl = strlen(result.credentialsValue);
	if (tkl >= sizeof(access_token)) {
		LOG_ERR("Token too long");
		return -ENOMEM;
	}

	strncpy(access_token, result.credentialsValue, tkl + 1);
	LOG_INF("Obtained access token");

	err = settings_save_one(THINGSBOARD_TOKEN_SETTINGS_KEY, access_token, tkl + 1);
	if (err) {
		LOG_WRN("Failed to save access token");
	}

	if (prov_cb) {
		prov_cb(access_token);
	}

	return 0;
}

static int make_provisioning_request(const char *device_name)
{
	static const char prov_key[] = CONFIG_THINGSBOARD_PROVISIONING_KEY;
	static const char prov_secret[] = CONFIG_THINGSBOARD_PROVISIONING_SECRET;
	static const char request_fmt[] = "{\"deviceName\": \"%s\",\"provisionDeviceKey\": "
					  "\"%s\",\"provisionDeviceSecret\": \"%s\"}";
	char request[sizeof(prov_key) + sizeof(prov_secret) + sizeof(request_fmt) + 30];
	int err;
	const uint8_t *uri[] = {"api", "v1", "provision", NULL};

	err = snprintf(request, sizeof(request), request_fmt, device_name, prov_key, prov_secret);
	if (err < 0 || err >= sizeof(request)) {
		return -ENOMEM;
	}

	err = coap_client_make_request(uri, request, err, COAP_TYPE_CON, COAP_METHOD_POST,
				       COAP_CONTENT_FORMAT_APP_JSON, client_handle_prov_resp);
	if (err) {
		LOG_ERR("Failed to make provisioning request");
		return err;
	}

	return 0;
}

int thingsboard_provision_device(const char *device_name, thingsboard_provisiong_callback cb)
{
	int err;

	prov_cb = cb;

	err = settings_subsys_init();
	if (err) {
		LOG_ERR("Failed to initialize settings subsystem, error (%d): %s", err,
			strerror(-err));
		return err;
	}

	err = settings_load();
	if (err) {
		LOG_ERR("Could not load settings");
		return err;
	}

	if (access_token[0] == '\0') {
		return make_provisioning_request(device_name);
	}

	prov_cb(access_token);

	return 0;
}
