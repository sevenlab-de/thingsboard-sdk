#include "thingsboard.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>

#include "tb_internal.h"

LOG_MODULE_REGISTER(thingsboard_client, CONFIG_THINGSBOARD_LOG_LEVEL);

struct thingsboard_client thingsboard_client = {
	.server_socket = -1,
	.state = THINGSBOARD_STATE_INIT,
};

K_MEM_SLAB_DEFINE_STATIC(request_slab, sizeof(struct thingsboard_request),
			 CONFIG_COAP_CLIENT_MAX_REQUESTS, 4);

static void start_client(void);

void thingsboard_lock(void)
{
	(void)k_mutex_lock(&thingsboard_client.lock, K_FOREVER);
}

void thingsboard_unlock(void)
{
	int err = k_mutex_unlock(&thingsboard_client.lock);
	__ASSERT_NO_MSG(err == 0);
	(void)err;
}

int thingsboard_cat_path(const char *in[], char *out, size_t out_len)
{
	__ASSERT_NO_MSG(in != NULL);
	__ASSERT_NO_MSG(out != NULL);
	__ASSERT_NO_MSG(out_len > 0);

	size_t pos = 0;
	size_t left = out_len;
	for (size_t i = 0; in[i] != NULL; i++) {
		if (left < 1) {
			return -ENOMEM;
		}
		out[pos] = '/';
		pos++;
		left--;

		size_t in_len = strlen(in[i]);
		if (left < in_len) {
			return -ENOMEM;
		}
		memcpy(&out[pos], in[i], left);
		pos += in_len;
		left -= in_len;
	}

	if (pos > out_len) {
		return -ENOMEM;
	}

	out[pos] = 0;

	return 0;
}

struct thingsboard_request *thingsboard_request_alloc(void)
{
	void *slab;
	int err = k_mem_slab_alloc(&request_slab, &slab, K_NO_WAIT);
	if (err != 0) {
		LOG_ERR("Failed to allocate request: %d", err);
		return NULL;
	}

	memset(slab, 0, sizeof(struct thingsboard_request));

	return slab;
}

void thingsboard_request_free(struct thingsboard_request *request)
{
	k_mem_slab_free(&request_slab, request);
}

bool thingsboard_is_active(void)
{
	return thingsboard_client.state == THINGSBOARD_STATE_CONNECTED;
}

static const char *thingsboard_state_to_a(enum thingsboard_state state)
{
	switch (state) {
	case THINGSBOARD_STATE_INIT:
		return "INIT";
	case THINGSBOARD_STATE_CONNECTING:
		return "CONNECTING";
	case THINGSBOARD_STATE_CONNECTED:
		return "CONNECTED";
	case THINGSBOARD_STATE_SUSPENDED:
		return "SUSPENDED";
	case THINGSBOARD_STATE_DISCONNECTED:
		return "DISCONNECTED";
	}

	return "UNKNOWN";
}

static void thingsboard_set_state(enum thingsboard_state new_state);

static void thingsboard_handle_state_connecting(void)
{
	start_client();
}

static void thingsboard_handle_state_connected(void)
{
	thingsboard_event(THINGSBOARD_EVENT_ACTIVE);
}

static void thingsboard_handle_state_suspended(void)
{
	thingsboard_event(THINGSBOARD_EVENT_SUSPENDED);
}

static void thingsboard_handle_state_disconnected(void)
{
	thingsboard_event(THINGSBOARD_EVENT_DISCONNECTED);
}

static void thingsboard_set_state(enum thingsboard_state new_state)
{
	thingsboard_lock();

	if (new_state == thingsboard_client.state) {
		thingsboard_unlock();
		return;
	}

	LOG_DBG("%s -> %s", thingsboard_state_to_a(thingsboard_client.state),
		thingsboard_state_to_a(new_state));

	thingsboard_client.state = new_state;

	switch (new_state) {
	case THINGSBOARD_STATE_INIT:
		/* This is not expected to happen */
		__ASSERT_NO_MSG(false);
		break;
	case THINGSBOARD_STATE_CONNECTING:
		thingsboard_handle_state_connecting();
		break;
	case THINGSBOARD_STATE_CONNECTED:
		thingsboard_handle_state_connected();
		break;
	case THINGSBOARD_STATE_SUSPENDED:
		thingsboard_handle_state_suspended();
		break;
	case THINGSBOARD_STATE_DISCONNECTED:
		thingsboard_handle_state_disconnected();
		break;
	}

	thingsboard_unlock();
}

