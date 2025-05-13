#ifndef _TB_INTERNAL_H_
#define _TB_INTERNAL_H_

#include <zephyr/net/socket.h>

#include "thingsboard.h"

extern const char *thingsboard_access_token;

#ifdef CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON
#include <thingsboard_rpc_request_serde.h>

typedef struct thingsboard_rpc_request thingsboard_rpc_request;

typedef struct {
	bool has_payload;
	const char *payload;
} thingsboard_rpc_response;

#define THINGSBOARD_DEFAULT_CONTENT_FORMAT COAP_CONTENT_FORMAT_APP_JSON

#else /* CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */

#define THINGSBOARD_DEFAULT_CONTENT_FORMAT COAP_CONTENT_FORMAT_APP_OCTET_STREAM

#endif /* CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */

#ifdef CONFIG_THINGSBOARD_FOTA
/**
 * Should be called as soon as CoAP connectivity works.
 * This confirms the image in MCUboot and sends the current
 * version as given by struct tb_fw_id (on init) to Thingsboard,
 * if the image has not already been confirmed.
 */
int thingsboard_fota_confirm_update(void);

/**
 * Call this function when attributes have been received to check
 * for the relevant FOTA attributes. If all data is available,
 * it also attempts to start an update.
 */
void thingsboard_fota_on_attributes(thingsboard_attributes *attr);

/**
 * Initialize the FOTA system. The system only stores the pointers internally
 * and does not copy the memory, so changing the pointed-to memory later is
 * an error and undefined behavior may happen.
 */
void thingsboard_fota_init(const struct thingsboard_firmware_info *current_fw);

#endif /* CONFIG_THINGSBOARD_FOTA */

int thingsboard_server_resolve(const char *hostname, uint16_t port,
			       struct sockaddr_storage *server);

int thingsboard_socket_connect(const struct thingsboard_configuration *config,
			       struct sockaddr_storage **server_address,
			       size_t *server_address_len);

void thingsboard_event(enum thingsboard_event event);

#ifdef CONFIG_THINGSBOARD_TIME
void thingsboard_start_time_sync(void);
#endif /* CONFIG_THINGSBOARD_TIME */

int thingsboard_attributes_decode(const char *buffer, size_t len, thingsboard_attributes *v);
int thingsboard_rpc_response_decode(const char *buffer, size_t len, thingsboard_rpc_response *rr);

int thingsboard_rpc_request_encode(const thingsboard_rpc_request *rq, char *buffer, size_t *len);
int thingsboard_telemetry_encode(const thingsboard_telemetry *v, char *buffer, size_t *len);
int thingsboard_timeseries_encode(const thingsboard_timeseries *ts, size_t *ts_count, char *buffer,
				  size_t *len);

#endif /* _TB_INTERNAL_H_ */
