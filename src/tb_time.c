#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "coap_client.h"
#include "tb_internal.h"

LOG_MODULE_REGISTER(thingsboard_time, CONFIG_THINGSBOARD_LOG_LEVEL);

static struct {
	int64_t tb_time;      // actual Unix timestamp in ms
	int64_t own_time;     // uptime when receiving timestamp in ms
	int64_t last_request; // uptime when time was last requested in ms
} tb_time;

static void client_request_time(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(work_time, client_request_time);

/**
 * Parse an int64_t from a non-zero-terminated buffer.
 *
 * The value is expected to be a valid (and current) timestamp.
 * Hence, the only error case that is handled is that the buffer
 * does not actually consist of digits. No attempt is made to validate
 * the resulting number.
 *
 * Limitations:
 * - It does not actually handle negative values
 * - It does not take care of integer overflows
 * - The buffer must only contain valid digits
 */
static int timestamp_from_buf(int64_t *value, const void *buf, size_t sz)
{
	int64_t result = 0;
	size_t i;
	const char *next;

	for (i = 0, next = buf; i < sz; i++, next++) {
		if (*next < '0' || *next > '9') {
			LOG_WRN("Buffer contains non-digits: %c", *next);
			return -EBADMSG;
		}
		result = result * 10 + (*next - '0');
	}

	*value = result;
	return 0;
}

static int client_handle_time_response(struct coap_client_request *req,
				       struct coap_packet *response)
{
	int64_t ts = 0;
	const uint8_t *payload;
	uint16_t payload_len;
	uint8_t code;
	char code_str[5];
	char expected_code_str[5];
	int err;

	code = coap_header_get_code(response);
	if (code != COAP_RESPONSE_CODE_CONTENT) {
		coap_response_code_to_str(code, code_str);
		coap_response_code_to_str(COAP_RESPONSE_CODE_CONTENT, expected_code_str);
		LOG_ERR("Unexpected response code for timestamp request: got %s, expected %s",
			code_str, expected_code_str);
		return -1;
	}

	payload = coap_packet_get_payload(response, &payload_len);
	if (!payload_len) {
		LOG_ERR("Received empty timestamp");
		return payload_len;
	}

	thingsboard_rpc_response rpc_response;
	err = thingsboard_rpc_response_decode(payload, payload_len, &rpc_response);
	if (err < 0) {
		LOG_ERR("Failed to decode rpc response: %d", err);
		return err;
	}

	err = timestamp_from_buf(&ts, rpc_response.payload, strlen(rpc_response.payload));
	if (err) {
		LOG_ERR("Parsing of time response failed");
		return err;
	}

	tb_time.tb_time = ts;
	tb_time.own_time = k_uptime_get();
	LOG_DBG("Timestamp updated: %lld", ts);

	thingsboard_event(THINGSBOARD_EVENT_TIME_UPDATE);

	/* schedule a refresh request for later. */
	k_work_reschedule(&work_time, K_SECONDS(CONFIG_THINGSBOARD_TIME_REFRESH_INTERVAL_SECONDS));

	return 0;
}

static void client_request_time(struct k_work *work)
{
	int err;

	char payload[32];
	const uint8_t *uri[] = {"api", "v1", thingsboard_access_token, "rpc", NULL};

	thingsboard_rpc_request request = {
		.has_method = true,
		.method = "getCurrentTime",
	};

	size_t request_len = ARRAY_SIZE(payload);
	err = thingsboard_rpc_request_encode(&request, payload, &request_len);
	if (err < 0) {
		LOG_ERR("Failed to encode time request");
		goto error;
	}

	err = coap_client_make_request(uri, payload, request_len, COAP_TYPE_CON, COAP_METHOD_POST,
				       THINGSBOARD_DEFAULT_CONTENT_FORMAT,
				       client_handle_time_response);
	if (err) {
		LOG_ERR("Failed to request time");
	}

error:
	tb_time.last_request = k_uptime_get();

	// Fallback to ask for time, if we don't receive a response.
	k_work_reschedule(
		k_work_delayable_from_work(work),
		K_SECONDS(CONFIG_COAP_INIT_ACK_TIMEOUT_MS * (2 << CONFIG_COAP_NUM_RETRIES)));
}

time_t thingsboard_time(void)
{
	return thingsboard_time_msec() / MSEC_PER_SEC;
}

time_t thingsboard_time_msec(void)
{
	time_t result = (time_t)((k_uptime_get() - tb_time.own_time) + tb_time.tb_time);
	return result;
}

void thingsboard_start_time_sync(void)
{
	if (k_work_reschedule(&work_time, K_NO_WAIT) < 0) {
		LOG_ERR("Failed to schedule time worker!");
	}
}
