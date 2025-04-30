#include "thingsboard.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>

#include "coap_client.h"
#include "provision.h"
#include "tb_fota.h"
#include "tb_internal.h"

LOG_MODULE_REGISTER(thingsboard_client, CONFIG_THINGSBOARD_LOG_LEVEL);

static struct thingsboard_cbs *callbacks;

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
	thingsboard_check_fw_attributes(&attr);
#endif

	if (callbacks && callbacks->on_attributes_write) {
		callbacks->on_attributes_write(&attr);
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
	size_t buffer_length = sizeof(thingsboard_serde_buffer);
	int err = thingsboard_telemetry_encode(telemetry, thingsboard_serde_buffer, &buffer_length);
	if (err < 0) {
		return err;
	}

	return thingsboard_send_telemetry_buf(thingsboard_serde_buffer, buffer_length);
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
	err = coap_client_make_request(uri, payload, sz, COAP_TYPE_CON, COAP_METHOD_POST, NULL);
	return err;
}

void thingsboard_event(enum thingsboard_event event)
{

	if (callbacks && callbacks->on_event) {
		callbacks->on_event(event);
	}
}

static void start_client(void);

static const struct tb_fw_id *current_fw;

static void prov_callback(const char *token)
{
	LOG_INF("Device provisioned");
	thingsboard_access_token = token;

#ifdef CONFIG_THINGSBOARD_FOTA
	thingsboard_fota_init(thingsboard_access_token, current_fw);

	if (confirm_fw_update() != 0) {
		LOG_ERR("Failed to confirm FW update");
	}
#endif

	if (callbacks && callbacks->on_event) {
		callbacks->on_event(THINGSBOARD_EVENT_PROVISIONED);
	}

	start_client();
}

static void start_client(void)
{
	int err;

	if (!thingsboard_access_token) {
		LOG_INF("No access token in storage. Requesting provisioning.");

		err = thingsboard_provision_device(current_fw->device_name, prov_callback);
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

	if (callbacks && callbacks->on_event) {
		callbacks->on_event(THINGSBOARD_EVENT_ACTIVE);
	}
}

int thingsboard_init(struct thingsboard_cbs *cbs, const struct tb_fw_id *fw_id)
{
	callbacks = cbs;
	int ret;

	current_fw = fw_id;

	ret = coap_client_init(start_client);
	if (ret != 0) {
		LOG_ERR("Failed to initialize CoAP client (%d)", ret);
		return ret;
	}

	return 0;
}
