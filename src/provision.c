#include <stdio.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <thingsboard_provision_response_serde.h>

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

static void client_handle_prov_resp(int16_t result_code, size_t offset, const uint8_t *payload,
				    size_t len, bool last_block, void *user_data)
{
	struct thingsboard_request *request = user_data;

	if (result_code < 0) {
		LOG_ERR("Failed to send provisioning request: %" PRId16, result_code);
		goto out;
	}

	struct thingsboard_provision_response result = {0};
	int err;
	size_t tkl;

	err = thingsboard_provision_response_from_json((char *)payload, len, &result);
	if (err < 0) {
		LOG_HEXDUMP_ERR(payload, len, "Could not parse provisioning response");
		goto out;
	}

	if (!result.has_status) {
		LOG_ERR("Provisioning response incomplete");
		goto out;
	}

	if (strcmp(result.status, "SUCCESS") != 0) {
		LOG_ERR("Provisioning was not successful: \"%s\"", result.status);
		goto out;
	}

	if (!result.has_credentialsType) {
		LOG_ERR("Provisioning response incomplete");
		goto out;
	}

	if (strcmp(result.credentialsType, "ACCESS_TOKEN") != 0) {
		LOG_ERR("Got unexpected credentials type \"%s\"", result.credentialsType);
		goto out;
	}

	if (!result.has_credentialsValue) {
		LOG_ERR("Provisioning response incomplete");
		goto out;
	}

	tkl = strlen(result.credentialsValue);
	if (tkl >= sizeof(access_token)) {
		LOG_ERR("Token too long");
		goto out;
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
out:
	thingsboard_request_free(request);

	return;
}

static int make_provisioning_request(const char *device_name)
{
	static const char prov_key[] = CONFIG_THINGSBOARD_PROVISIONING_KEY;
	static const char prov_secret[] = CONFIG_THINGSBOARD_PROVISIONING_SECRET;
	static const char request_fmt[] = "{\"deviceName\": \"%s\",\"provisionDeviceKey\": "
					  "\"%s\",\"provisionDeviceSecret\": \"%s\"}";
	int err;

	struct thingsboard_request *request = thingsboard_request_alloc();
	if (request == NULL) {
		return -ENOMEM;
	}

	err = snprintf(request->payload, sizeof(request->payload), request_fmt, device_name,
		       prov_key, prov_secret);
	if (err < 0 || err >= sizeof(request->payload)) {
		err = -ENOMEM;
		goto error;
	}

	request->coap_request = (struct coap_client_request){
		.payload = request->payload,
		.len = strlen(request->payload),
		.confirmable = true,
		.method = COAP_METHOD_POST,
		.fmt = COAP_CONTENT_FORMAT_APP_JSON,
		.path = "/api/v1/provision",
		.cb = client_handle_prov_resp,
		.user_data = request,
	};

	err = coap_client_req(&thingsboard_client.coap_client, thingsboard_client.server_socket,
			      (struct sockaddr *)thingsboard_client.server_address,
			      &request->coap_request, NULL);
	if (err < 0) {
		LOG_ERR("Failed to send provisioning request: %d", err);
		err = -EFAULT;
		goto error;
	}
	return 0;

error:
	thingsboard_request_free(request);
	return err;
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