int thingsboard_connect(void)
{
	thingsboard_lock();

	switch (thingsboard_client.state) {
	case THINGSBOARD_STATE_CONNECTED:
	case THINGSBOARD_STATE_SUSPENDED:
		thingsboard_unlock();
		return -EALREADY;
	case THINGSBOARD_STATE_DISCONNECTED:
		break;
	default:
		thingsboard_unlock();
		return -EINVAL;
	}

	int ret = thingsboard_socket_connect(thingsboard_client.config,
					     &thingsboard_client.server_address,
					     &thingsboard_client.server_address_len);
	if (ret < 0) {
		LOG_ERR("Failed to connect socket: %d", ret);
		thingsboard_set_state(THINGSBOARD_STATE_DISCONNECTED);
		thingsboard_unlock();
		return -ENOTCONN;
	}
	thingsboard_client.server_socket = ret;

	thingsboard_set_state(THINGSBOARD_STATE_CONNECTING);

	thingsboard_unlock();

	return 0;
}

int thingsboard_disconnect(void)
{
	thingsboard_lock();

	switch (thingsboard_client.state) {
	case THINGSBOARD_STATE_CONNECTED:
	case THINGSBOARD_STATE_SUSPENDED:
		break;
	case THINGSBOARD_STATE_DISCONNECTED:
		thingsboard_unlock();
		return -EALREADY;
	default:
		thingsboard_unlock();
		return -EINVAL;
	}

	int err = thingsboard_client_unsubscribe_attributes();
	if (err == -EALREADY) {
		LOG_DBG("Was not subscribed to attributes notification");
	}

#ifdef CONFIG_THINGSBOARD_TIME
	thingsboard_stop_time_sync();
#endif /* CONFIG_THINGSBOARD_TIME */

	thingsboard_socket_close(thingsboard_client.server_socket);

	thingsboard_set_state(THINGSBOARD_STATE_DISCONNECTED);

	thingsboard_unlock();

	return 0;
}

int thingsboard_suspend(void)
{
	thingsboard_lock();

	if (thingsboard_client.state == THINGSBOARD_STATE_SUSPENDED) {
		thingsboard_unlock();
		return 0;
	}

	if (thingsboard_client.state != THINGSBOARD_STATE_CONNECTED) {
		thingsboard_unlock();
		return -EINVAL;
	}

	int err = thingsboard_socket_suspend(&thingsboard_client.server_socket);
	if (err < 0) {
		LOG_ERR("Failed to suspend socket: %d", err);
		thingsboard_unlock();
		return -EIO;
	}

	thingsboard_set_state(THINGSBOARD_STATE_SUSPENDED);

	thingsboard_unlock();

	return 0;
}

int thingsboard_resume(void)
{
	thingsboard_lock();

	if (thingsboard_client.state == THINGSBOARD_STATE_CONNECTED) {
		thingsboard_unlock();
		return 0;
	}

	if (thingsboard_client.state != THINGSBOARD_STATE_SUSPENDED) {
		thingsboard_unlock();
		return -EINVAL;
	}

	int err = thingsboard_socket_resume(&thingsboard_client.server_socket);
	if (err < 0) {
		LOG_ERR("Failed to resume socket: %d", err);
		thingsboard_unlock();
		return -EIO;
	}

	thingsboard_set_state(THINGSBOARD_STATE_CONNECTED);

	thingsboard_unlock();

	return 0;
}

static void coap_decode_response_code(uint8_t code, uint8_t *class, uint8_t *detail)
{
	*class = (code >> 5);
	*detail = code & 0x1f;
}

