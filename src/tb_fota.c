#include <stdio.h>

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <dfu/dfu_target_mcuboot.h>

#include "coap_client.h"
#include "tb_internal.h"

LOG_MODULE_REGISTER(tb_fota, CONFIG_THINGSBOARD_LOG_LEVEL);

static const struct thingsboard_firmware_info *current_firmware;

BUILD_ASSERT((CONFIG_THINGSBOARD_FOTA_CHUNK_SIZE + 100 < CONFIG_COAP_CLIENT_MSG_LEN),
	     "CoAP messages too small");

static struct {
	char title[CONFIG_THINGSBOARD_MAX_STRINGS_LENGTH];
	char version[CONFIG_THINGSBOARD_MAX_STRINGS_LENGTH];
	size_t offset;
	size_t size;
	uint8_t dfu_buf[1024];

	bool title_set;
	bool version_set;
	bool size_set;
} tb_fota_ctx;

static inline unsigned int fw_next_chunk(void)
{
	return tb_fota_ctx.offset / CONFIG_THINGSBOARD_FOTA_CHUNK_SIZE;
}

static inline unsigned int fw_num_chunks(void)
{
	return DIV_ROUND_UP(tb_fota_ctx.size, CONFIG_THINGSBOARD_FOTA_CHUNK_SIZE);
}

static inline size_t fw_next_chunk_size(void)
{
	size_t rem = tb_fota_ctx.size - tb_fota_ctx.offset;
	return MIN(rem, CONFIG_THINGSBOARD_FOTA_CHUNK_SIZE);
}

enum thingsboard_fw_state {
	TB_FW_DOWNLOADING,
	TB_FW_DOWNLOADED,
	TB_FW_VERIFIED,
	TB_FW_UPDATING,
	TB_FW_UPDATED,
	TB_FW_FAILED,
	TB_FW_NUM_STATES,
};

#define STATE(s)                                                                                   \
	case TB_FW_##s:                                                                            \
		return #s
static const char *state_str(enum thingsboard_fw_state state)
{
	switch (state) {
		STATE(DOWNLOADING);
		STATE(DOWNLOADED);
		STATE(VERIFIED);
		STATE(UPDATING);
		STATE(UPDATED);
		STATE(FAILED);
		STATE(NUM_STATES);
	}

	return "INVALID STATE";
}
#undef STATE

static int client_set_fw_state(enum thingsboard_fw_state state)
{
	static enum thingsboard_fw_state current_state = TB_FW_NUM_STATES;

	if (current_state == state) {
		return 0;
	}
	current_state = state;

	thingsboard_telemetry telemetry = {
		.has_fw_state = true,
	};
#ifdef CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON
	telemetry.fw_state = state_str(state);
#else  /*  CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */
	__ASSERT_NO_MSG(strlen(state_str(state)) < ARRAY_SIZE(telemetry.fw_state));
	strncpy(telemetry.fw_state, state_str(state), ARRAY_SIZE(telemetry.fw_state));
#endif /* CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */

	return thingsboard_send_telemetry(&telemetry);
}

static int client_fw_get_next_chunk(void);

static int fw_apply(void)
{
	int err;

	err = dfu_target_mcuboot_done(true);
	if (err < 0) {
		return err;
	}

	k_sleep(K_SECONDS(5));
	client_set_fw_state(TB_FW_UPDATING);
	k_sleep(K_SECONDS(5));
	dfu_target_mcuboot_schedule_update(0);
	sys_reboot(SYS_REBOOT_COLD);

	return 0;
}

static enum thingsboard_fw_state fw_chunk_process(const void *buf, size_t size)
{
	int err;

	if (size != fw_next_chunk_size()) {
		LOG_ERR("Wrong msg size");
		return TB_FW_FAILED;
	}

	if (fw_next_chunk() == 0) {
		// First chunk, check if data is valid
		if (!dfu_target_mcuboot_identify(buf)) {
			LOG_ERR("Data received is not a valid MCUBoot package, abort");
			tb_fota_ctx.size = 0;
			return TB_FW_FAILED;
		}
	}

	err = dfu_target_mcuboot_write(buf, size);
	if (err) {
		LOG_ERR("Could not write update chunk");
		return TB_FW_FAILED;
	}

	tb_fota_ctx.offset += size;
	if (tb_fota_ctx.offset < tb_fota_ctx.size) {
		return TB_FW_DOWNLOADING;
	}

	return TB_FW_DOWNLOADED;
}

