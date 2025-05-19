#include "thingsboard.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>

#include "coap_client.h"
#include "provision.h"
#include "tb_internal.h"

LOG_MODULE_REGISTER(thingsboard_client, CONFIG_THINGSBOARD_LOG_LEVEL);

static const struct thingsboard_configuration *config;

const char *thingsboard_access_token;
char thingsboard_serde_buffer[CONFIG_THINGSBOARD_SERDE_BUFFER_SIZE];

static int client_handle_attribute_notification(struct coap_client_request *req,
						struct coap_packet *response)
{
	uint8_t *payload;
	uint16_t payload_len;
	thingsboard_attributes attr = {0};
	int err;

	payload = (uint8_t *)coap_packet_get_payload(response, &payload_len);
	if (!payload_len) {
		LOG_WRN("Received empty attributes");
		return payload_len;
	}
	LOG_HEXDUMP_DBG(payload, payload_len, "Received attributes");

	err = thingsboard_attributes_decode(payload, payload_len, &attr);
	if (err < 0) {
		LOG_ERR("Parsing attributes failed");
		return err;
	}

#ifdef CONFIG_THINGSBOARD_FOTA
	thingsboard_fota_on_attributes(&attr);
#endif

	if (config != NULL && config->callbacks.on_attributes_write) {
		config->callbacks.on_attributes_write(&attr);
	}
	return 0;
}

static int client_subscribe_to_attributes(void)
{
	int err;
	struct coap_client_request *request;

	request = coap_client_request_alloc(COAP_TYPE_CON, COAP_METHOD_GET);
	if (!request) {
		return -ENOMEM;
	}

	err = coap_client_request_observe(request);
	if (err < 0) {
		return err;
	}

	const uint8_t *uri[] = {"api", "v1", thingsboard_access_token, "attributes", NULL};
	err = coap_packet_append_uri_path(&request->pkt, uri);
	if (err < 0) {
		return err;
	}

	err = coap_client_send(request, client_handle_attribute_notification);
	if (err < 0) {
		return err;
	}

	LOG_DBG("Attributes subscription request sent");

	return 0;
}

int thingsboard_send_telemetry(const thingsboard_telemetry *telemetry)
{
#ifdef CONFIG_THINGSBOARD_TELEMETRY_ALWAYS_TIMESTAMP
	thingsboard_timeseries timeseries = {
		.ts = thingsboard_time_msec(),
		.has_values = true,
		.values = *telemetry,
	};

	return thingsboard_send_timeseries(&timeseries, 1);
#else  /* CONFIG_THINGSBOARD_TELEMETRY_ALWAYS_TIMESTAMP */
	size_t buffer_length = sizeof(thingsboard_serde_buffer);
	int err = thingsboard_telemetry_encode(telemetry, thingsboard_serde_buffer, &buffer_length);
	if (err < 0) {
		return err;
	}

	return thingsboard_send_telemetry_buf(thingsboard_serde_buffer, buffer_length);
#endif /* CONFIG_THINGSBOARD_TELEMETRY_ALWAYS_TIMESTAMP */
}

int thingsboard_send_timeseries(const thingsboard_timeseries *ts, size_t ts_count)
{
	size_t ts_sent = 0;

	__ASSERT_NO_MSG(ts);
	__ASSERT_NO_MSG(ts_count > 0);

	while ((ts_count - ts_sent) > 0) {
		size_t buffer_length = sizeof(thingsboard_serde_buffer);
		size_t ts_to_send = ts_count - ts_sent;
		int err = thingsboard_timeseries_encode(&ts[ts_sent], &ts_to_send,
							thingsboard_serde_buffer, &buffer_length);
		if (err < 0) {
			return err;
		}
		__ASSERT_NO_MSG(ts_to_send != 0);

		err = thingsboard_send_telemetry_buf(thingsboard_serde_buffer, buffer_length);
		if (err < 0) {
			return err;
		}

		ts_sent += ts_to_send;
	}

	return 0;
}

int thingsboard_send_telemetry_buf(const void *payload, size_t sz)
{
	int err;

	if (!thingsboard_access_token) {
		return -ENOENT;
	}

	const uint8_t *uri[] = {"api", "v1", thingsboard_access_token, "telemetry", NULL};
	err = coap_client_make_request(uri, payload, sz, COAP_TYPE_CON, COAP_METHOD_POST,
				       THINGSBOARD_DEFAULT_CONTENT_FORMAT, NULL);
	return err;
}

void thingsboard_event(enum thingsboard_event event)
{
	if (config != NULL && config->callbacks.on_event) {
		config->callbacks.on_event(event);
	}
}

static void start_client(void);

static void prov_callback(const char *token)
{
	LOG_INF("Device provisioned");
	thingsboard_access_token = token;

#ifdef CONFIG_THINGSBOARD_FOTA
	thingsboard_fota_init(&config->current_firmware);

	if (thingsboard_fota_confirm_update() != 0) {
		LOG_ERR("Failed to confirm FW update");
	}
#endif

	thingsboard_event(THINGSBOARD_EVENT_PROVISIONED);

	start_client();
}

static void start_client(void)
{
	if (!thingsboard_access_token) {
		LOG_INF("No access token in storage. Requesting provisioning.");

		int err = thingsboard_provision_device(config->device_name, prov_callback);
		if (err) {
			LOG_ERR("Could not provision device");
			return;
		}

		return;
	}

	if (client_subscribe_to_attributes() != 0) {
		LOG_ERR("Failed to observe attributes");
	}

#ifdef CONFIG_THINGSBOARD_TIME
	thingsboard_start_time_sync();
#endif /* CONFIG_THINGSBOARD_TIME */

	thingsboard_event(THINGSBOARD_EVENT_ACTIVE);
}

static bool string_is_set(const char *str)
{
	return (str != NULL && strlen(str) > 0);
}

int thingsboard_init(const struct thingsboard_configuration *configuration)
{
	int ret;

	if (configuration == NULL) {
		LOG_ERR("`configuration` may not be NULL");
		return -EINVAL;
	}

	if (!string_is_set(configuration->device_name)) {
		LOG_ERR("`device_name` must be set");
		return -EINVAL;
	}

	if (!string_is_set(configuration->server_hostname)) {
		LOG_ERR("`server_hostname` must be set");
		return -EINVAL;
	}

	if (IS_ENABLED(CONFIG_THINGSBOARD_FOTA)) {
		if (!string_is_set(configuration->current_firmware.title)) {
			LOG_ERR("`current_firmware.title` must be set");
			return -EINVAL;
		}
		if (!string_is_set(configuration->current_firmware.version)) {
			LOG_ERR("`current_firmware.version` must be set");
			return -EINVAL;
		}
	}

#ifdef CONFIG_THINGSBOARD_DTLS
	if (configuration->security.tags == NULL ||
	    configuration->security.tags_size < sizeof(sec_tag_t)) {
		LOG_ERR("`security.tags` must be set");
		return -EINVAL;
	}
#endif /* CONFIG_THINGSBOARD_DTLS */

	config = configuration;

	struct sockaddr_storage *server_address = NULL;
	size_t server_address_len = 0;
	ret = thingsboard_socket_connect(config, &server_address, &server_address_len);
	if (ret < 0) {
		LOG_ERR("Failed to connect socket: %d", ret);
		return -ENETUNREACH;
	}
	int thingsboard_server_socket = ret;

	ret = coap_client_init(thingsboard_server_socket, server_address, server_address_len,
			       start_client);
	if (ret != 0) {
		LOG_ERR("Failed to initialize CoAP client (%d)", ret);
		return ret;
	}

	return 0;
}
