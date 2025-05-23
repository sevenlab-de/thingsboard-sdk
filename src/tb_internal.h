#ifndef _TB_INTERNAL_H_
#define _TB_INTERNAL_H_

#include <zephyr/net/coap_client.h>
#include <zephyr/net/socket.h>

#include "thingsboard.h"

#define THINGSBOARD_PATH_BASE(...)   ((const char *[]){__VA_ARGS__, NULL})
#define THINGSBOARD_PATH_API_V1(...) THINGSBOARD_PATH_BASE("api", "v1", __VA_ARGS__)

#ifdef CONFIG_THINGSBOARD_DTLS

/* DTLS does not embed the access token into the path */
#define THINGSBOARD_PATH(...)       THINGSBOARD_PATH_API_V1(__VA_ARGS__)
#define THINGSBOARD_SHORT_PATH(...) THINGSBOARD_PATH_BASE(__VA_ARGS__)

#else /* CONFIG_THINGSBOARD_DTLS */

/* Without DTLS, the access token needs to be embedded in the path */
#define THINGSBOARD_PATH(...) THINGSBOARD_PATH_API_V1(thingsboard_client.access_token, __VA_ARGS__)
#define THINGSBOARD_SHORT_PATH(...)                                                                \
	THINGSBOARD_PATH_BASE(__VA_ARGS__, thingsboard_client.access_token)

#endif /* CONFIG_THINGSBOARD_DTLS */

/**
 * CoAP path specifications
 */

#define THINGSBOARD_PATH_ATTRIBUTES THINGSBOARD_PATH("attributes")
#define THINGSBOARD_PATH_TELEMETRY  THINGSBOARD_PATH("telemetry")
#define THINGSBOARD_PATH_RPC        THINGSBOARD_PATH("rpc")
#define THINGSBOARD_PATH_FIRMWARE   THINGSBOARD_SHORT_PATH("fw")

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

struct thingsboard_request {
	struct coap_client_request coap_request;
	void (*rpc_cb)(const uint8_t *payload, size_t len);
	struct coap_client_option options[1];
	char path[CONFIG_THINGSBOARD_REQUEST_MAX_PATH_LENGTH];
	char payload[CONFIG_COAP_CLIENT_MESSAGE_SIZE];
};

enum thingsboard_state {
	THINGSBOARD_STATE_INIT,
	THINGSBOARD_STATE_CONNECTING,
	THINGSBOARD_STATE_CONNECTED,
	THINGSBOARD_STATE_SUSPENDED,
	THINGSBOARD_STATE_DISCONNECTED,
};

struct thingsboard_client {
	enum thingsboard_state state;

	const struct thingsboard_configuration *config;

	struct coap_client coap_client;

	struct sockaddr_storage *server_address;
	size_t server_address_len;
	int server_socket;

	struct thingsboard_request *attributes_observation;

#ifndef CONFIG_THINGSBOARD_DTLS
	const char *access_token;
#endif /* CONFIG_THINGSBOARD_DTLS */

	struct k_mutex lock;
	thingsboard_attributes shared_attributes;

#ifdef CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON
	struct thingsboard_attributes_buffer shared_attributes_buffer;
#endif /* CONFIG_THINGSBOARD_CONTENT_FORMAT_JSON */
};

extern struct thingsboard_client thingsboard_client;

/**
 * Concatenate path elements into one string buffer, as required by Zephyrs CoAP client.
 *
 * @param in NULL terminated array of NULL terminated path elements
 * @param out Pointer to buffer where path will be written to
 * @param out_len Length of buffer in `out`
 * @return 0 on success, -ENOMEM path didn't into  `out`
 */
int thingsboard_cat_path(const char *in[], char *out, size_t out_len);

/**
 * Allocate a `struct thingsboard_request` from Thingsbaord SDKs internal slab storage.
 *
 * @return Pointer to allocated `struct thingsboard_request` or NULL, when no slab was free.
 */
struct thingsboard_request *thingsboard_request_alloc(void);

/**
 * Free slab, previous allocated with `thingsboard_request_alloc()`.
 *
 * @param request Pointer to `struct thingsboard_request` to be freed
 */
void thingsboard_request_free(struct thingsboard_request *request);

