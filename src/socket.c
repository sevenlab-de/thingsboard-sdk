#include <zephyr/logging/log.h>

#include "tb_internal.h"

LOG_MODULE_DECLARE(thingsboard_client, CONFIG_THINGSBOARD_LOG_LEVEL);

static struct sockaddr_storage thingsboard_server_address;

int thingsboard_server_resolve(const char *hostname, uint16_t port, struct sockaddr_storage *server)
{
	int err;
	struct zsock_addrinfo *results = NULL;
	struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};

	__ASSERT_NO_MSG(server != NULL);

	err = zsock_getaddrinfo(hostname, NULL, &hints, &results);
	if (err != 0) {
		LOG_ERR("getaddrinfo failed, error %d: (%s)", err, zsock_gai_strerror(err));
		return -EIO;
	}

	if (results == NULL) {
		LOG_ERR("server address not found");
		return -ENOENT;
	}

	/* IPv4 Address. */
	struct sockaddr_in *server4 = ((struct sockaddr_in *)server);

	server4->sin_addr.s_addr = ((struct sockaddr_in *)results->ai_addr)->sin_addr.s_addr;
	server4->sin_family = AF_INET;
	server4->sin_port = htons(port);

#if CONFIG_THINGSBOARD_LOG_LEVEL >= LOG_LEVEL_DBG
	char ipv4_addr[NET_IPV4_ADDR_LEN] = {0};
	char *addr =
		zsock_inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr, sizeof(ipv4_addr));
	if (addr != NULL) {
		LOG_DBG("IPv4 Address found %s, using port %" PRIu16, addr, port);
	} else {
		LOG_ERR("Failed to show IP address: %d", errno);
	}
#endif

	/* Free the address. */
	zsock_freeaddrinfo(results);

	return 0;
}

static int thingsboard_socket_connect_internal(void);

__weak int thingsboard_socket_connect(const struct thingsboard_configuration *config,
				      struct sockaddr_storage **server_address,
				      size_t *server_address_len)
{
	int err;

	err = thingsboard_server_resolve(config->server_hostname, config->server_port,
					 &thingsboard_server_address);
	if (err < 0) {
		LOG_ERR("Failed to resolve hostname: %d", err);
		return -ENONET;
	}

	int sock = thingsboard_socket_connect_internal();

	if (server_address != NULL) {
		*server_address = &thingsboard_server_address;
	}
	if (server_address_len != NULL) {
		*server_address_len = sizeof(thingsboard_server_address);
	}

	return sock;
}

static int thingsboard_socket_connect_internal(void)
{
	struct sockaddr_in src = {0};

	src.sin_family = AF_INET;
	src.sin_addr.s_addr = INADDR_ANY;
	src.sin_port = htons(0);

	int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create CoAP socket: %d", errno);
		return -ENONET;
	}

	/*
	 * Do not use connect!
	 * For some networking reason, the source address of the responses
	 * might not match the server address, in which case a connected
	 * socket can just drop the messages.
	 */
	int err = zsock_bind(sock, (struct sockaddr *)&src, sizeof(src));
	if (err < 0) {
		LOG_ERR("bind failed: %d", errno);
		thingsboard_socket_close(sock);
		return -ENONET;
	}

	return sock;
}

__weak void thingsboard_socket_close(int sock)
{
	if (sock == -1) {
		LOG_DBG("Tried to close socket -1, socket is probably not open!");
		return;
	}

	int err = zsock_close(sock);
	if (err < 0) {
		LOG_ERR("Failed to close socket '%d': %d", sock, errno);
	}
}

#ifdef CONFIG_THINGSBOARD_SOCKET_SUSPEND_NONE

__weak int thingsboard_socket_suspend(int *sock)
{
	(void)sock;

	return 0;
}

__weak int thingsboard_socket_resume(int *sock)
{
	(void)sock;

	return 0;
}

#elif defined CONFIG_THINGSBOARD_SOCKET_SUSPEND_DISCONNECT

int thingsboard_socket_suspend(int *sock)
{
	__ASSERT_NO_MSG(sock != NULL);

	int err = thingsboard_client_unsubscribe_attributes();
	if (err == -EALREADY) {
		LOG_DBG("Was not subscribed to attributes notification");
	}

	thingsboard_socket_close(*sock);
	*sock = -1;

	return 0;
}

int thingsboard_socket_resume(int *sock)
{
	__ASSERT_NO_MSG(sock != NULL);

	int ret = thingsboard_socket_connect(thingsboard_client.config,
					     &thingsboard_client.server_address,
					     &thingsboard_client.server_address_len);
	if (ret < 0) {
		LOG_ERR("Failed to reconnect socket: %d", ret);
		return -ECONNREFUSED;
	}

	*sock = ret;

	ret = thingsboard_client_subscribe_attributes();
	if (ret < 0) {
		LOG_ERR("Failed to observe attributes: %d", ret);
		thingsboard_socket_close(*sock);
		*sock = -1;
		return -EBADMSG;
	}

	return 0;
}

#elif defined CONFIG_THINGSBOARD_SOCKET_SUSPEND_RAI

int thingsboard_socket_suspend(int *sock)
{
	int option = RAI_NO_DATA;
	int err = zsock_setsockopt(*sock, SOL_SOCKET, SO_RAI, &option, sizeof(option));
	if (err < 0) {
		LOG_WRN("Failed to set RAI_NO_DATA: %d", err);
	}
	return 0;
}

int thingsboard_socket_resume(int *sock)
{
	int option = RAI_ONGOING;
	int err = zsock_setsockopt(*sock, SOL_SOCKET, SO_RAI, &option, sizeof(option));
	if (err < 0) {
		LOG_WRN("Failed to set RAI_NO_DATA: %d", err);
	}
	/* Nothing to do, as `thingsboard_socket_suspend()` didn't do anything */
	return 0;
}

#endif /* CONFIG_THINGSBOARD_SOCKET_SUSPEND_NONE */