static void coap_response_code_to_str(uint8_t code, char str[5])
{
	uint8_t class;
	uint8_t detail;
	coap_decode_response_code(code, &class, &detail);
	// class: 1 digit, detail: 2 digits
	sprintf(str, "%" PRIu8 ".%02" PRIu8, class, detail);
}

static void client_handle_attribute_notification(int16_t result_code, size_t offset,
						 const uint8_t *payload, size_t len,
						 bool last_block, void *user_data)
{
	thingsboard_attributes attr = {0};
	struct thingsboard_request *request = user_data;
	int err;

	thingsboard_lock();

	if (result_code == -ECANCELED) {
		LOG_DBG("Attributes subscription has been canceled");
		goto out;
	}

	if (result_code < 0) {
		LOG_ERR("Attributes notification failed: %d", result_code);
		goto out;
	}

	if (!len) {
		LOG_WRN("Received empty attributes");
		goto out;
	}
	LOG_HEXDUMP_DBG(payload, len, "Received attributes");

	err = thingsboard_attributes_decode(payload, len, &attr);
	if (err < 0) {
		LOG_ERR("Parsing attributes failed");
		goto out;
	}

#ifdef CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON
	ssize_t ret =
		thingsboard_attributes_update(&attr, &thingsboard_client.shared_attributes,
					      &thingsboard_client.shared_attributes_buffer,
					      sizeof(thingsboard_client.shared_attributes_buffer));
#else  /* CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */
	ssize_t ret = thingsboard_attributes_update(&attr, &thingsboard_client.shared_attributes,
						    NULL, 0);
#endif /* CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */
	if (ret < 0) {
		LOG_ERR("Failed to update shared attributes: %d", (int)ret);
		thingsboard_unlock();
		return;
	}

	LOG_DBG("%zi shared attributes changed", ret);

#ifdef CONFIG_THINGSBOARD_FOTA
	thingsboard_fota_on_attributes();
#endif

	if (thingsboard_client.config != NULL &&
	    thingsboard_client.config->callbacks.on_attributes_write) {
		thingsboard_client.config->callbacks.on_attributes_write(&attr);
	}

out:
	if (last_block) {
		thingsboard_client.attributes_observation = NULL;
		thingsboard_request_free(request);
	}

	thingsboard_unlock();
}

int thingsboard_client_subscribe_attributes(void)
{
	int err;

	__ASSERT_NO_MSG(thingsboard_client.attributes_observation == NULL);

	thingsboard_client.attributes_observation = thingsboard_request_alloc();
	if (thingsboard_client.attributes_observation == NULL) {
		return -ENOMEM;
	}

	err = thingsboard_cat_path(THINGSBOARD_PATH_ATTRIBUTES,
				   thingsboard_client.attributes_observation->path,
				   sizeof(thingsboard_client.attributes_observation->path));
	if (err < 0) {
		thingsboard_request_free(thingsboard_client.attributes_observation);
		thingsboard_client.attributes_observation = NULL;
		return -EFAULT;
	}

	thingsboard_client.attributes_observation->options[0].code = COAP_OPTION_OBSERVE;
	thingsboard_client.attributes_observation->options[0].len = 0;

	struct coap_client_request coap_request = {
		.confirmable = true,
		.method = COAP_METHOD_GET,
		.path = thingsboard_client.attributes_observation->path,
		.options = thingsboard_client.attributes_observation->options,
		.num_options = 1,
		.cb = client_handle_attribute_notification,
		.user_data = thingsboard_client.attributes_observation,
	};

	err = coap_client_req(&thingsboard_client.coap_client, thingsboard_client.server_socket,
			      (struct sockaddr *)thingsboard_client.server_address, &coap_request,
			      NULL);
	if (err < 0) {
		LOG_ERR("Failed to send attributes observation: %d", err);
		thingsboard_request_free(thingsboard_client.attributes_observation);
		thingsboard_client.attributes_observation = NULL;
		return -EIO;
	}

	LOG_DBG("Attributes subscription request sent");

	return 0;
}