/**
 * Send RPC client to server request to Thingsboard Instance.
 *
 * @param r RPC request to be sent
 * @param rpc_cb Callback called after successful execution of RPC
 * @return 0 on success, negative on error
 */
int thingsboard_send_rpc_request(thingsboard_rpc_request *r,
				 void (*rpc_cb)(const uint8_t *payload, size_t len));

#ifdef CONFIG_THINGSBOARD_FOTA
/**
 * Should be called as soon as CoAP connectivity works.
 * This confirms the image in MCUboot and sends the current
 * version as given by struct tb_fw_id (on init) to Thingsboard,
 * if the image has not already been confirmed.
 *
 * @return 0 on success, negative on error
 */
int thingsboard_fota_confirm_update(void);

/**
 * Call this function when attributes have been received to check
 * for the relevant FOTA attributes. If all data is available,
 * it also attempts to start an update.
 */
void thingsboard_fota_on_attributes(void);

/**
 * Initialize the FOTA system. The system only stores the pointers internally
 * and does not copy the memory, so changing the pointed-to memory later is
 * an error and undefined behavior may happen.
 *
 * @param current_fw Information about the currently installed firmware
 */
void thingsboard_fota_init(const struct thingsboard_firmware_info *current_fw);

#endif /* CONFIG_THINGSBOARD_FOTA */

#ifdef CONFIG_THINGSBOARD_USE_PROVISIONING
/**
 * Callback that will be called with the token as soon as
 * provisioning has been completed successfully.
 *
 * @param token Access Token to be used by this device.
 */
typedef void (*thingsboard_provisiong_callback)(const char *token);

/**
 * Provision the device.
 *
 * Requests access token or reads it from settings.
 *
 * @param device_name Name of this device.
 * @param cb Callback to be called on successful provisioning
 */
int thingsboard_provision_device(const char *device_name, thingsboard_provisiong_callback cb);
#endif /* CONFIG_THINGSBOARD_USE_PROVISIONING */

/**
 * Resolve Thingsboard instances hostname and create appropriate `sockaddr_t`
 * structure.
 *
 * @param hostname Hostname or IP address as string to be resolved
 * @param port UDP port to be written into `sockaddr_t` structure
 * @param server Pointer to `sockaddr_storage` to put resolved address into.
 *
 * @return 0 on success, negative on error
 */
int thingsboard_server_resolve(const char *hostname, uint16_t port,
			       struct sockaddr_storage *server);

/**
 * Create and configure socket for connection to Thingsboard instance.
 *
 * Will return a `sockaddr_t` structure via the `server_address` parameter,
 * for use with `sendto()`, when needed.
 *
 * @param config Hostname, port and security configuration to be used for
 *               connecting to Thingboard instance.
 * @param server_address Pointer to place resolved `sockaddr_t` into, for use
 *                       `sendto()`.
 * @param server_address_len Returns size of `sockaddr_t` structure this function
 *                           put into `server_address`
 *
 * @return positive socket number on success, negative on error
 */
int thingsboard_socket_connect(const struct thingsboard_configuration *config,
			       struct sockaddr_storage **server_address,
			       size_t *server_address_len);

/**
 * Suspend socket.
 *
 * Action is determined by the THINGSBOARD_SOCKET_SUSPEND Kconfig option.
 *
 * When setting THINGSBOARD_SOCKET_SUSPEND_NONE, will be defined as weak symbol.
 *
 * @param sock Pointer to current socket. Might be overwritten. Will be set to -1 when it has been
 * closed.
 *
 * @return 0 in success, negative on error
 */
int thingsboard_socket_suspend(int *sock);

/**
 * Resume socket.
 *
 * Action is determined by the THINGSBOARD_SOCKET_SUSPEND Kconfig option.
 *
 * When setting THINGSBOARD_SOCKET_SUSPEND_NONE, will be defined as weak symbol.
 *
 * @param sock Pointer to current socket. Might be overwritten. Will be set to -1 when it has been
 * closed.
 *
 * @return 0 in success, negative on error
 */
int thingsboard_socket_resume(int *sock);

/**
 * Close socket.
 *
 * @param sock Socket to close
 */
void thingsboard_socket_close(int sock);