static int client_handle_fw_chunk(struct coap_client_request *req, struct coap_packet *response)
{
	const uint8_t *payload;
	uint16_t payload_len;
	enum thingsboard_fw_state state;

	int err;

	payload = coap_packet_get_payload(response, &payload_len);
	if (!payload_len) {
		LOG_WRN("Received empty response");
		return -ENOMSG;
	}

	state = fw_chunk_process(payload, payload_len);
	err = client_set_fw_state(state);
	if (err) {
		LOG_ERR("Failed to report state");
	}

	switch (state) {
	case TB_FW_DOWNLOADING:
		return client_fw_get_next_chunk();
	case TB_FW_DOWNLOADED:
		return fw_apply();
	case TB_FW_FAILED:
		return -1;
	default:
		return 0;
	}
}

static int client_fw_get_next_chunk(void)
{
	int err;
	unsigned int chunk = fw_next_chunk();
	struct coap_client_request *request;

	LOG_DBG("Requesting chunk %u of %u", chunk, fw_num_chunks());

	request = coap_client_request_alloc(COAP_TYPE_CON, COAP_METHOD_GET);
	if (!request) {
		return -ENOMEM;
	}

	err = coap_packet_append_uri_path(&request->pkt, THINGSBOARD_PATH_FIRMWARE);
	if (err < 0) {
		LOG_ERR("Failed to encode uri path, %d", err);
		return err;
	}

	err = coap_packet_append_uri_query_s(&request->pkt, "title=%s", tb_fota_ctx.title);
	if (err) {
		return err;
	}
	err = coap_packet_append_uri_query_s(&request->pkt, "version=%s", tb_fota_ctx.version);
	if (err) {
		return err;
	}
	err = coap_packet_append_uri_query_d(&request->pkt, "chunk=%d", (int)chunk);
	if (err) {
		return err;
	}
	err = coap_packet_append_uri_query_d(&request->pkt, "size=%d",
					     CONFIG_THINGSBOARD_FOTA_CHUNK_SIZE);
	if (err) {
		return err;
	}

	err = coap_client_send(request, client_handle_fw_chunk);
	if (err < 0) {
		LOG_ERR("Failed to send CoAP request, %d", errno);
		return -errno;
	}

	return 0;
}

int thingsboard_fota_confirm_update(void)
{
	int err;

	__ASSERT_NO_MSG(current_firmware != NULL);

	// Check if we booted this image the first time
	if (boot_is_img_confirmed()) {
		// Nothing to do
		return 0;
	}

	LOG_INF("Confirming FW update");

	err = boot_write_img_confirmed();
	if (err) {
		LOG_WRN("Confirming image failed");
	}

	thingsboard_telemetry telemetry = {
		.fw_state = "UPDATED",
		.has_fw_state = true,
		.has_current_fw_title = true,
		.has_current_fw_version = true,
	};

#ifdef CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON
	telemetry.current_fw_title = current_fw->fw_title;
	telemetry.current_fw_version = current_fw->fw_version;
	;
#else  /* CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */
	strncpy(telemetry.current_fw_title, current_firmware->title,
		ARRAY_SIZE(telemetry.current_fw_title));
	if (strlen(current_firmware->title) >= ARRAY_SIZE(telemetry.current_fw_title)) {
		telemetry.current_fw_title[ARRAY_SIZE(telemetry.current_fw_title) - 1] = 0;
		LOG_WRN("current firmware title has been truncated");
	}

	strncpy(telemetry.current_fw_version, current_firmware->version,
		ARRAY_SIZE(telemetry.current_fw_version));
	if (strlen(current_firmware->version) >= ARRAY_SIZE(telemetry.current_fw_version)) {
		telemetry.current_fw_version[ARRAY_SIZE(telemetry.current_fw_version) - 1] = 0;
		LOG_WRN("current firmware version has been truncated");
	}
#endif /* CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */

	return thingsboard_send_telemetry(&telemetry);
}