int thingsboard_client_unsubscribe_attributes(void)
{
	if (thingsboard_client.attributes_observation == NULL) {
		return -EALREADY;
	}

	coap_client_cancel_request(&thingsboard_client.coap_client,
				   &(struct coap_client_request){
					   .user_data = thingsboard_client.attributes_observation});

	return 0;
}

int thingsboard_send_telemetry_request(struct thingsboard_request *request, size_t sz);

int thingsboard_send_telemetry(const thingsboard_telemetry *telemetry)
{
	__ASSERT_NO_MSG(telemetry);

	if (!thingsboard_is_active()) {
		return -EAGAIN;
	}

#ifdef CONFIG_THINGSBOARD_TELEMETRY_ALWAYS_TIMESTAMP
	thingsboard_timeseries timeseries = {
		.ts = thingsboard_time_msec(),
		.has_values = true,
		.values = *telemetry,
	};

	return thingsboard_send_timeseries(&timeseries, 1);
#else  /* CONFIG_THINGSBOARD_TELEMETRY_ALWAYS_TIMESTAMP */
	struct thingsboard_request *request = thingsboard_request_alloc();
	if (request == NULL) {
		return -ENOMEM;
	}

	size_t buffer_length = sizeof(request->payload);
	int err = thingsboard_telemetry_encode(telemetry, request->payload, &buffer_length);
	if (err < 0) {
		thingsboard_request_free(request);
		return err;
	}

	return thingsboard_send_telemetry_request(request, buffer_length);
#endif /* CONFIG_THINGSBOARD_TELEMETRY_ALWAYS_TIMESTAMP */
}

int thingsboard_send_timeseries(const thingsboard_timeseries *ts, size_t ts_count)
{
	if (!thingsboard_is_active()) {
		return -EAGAIN;
	}

	int err = 0;
	struct thingsboard_request *requests[CONFIG_COAP_CLIENT_MAX_REQUESTS] = {NULL};
	size_t payload_len[CONFIG_COAP_CLIENT_MAX_REQUESTS] = {0};
	size_t request_num = 0;
	size_t ts_sent = 0;

	__ASSERT_NO_MSG(ts);
	__ASSERT_NO_MSG(ts_count > 0);

	/* Serialize all telemetry data and prepare all requests to be sent.
	 *
	 * By preparing them first, we know beforehand if we have enough memory
	 * and can send all of them at once or none of them.
	 */
	while ((ts_count - ts_sent) > 0) {
		struct thingsboard_request *request = thingsboard_request_alloc();
		if (request == NULL) {
			err = -ENOMEM;
			goto free_requests;
		}
		requests[request_num] = request;

		size_t buffer_length = sizeof(request->payload);
		size_t ts_to_send = ts_count - ts_sent;
		err = thingsboard_timeseries_encode(&ts[ts_sent], &ts_to_send, request->payload,
						    &buffer_length);
		if (err < 0) {
			err = -EINVAL;
			goto free_requests;
		}
		__ASSERT_NO_MSG(ts_to_send != 0);

		payload_len[request_num] = buffer_length;
		request_num++;

		ts_sent += ts_to_send;
	}

	/* Send all prepared requests */
	for (size_t i = 0; i < request_num; i++) {
		err = thingsboard_send_telemetry_request(requests[i], payload_len[i]);
		if (err < 0) {
			for (i++; i < request_num; i++) {
				thingsboard_request_free(requests[i]);
			}
			return -EIO;
		}
	}

	return 0;

free_requests:
	for (size_t i = 0; i <= request_num; i++) {
		thingsboard_request_free(requests[i]);
	}

	return err;
}

static void thingsboard_handle_response(int16_t result_code, size_t offset, const uint8_t *payload,
					size_t len, bool last_block, void *user_data)
{
	struct thingsboard_request *request = user_data;

	if (result_code < 0) {
		LOG_ERR("Failed to send request: %" PRId16, result_code);
		goto out;
	}

	uint8_t code = result_code;
	char code_str[5];

	coap_response_code_to_str(code, code_str);
	LOG_DBG("Request completed with code %s", code_str);

out:
	if (last_block) {
		thingsboard_request_free(request);
	}
}