/**
 * Check for Thingsboard being connected.
 *
 * @retval true when the Thingsboard client is initialized, connected and ready to do send requests.
 * @retval false when the Thingsboard client should not try to send data.
 */
bool thingsboard_is_active(void);

/**
 * Send event to application.
 *
 * @param event Event to be sent to application.
 */
void thingsboard_event(enum thingsboard_event event);

#ifdef CONFIG_THINGSBOARD_TIME
/**
 * Start time synchronization.
 */
void thingsboard_start_time_sync(void);

/**
 * Stop time synchronization.
 */
void thingsboard_stop_time_sync(void);

#endif /* CONFIG_THINGSBOARD_TIME */

/**
 * Subscribe(observe) attributes notification.
 *
 * Can only be done, when not already subscribed.
 *
 * @return 0 on success, negative on error
 */
int thingsboard_client_subscribe_attributes(void);

/**
 * Unsubscribe attributes notifications.
 *
 * @retval 0 success
 * @retval -EALREADY not subscribed to attributes notification
 */
int thingsboard_client_unsubscribe_attributes(void);

ssize_t thingsboard_attributes_update(thingsboard_attributes *changes,
				      thingsboard_attributes *current, void *buffer,
				      size_t buffer_len);

/**
 * Decode Protobuf or JSON payload to `thingsboard_attributes`.
 *
 * Might reference parts of `buffer` in the decoded structure.
 *
 * @param buffer Buffer from which to read data to be decoded.
 * @param len Length of data given in `buffer`
 * @param v Pointer to `thingsboard_attributes` object, where decoded values
 *          will be placed into
 *
 * @return 0 on success, negative on error
 */
int thingsboard_attributes_decode(const char *buffer, size_t len, thingsboard_attributes *v);

/**
 * Decode Protobuf or JSON payload to `thingsboard_rpc_response`.
 *
 * Does not decode the RPC response payload. The RPC response payload will be
 * a string containing a JSON object.
 *
 * Might reference parts of `buffer` in the decoded structure.
 *
 * @param buffer Buffer from which to read data to be decoded.
 * @param len Length of data given in `buffer`
 * @param rr Pointer to `thingsboard_rpc_responce` object, where decoded values
 *           will be placed into
 *
 * @return 0 on success, negative on error
 */
int thingsboard_rpc_response_decode(const char *buffer, size_t len, thingsboard_rpc_response *rr);

/**
 * Encode `thingsboard_rpc_request` as Protobuf or JSON payload.
 *
 * @param rq Pointer to `thingsboard_rpc_request` object to be serialized
 * @param buffer Byte array where to serialize `rq` to
 * @param len Pointer to `size_t` with length of `buffer`. The actual amount of
 *            bytes used to encode `rq` will be written there.
 *
 * @return 0 on success, negative on error
 */
int thingsboard_rpc_request_encode(const thingsboard_rpc_request *rq, char *buffer, size_t *len);

/**
 * Encode `thingsboard_telemetry` as Protobuf or JSON payload.
 *
 * @param v Pointer to `thingsboard_telemetry` object to be serialized
 * @param buffer Byte array where to serialize `v` to
 * @param len Pointer to `size_t` with length of `buffer`. The actual amount of
 *            bytes used to encode `v` will be written there.
 *
 * @return 0 on success, negative on error
 */
int thingsboard_telemetry_encode(const thingsboard_telemetry *v, char *buffer, size_t *len);

/**
 * Encode `thingsboard_timeseries` as Prot
 *
 * The timestamps provided are used "as is" and need to be accurate.obuf or JSON payload.
 *
 * @param ts Pointer to `thingsboard_timeseries` object to be serialized
 * @param ts_count Pointer to amount of `thingsboard_timeseries` object in `ts`.
 *                 The amount of `thingsboard_timeseries` objects, which could
 *                 be fitted into `buffer` will be written there.
 * @param buffer Byte array where to serialize `v` to
 * @param len Pointer to `size_t` with length of `buffer`. The actual amount of
 *            bytes used to encode `v` will be written there.
 *
 * @return 0 on success, negative on error
 */
int thingsboard_timeseries_encode(const thingsboard_timeseries *ts, size_t *ts_count, char *buffer,
				  size_t *len);

#endif /* _TB_INTERNAL_H_ */