static void thingsboard_start_fw_update(void)
{
	int err;

	if (!tb_fota_ctx.size) {
		LOG_ERR("No FW set");
	}

	if (!strcmp(tb_fota_ctx.title, current_firmware->title) &&
	    !strcmp(tb_fota_ctx.version, current_firmware->version)) {
		LOG_INF("Skipping FW update, requested FW already installed");
		return;
	}

	LOG_INF("Starting FW update: %s - %s (%zu B)", tb_fota_ctx.title, tb_fota_ctx.version,
		tb_fota_ctx.size);

	err = client_set_fw_state(TB_FW_DOWNLOADING);
	if (err < 0) {
		LOG_WRN("Could not send fw state");
		// Ignore error, not mission critical
	}

	if (tb_fota_ctx.offset) {
		/* FOTA already running */
		dfu_target_mcuboot_done(false);
	} else {
		err = dfu_target_mcuboot_set_buf(tb_fota_ctx.dfu_buf, sizeof(tb_fota_ctx.dfu_buf));
		if (err < 0) {
			LOG_ERR("Failed: dfu_target_mcuboot_set_buf");
			return;
		}
	}

	// Callback argument is not used by DFU-MCUboot
	err = dfu_target_mcuboot_init(tb_fota_ctx.size, 0, NULL);
	if (err < 0) {
		LOG_ERR("Failed: dfu_target_mcuboot_init");
		return;
	}

	// Could be non-zero if CONFIG_DFU_TARGET_STREAM_SAVE_PROGRESS is enabled
	err = dfu_target_mcuboot_offset_get(&tb_fota_ctx.offset);
	if (err < 0) {
		LOG_ERR("Failed: dfu_target_mcuboot_offset_get");
		return;
	}

	(void)client_fw_get_next_chunk();
}

void thingsboard_fota_init(const struct thingsboard_firmware_info *current_fw)
{
	__ASSERT_NO_MSG(current_fw != NULL);
	__ASSERT_NO_MSG(current_fw->title != NULL && strlen(current_fw->title) > 0);
	__ASSERT_NO_MSG(current_fw->version != NULL && strlen(current_fw->version) > 0);

	current_firmware = current_fw;

	thingsboard_telemetry telemetry = {
		.has_current_fw_title = true,
		.has_current_fw_version = true,
	};

#ifdef CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON
	telemetry.current_fw_title = current_firmware->fw_title;
	telemetry.current_fw_version = current_firmware->fw_version;
#else  /* CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */
	strncpy(telemetry.current_fw_title, current_fw->title,
		ARRAY_SIZE(telemetry.current_fw_title));
	if (strlen(current_firmware->title) >= ARRAY_SIZE(telemetry.current_fw_title)) {
		telemetry.current_fw_title[ARRAY_SIZE(telemetry.current_fw_title) - 1] = 0;
		LOG_WRN("current firmware title has been truncated");
	}

	strncpy(telemetry.current_fw_version, current_firmware->version,
		ARRAY_SIZE(telemetry.current_fw_version));
	if (strlen(current_firmware->version) >= ARRAY_SIZE(telemetry.current_fw_version)) {
		telemetry.current_fw_version[ARRAY_SIZE(telemetry.current_fw_version) - 1] = 0;
		LOG_WRN("current firmware version has been truncated");
	}
#endif /* CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */

	thingsboard_send_telemetry(&telemetry);
}

void thingsboard_fota_on_attributes(thingsboard_attributes *attr)
{
	__ASSERT_NO_MSG(current_firmware != NULL);
	__ASSERT_NO_MSG(attr != NULL);

	if (attr->has_fw_title) {
		if (strlen(attr->fw_title) >= sizeof(tb_fota_ctx.title)) {
			LOG_WRN("`fw_title` too long");
			tb_fota_ctx.title_set = false;
		} else {
			strncpy(tb_fota_ctx.title, attr->fw_title, sizeof(tb_fota_ctx.title));
			tb_fota_ctx.title_set = true;
		}
	}

	if (attr->has_fw_version) {
		if (strlen(attr->fw_version) >= sizeof(tb_fota_ctx.version)) {
			LOG_WRN("`fw_version` too long");
			tb_fota_ctx.version_set = false;
		} else {
			strncpy(tb_fota_ctx.version, attr->fw_version, sizeof(tb_fota_ctx.version));
			tb_fota_ctx.version_set = true;
		}
	}

	if (attr->has_fw_size) {
		tb_fota_ctx.size = attr->fw_size;
		tb_fota_ctx.size_set = true;
	}

	if (!tb_fota_ctx.title_set || !tb_fota_ctx.version_set || !tb_fota_ctx.size_set) {
		/* Not enough information to check for available firmware update */
		return;
	}

	if (!strcmp(tb_fota_ctx.title, current_firmware->title) &&
	    !strcmp(tb_fota_ctx.version, current_firmware->version)) {
		/* Already installed firmware matches new firmware, nothing to do */
		return;
	}

	thingsboard_start_fw_update();
}
