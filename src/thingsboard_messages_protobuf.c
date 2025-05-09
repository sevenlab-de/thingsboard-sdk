#include <zephyr/logging/log.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "tb_internal.h"

LOG_MODULE_REGISTER(thingsboard_protobuf, CONFIG_THINGSBOARD_LOG_LEVEL);

/* Workaround for thingsboard not using specified format in device profile */
#define DECODE_ATTR_SINGULAR(obj, _fieldname)
#define DECODE_ATTR_OPTIONAL(obj, _fieldname) (obj).has_##_fieldname = true;

#define DECODE_ATTR_FIELD_STRING(obj, _fieldname)                                                  \
	if (tkp.kv.type != KeyValueType_STRING_V) {                                                \
		return false;                                                                      \
	}                                                                                          \
	const size_t field_max_length = sizeof(((thingsboard_attributes){})._fieldname);           \
	strncpy((obj)._fieldname, tkp.kv.string_v, field_max_length);                              \
	(obj)._fieldname[field_max_length - 1] = 0;

#define DECODE_ATTR_FIELD_UINT32(obj, _fieldname)                                                  \
	if (tkp.kv.type != KeyValueType_LONG_V || tkp.kv.long_v > UINT32_MAX) {                    \
		return false;                                                                      \
	}                                                                                          \
	(obj)._fieldname = (uint32_t)tkp.kv.long_v;

#define DECODE_ATTR_FIELDS(obj, _, _optional, _type, _fieldname, ___)                              \
	if (strncmp(tkp.kv.key, #_fieldname, sizeof(tkp.kv.key)) == 0) {                           \
		DECODE_ATTR_FIELD_##_type((obj), _fieldname);                                      \
		DECODE_ATTR_##_optional((obj), _fieldname);                                        \
		return true;                                                                       \
	}

static bool attribute_decode_cb(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	thingsboard_attributes *v = *arg;

	TsKvProto tkp = TsKvProto_init_zero;

	if (stream == NULL || field->tag != AttributeUpdateNotificationMsg_sharedUpdated_tag) {
		return true;
	}

	if (!pb_decode(stream, TsKvProto_fields, &tkp)) {
		return false;
	}

	if (tkp.has_kv) {
		thingsboard_attributes_FIELDLIST(DECODE_ATTR_FIELDS, *v);
		LOG_WRN("Ignored unknown attribute \"%s\"", tkp.kv.key);
	}

	return true;
}

static int thingsboard_AttributeUpdateNotificationMsg_decode(const char *buffer, size_t len,
							     thingsboard_attributes *v)
{
	AttributeUpdateNotificationMsg msg = {
		.sharedUpdated =
			{
				.arg = v,
				.funcs.decode = attribute_decode_cb,
			},
	};
	pb_istream_t stream = pb_istream_from_buffer(buffer, len);

	bool success = pb_decode(&stream, AttributeUpdateNotificationMsg_fields, &msg);
	if (!success) {
		LOG_WRN("Failed to decode `AttributeUpdateNotificationMsg`: %s",
			PB_GET_ERROR(&stream));
		return -EFAULT;
	}

	return 0;
}

int thingsboard_attributes_decode(const char *buffer, size_t len, thingsboard_attributes *v)
{
	*v = (thingsboard_attributes)thingsboard_attributes_init_zero;

#ifndef CONFIG_THINGSBOARD_PROTOBUF_ATTRIBUTES_WORKAROUND_DEFAULT
	pb_istream_t stream = pb_istream_from_buffer(buffer, len);

	bool success = pb_decode(&stream, thingsboard_attributes_fields, v);
	if (success) {
		return 0;
	}

	LOG_WRN("Failed to decode `thingsboard_attributes`: \"%s\" - Trying as "
		"AttributeUpdateNotificationMsg",
		PB_GET_ERROR(&stream));

#endif /* CONFIG_THINGSBOARD_PROTOBUF_ATTRIBUTES_WORKAROUND_DEFAULT */
	int err = thingsboard_AttributeUpdateNotificationMsg_decode(buffer, len, v);
	if (err < 0) {
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