int thingsboard_send_telemetry_buf(const void *payload, size_t sz)
{
	__ASSERT_NO_MSG(payload);
	__ASSERT_NO_MSG(sz > 0);

	if (!thingsboard_is_active()) {
		return -EAGAIN;
	}

	if (sz > sizeof(((struct thingsboard_request){}).payload)) {
		return -EINVAL;
	}

	struct thingsboard_request *request = thingsboard_request_alloc();
	if (request == NULL) {
		return -ENOMEM;
	}

	memcpy(request->payload, payload, sz);

	return thingsboard_send_telemetry_request(request, sz);
}

int thingsboard_send_telemetry_request(struct thingsboard_request *request, size_t sz)
{
	int err;

	err = thingsboard_cat_path(THINGSBOARD_PATH_TELEMETRY, request->path,
				   sizeof(request->path));
	if (err < 0) {
		thingsboard_request_free(request);
		return -EFAULT;
	}

	struct coap_client_request coap_request = {
		.payload = request->payload,
		.len = sz,
		.confirmable = true,
		.method = COAP_METHOD_POST,
		.fmt = THINGSBOARD_DEFAULT_CONTENT_FORMAT,
		.path = request->path,
		.cb = thingsboard_handle_response,
		.user_data = request,
	};

	err = coap_client_req(&thingsboard_client.coap_client, thingsboard_client.server_socket,
			      (struct sockaddr *)thingsboard_client.server_address, &coap_request,
			      NULL);
	if (err < 0) {
		LOG_ERR("Failed to send telemetry: %d", err);
		thingsboard_request_free(request);
	}

	return err;
}

static void thingsboard_handle_rpc_response(int16_t result_code, size_t offset,
					    const uint8_t *payload, size_t len, bool last_block,
					    void *user_data)
{
	struct thingsboard_request *request = user_data;

	if (result_code < 0) {
		LOG_ERR("Failed to send RPC request: %" PRId16, result_code);
		goto out;
	}

	uint8_t code = result_code;
	char code_str[5];
	char expected_code_str[5];

	if (result_code != COAP_RESPONSE_CODE_CONTENT) {
		coap_response_code_to_str(code, code_str);
		coap_response_code_to_str(COAP_RESPONSE_CODE_CONTENT, expected_code_str);
		LOG_ERR("Unexpected response code for rpc request: got %s, expected %s", code_str,
			expected_code_str);
		goto out;
	}

	if (request->rpc_cb) {
		request->rpc_cb(payload, len);
	}

out:
	if (last_block) {
		thingsboard_request_free(request);
	}
}

int thingsboard_send_rpc_request(thingsboard_rpc_request *r,
				 void (*rpc_cb)(const uint8_t *payload, size_t len))
{
	int err;

	__ASSERT_NO_MSG(r);

	struct thingsboard_request *request = thingsboard_request_alloc();
	if (request == NULL) {
		return -ENOMEM;
	}

	err = thingsboard_cat_path(THINGSBOARD_PATH_RPC, request->path, sizeof(request->path));
	if (err < 0) {
		thingsboard_request_free(request);
		return -EFAULT;
	}

	size_t request_len = ARRAY_SIZE(request->payload);
	err = thingsboard_rpc_request_encode(r, request->payload, &request_len);
	if (err < 0) {
		LOG_ERR("Failed to encode RPC request");
		thingsboard_request_free(request);
		return -EINVAL;
	}

	request->rpc_cb = rpc_cb;
	struct coap_client_request coap_request = {
		.payload = request->payload,
		.len = request_len,
		.confirmable = true,
		.method = COAP_METHOD_POST,
		.fmt = THINGSBOARD_DEFAULT_CONTENT_FORMAT,
		.path = request->path,
		.cb = thingsboard_handle_rpc_response,
		.user_data = request,
	};

	err = coap_client_req(&thingsboard_client.coap_client, thingsboard_client.server_socket,
			      (struct sockaddr *)thingsboard_client.server_address, &coap_request,
			      NULL);
	if (err < 0) {
		LOG_ERR("Failed to send RPC request: %d", err);
		thingsboard_request_free(request);
		return -EIO;
	}

	return 0;
}

