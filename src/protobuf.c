#include <zephyr/logging/log.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "tb_internal.h"

LOG_MODULE_REGISTER(thingsboard_protobuf, CONFIG_THINGSBOARD_LOG_LEVEL);

int thingsboard_attributes_decode(const char *buffer, size_t len, thingsboard_attributes *v)
{
	*v = (thingsboard_attributes)thingsboard_attributes_init_zero;

	pb_istream_t stream = pb_istream_from_buffer(buffer, len);

	bool success = pb_decode(&stream, thingsboard_attributes_fields, v);
	if (!success) {
		LOG_WRN("Failed to decode `thingsboard_attributes`: %s", PB_GET_ERROR(&stream));
		return -EFAULT;
	}

	return 0;
}

int thingsboard_rpc_response_decode(const char *buffer, size_t len, thingsboard_rpc_response *rr)
{
	*rr = (thingsboard_rpc_response)thingsboard_rpc_response_init_zero;

	pb_istream_t stream = pb_istream_from_buffer(buffer, len);

	bool success = pb_decode(&stream, thingsboard_rpc_response_fields, rr);
	if (!success) {
		LOG_WRN("Failed to decode `thingsboard_rpc_response`: %s", PB_GET_ERROR(&stream));
		return -EFAULT;
	}

	return 0;
}

int thingsboard_rpc_request_encode(const thingsboard_rpc_request *rq, char *buffer, size_t *len)
{
	pb_ostream_t stream = pb_ostream_from_buffer(buffer, *len);

	bool success = pb_encode(&stream, thingsboard_rpc_request_fields, rq);
	size_t message_length = stream.bytes_written;

	if (!success) {
		LOG_WRN("Failed to encode `thingsboard_rpc_request`: %s", PB_GET_ERROR(&stream));
		return -EFAULT;
	}

	*len = message_length;

	return 0;
}

int thingsboard_telemetry_encode(const thingsboard_telemetry *v, char *buffer, size_t *len)
{
	pb_ostream_t stream = pb_ostream_from_buffer(buffer, *len);

	bool success = pb_encode(&stream, thingsboard_telemetry_fields, v);
	size_t message_length = stream.bytes_written;

	if (!success) {
		LOG_WRN("Failed to encode `thingsboard_telemetry`: %s", PB_GET_ERROR(&stream));
		return -EFAULT;
	}

	*len = message_length;

	return 0;
}

struct timeseries_encode_ctx {
	const thingsboard_timeseries *ts;
	size_t count;
	size_t encoded;
};

static bool timeseries_encode_cb(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	struct timeseries_encode_ctx *ctx = *arg;

	for (ctx->encoded = 0; ctx->encoded < ctx->count; ctx->encoded++) {
		if (!pb_encode_tag_for_field(stream, field)) {
			return false;
		}

		if (!pb_encode_submessage(stream, thingsboard_timeseries_fields,
					  &ctx->ts[ctx->encoded])) {
			return false;
		}
	}

	return true;
}

int thingsboard_timeseries_encode(const thingsboard_timeseries *ts, size_t *ts_count, char *buffer,
				  size_t *len)
{
	pb_ostream_t stream = pb_ostream_from_buffer(buffer, *len);

	struct timeseries_encode_ctx ctx = {
		.ts = ts,
		.count = *ts_count,
	};

	thingsboard_timeseries_list ts_list = {
		.values =
			{
				.arg = &ctx,
				.funcs.encode = timeseries_encode_cb,
			},
	};

	bool success = pb_encode(&stream, thingsboard_timeseries_list_fields, &ts_list);
	*len = stream.bytes_written;
	*ts_count = ctx.encoded;

	if (!success) {
		LOG_WRN("Failed to encode `thingsboard_telemetry`: %s", PB_GET_ERROR(&stream));
		return -EFAULT;
	}

	return 0;
}
