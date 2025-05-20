#include <stddef.h>

#include <zephyr/net/tls_credentials.h>
#include <zephyr/logging/log.h>

#include "tb_internal.h"

LOG_MODULE_DECLARE(thingsboard_client, CONFIG_THINGSBOARD_LOG_LEVEL);

int thingsboard_socket_connect(const struct thingsboard_configuration *config,
			       struct sockaddr_storage **server_address, size_t *server_address_len)
{
	int err;
	struct sockaddr_storage thingsboard_server_address;

	LOG_INF("Connecting to \"%s\" on port %" PRIu16 " using DTLS", config->server_hostname,
		config->server_port);

	err = thingsboard_server_resolve(config->server_hostname, config->server_port,
					 &thingsboard_server_address);
	if (err < 0) {
		LOG_ERR("Failed to resolve hostname: %d", err);
		return -ENONET;
	}

	struct sockaddr_in src = {0};

	src.sin_family = AF_INET;
	src.sin_addr.s_addr = INADDR_ANY;
	src.sin_port = htons(0);

	int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_DTLS_1_2);
	if (sock < 0) {
		LOG_ERR("Failed to create CoAP socket: %d", errno);
		return -ENONET;
	}

	err = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, config->security.tags,
			       config->security.tags_size);
	if (err < 0) {
		LOG_ERR("Failed to configure DTlS credentials: %d", err);
		thingsboard_socket_close(sock);
		return -EPERM;
	}

	err = zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, config->server_hostname,
			       strlen(config->server_hostname) + 1);
	if (err < 0) {
		LOG_ERR("Failed configure DTLS hostname: %d", err);
		thingsboard_socket_close(sock);
		return -EPERM;
	}

	int cid_status = TLS_DTLS_CID_ENABLED;
	err = zsock_setsockopt(sock, SOL_TLS, TLS_DTLS_CID, &cid_status, sizeof(cid_status));
	if (err < 0) {
		LOG_ERR("Failed enabled CID: %d", err);
		thingsboard_socket_close(sock);
		return -EPERM;
	}

	int keep_open = 1;
	err = zsock_setsockopt(sock, SOL_SOCKET, SO_KEEPOPEN, &keep_open, sizeof(keep_open));
	if (err < 0) {
		LOG_ERR("Failed configure SO_KEEPOPEN %d", err);
		thingsboard_socket_close(sock);
		return -EPERM;
	}

	err = zsock_connect(sock, (struct sockaddr *)&thingsboard_server_address,
			    sizeof(thingsboard_server_address));
	if (err < 0) {
		LOG_ERR("connect failed: %d", errno);
		thingsboard_socket_close(sock);
		return -ENONET;
	}

#if CONFIG_THINGSBOARD_LOG_LEVEL >= LOG_LEVEL_DBG
	socklen_t optlen = sizeof(cid_status);
	err = zsock_getsockopt(sock, SOL_TLS, TLS_DTLS_CID_STATUS, &cid_status, &optlen);
	if (err < 0 || optlen != sizeof(cid_status)) {
		LOG_ERR("Failed read CID status %d", err);
	} else {
		switch (cid_status) {
		case TLS_DTLS_CID_STATUS_DISABLED:
			LOG_DBG("CID status is DISABLED");
			break;
		case TLS_DTLS_CID_STATUS_DOWNLINK:
			LOG_DBG("CID status is DOWNLINK");
			break;
		case TLS_DTLS_CID_STATUS_UPLINK:
			LOG_DBG("CID status is UPLINK");
			break;
		case TLS_DTLS_CID_STATUS_BIDIRECTIONAL:
			LOG_DBG("CID status is BIDIRECTIONAL");
			break;
		}
	}
#endif

	/* We used `connect()`, so this is a connected socket.
	 * The coap client does not need to now the server address
	 */
	if (server_address != NULL) {
		*server_address = NULL;
	}

	if (server_address_len != NULL) {
		*server_address_len = 0;
	}

	return sock;
}