void thingsboard_event(enum thingsboard_event event)
{
	if (thingsboard_client.config != NULL && thingsboard_client.config->callbacks.on_event) {
		thingsboard_client.config->callbacks.on_event(event);
	}
}

#ifdef CONFIG_THINGSBOARD_USE_PROVISIONING
static void prov_callback(const char *token)
{
	LOG_INF("Device provisioned");
	thingsboard_client.access_token = token;

	thingsboard_event(THINGSBOARD_EVENT_PROVISIONED);

	start_client();
}
#endif /* CONFIG_THINGSBOARD_USE_PROVISIONING */

static void start_client(void)
{
#ifndef CONFIG_THINGSBOARD_DTLS
	if (!thingsboard_client.access_token) {
#ifdef CONFIG_THINGSBOARD_USE_PROVISIONING
		LOG_INF("No access token in storage. Requesting provisioning.");

		int err = thingsboard_provision_device(thingsboard_client.config->device_name,
						       prov_callback);
		if (err < 0) {
			LOG_ERR("Could not provision device: %d", err);
			thingsboard_set_state(THINGSBOARD_STATE_DISCONNECTED);
			return;
		}

		return;
#else  /* CONFIG_THINGSBOARD_USE_PROVISIONING */
		thingsboard_client.access_token = CONFIG_THINGSBOARD_ACCESS_TOKEN;
#endif /* CONFIG_THINGSBOARD_USE_PROVISIONING */
	}
#endif /* CONFIG_THINGSBOARD_DTLS */

#ifdef CONFIG_THINGSBOARD_FOTA
	thingsboard_fota_init(&thingsboard_client.config->current_firmware);

	if (thingsboard_fota_confirm_update() != 0) {
		LOG_ERR("Failed to confirm FW update");
	}
#endif

	int err = thingsboard_client_subscribe_attributes();
	if (err < 0) {
		LOG_ERR("Failed to observe attributes: %d", err);
		thingsboard_socket_close(thingsboard_client.server_socket);
		thingsboard_set_state(THINGSBOARD_STATE_DISCONNECTED);
		return;
	}

#ifdef CONFIG_THINGSBOARD_TIME
	thingsboard_start_time_sync();
#endif /* CONFIG_THINGSBOARD_TIME */

	thingsboard_set_state(THINGSBOARD_STATE_CONNECTED);
}

static bool string_is_set(const char *str)
{
	return (str != NULL && strlen(str) > 0);
}

const thingsboard_attributes *thingsboard_get_attributes(void)
{
	return &thingsboard_client.shared_attributes;
}

int thingsboard_init(const struct thingsboard_configuration *configuration)
{
	int ret;

	if (thingsboard_client.state != THINGSBOARD_STATE_INIT) {
		return -EALREADY;
	}

	if (thingsboard_client.server_socket >= 0) {
		return -EALREADY;
	}

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

	k_mutex_init(&thingsboard_client.lock);

	thingsboard_client.config = configuration;

	ret = coap_client_init(&thingsboard_client.coap_client, "Thingsboard Client");
	if (ret != 0) {
		LOG_ERR("Failed to initialize CoAP client (%d)", ret);
		return ret;
	}

	thingsboard_client.state = THINGSBOARD_STATE_DISCONNECTED;

#ifdef CONFIG_THINGSBOARD_CONNECT_ON_INIT
	ret = thingsboard_connect();
	if (ret < 0) {
		LOG_ERR("Failed to connect to Thingsboard instance: %d", ret);
		return -ENOTCONN;
	}
#endif /* CONFIG_THINGSBOARD_CONNECT_ON_INIT */

	return 0;
}
