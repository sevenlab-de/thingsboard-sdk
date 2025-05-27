#include <errno.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "tb_internal.h"

LOG_MODULE_REGISTER(thingsboard_json, CONFIG_THINGSBOARD_LOG_LEVEL);

ssize_t thingsboard_attributes_update(thingsboard_attributes *changes,
				      thingsboard_attributes *current, void *buffer,
				      size_t buffer_len)
{
	if (changes == NULL || current == NULL || buffer == NULL) {
		return -EINVAL;
	}

	if (buffer_len < sizeof(struct thingsboard_attributes_buffer)) {
		return -ENOMEM;
	}

	struct thingsboard_attributes_buffer *attributes_buffer = buffer;

	ssize_t ret =
		thingsboard_attributes_update_with_buffer(changes, current, attributes_buffer);
	if (ret < 0) {
		return -EFAULT;
	}

	return ret;
}

#define JSON_OBJ_DESCR_OBJECT_DYN(struct_, field_name_, sub_descr_, sub_descr_len_)                \
	{                                                                                          \
		.field_name = (#field_name_),                                                      \
		.align_shift = Z_ALIGN_SHIFT(struct_),                                             \
		.field_name_len = (sizeof(#field_name_) - 1),                                      \
		.type = JSON_TOK_OBJECT_START,                                                     \
		.offset = offsetof(struct_, field_name_),                                          \
		.object =                                                                          \
			{                                                                          \
				.sub_descr = sub_descr_,                                           \
				.sub_descr_len = sub_descr_len_,                                   \
			},                                                                         \
	}

int thingsboard_attributes_decode(const char *buffer, size_t len, thingsboard_attributes *v)
{
	return thingsboard_attributes_from_json(buffer, len, v);
}

int thingsboard_rpc_response_decode(const char *buffer, size_t len, thingsboard_rpc_response *rr)
{
	/* The RPC response is in JSON format, but not encapsulated. Each
	 * RPC caller needs to implement its own parsing of the actual message.
	 *
	 * For Protobuf, the RPC response is encapsulated in an Protobuf so it
	 * needs to be unmarshalled there. Here nothing is to do, besides creating
	 * API compatibility with the Protobuf parsing code.
	 */
	rr->has_payload = true;
	rr->payload = buffer;

	return 0;
}

int thingsboard_rpc_request_encode(const thingsboard_rpc_request *rq, char *buffer, size_t *len)
{
	int err = thingsboard_rpc_request_to_buf(rq, buffer, *len);
	if (err < 0) {
		LOG_WRN("Failed to encode `thingsboard_rpc_request`: %d", err);
		return -EINVAL;
	}

	*len = strlen(buffer);

	return 0;
}

int thingsboard_telemetry_encode(const thingsboard_telemetry *v, char *buffer, size_t *len)
{
	int err = thingsboard_telemetry_to_buf(v, buffer, *len);
	if (err < 0) {
		LOG_WRN("Failed to encode `thingsboard_telemetry`: %d", err);
		return -EINVAL;
	}

	*len = strlen(buffer);
	return 0;
}

ssize_t encode_timestamped_telemetry_to_buf(const thingsboard_timeseries *ts, char *buffer,
					    size_t len)
{
	struct json_obj_descr values_descr[THINGSBOARD_TELEMETRY_VALUE_COUNT];

	ssize_t ret;
	ret = thingsboard_telemetry_gen_obj_desc(&ts->values, values_descr,
						 ARRAY_SIZE(values_descr));
	if (ret <= 0) {
		return ret;
	}

	struct json_obj_descr ts_descr[] = {
		JSON_OBJ_DESCR_PRIM(struct thingsboard_timeseries, ts, JSON_TOK_INT64),
		JSON_OBJ_DESCR_OBJECT_DYN(struct thingsboard_timeseries, values, values_descr, ret),
	};

	int err = json_obj_encode_buf(ts_descr, ARRAY_SIZE(ts_descr), ts, buffer, len);
	if (err < 0) {
		return err;
	}

	/* There is currently no nice way to get the encoded object size without
	 * calling the JSON encoder two times. It guarantees a zero delimiter,
	 * though.
	 */
	return strlen(buffer);
}

int thingsboard_timeseries_encode(const thingsboard_timeseries *ts, size_t *ts_count, char *buffer,
				  size_t *len)
{
	/* The JSON encoder has no way to describe an array of object of different type.
	 * But as in our object, a field might or might not be set, every element might be
	 * different. Thus, encode each object separately and make the array manually.
	 */
	size_t pos = 0;
	size_t left = *len;
	size_t ts_encoded = 0;

	/* We have at least the opening and closing brackets and zero delimiter */
	if (left < 3) {
		return -ENOMEM;
	}

	buffer[pos++] = '[';
	left--;

	for (size_t i = 0; i < *ts_count; i++) {
		if (ts_encoded > 0) {
			/* If there is only enough space to encode the closing bracket and the zero
			 * delimiter, directly stop here */
			if (left <= 2) {
				break;
			}
			buffer[pos++] = ',';
			left--;
		}
		/* There are at least 2 bytes needed at the end of the buffer for the closing
		 * bracket and the zero delimiter, thefore the available buffer is two bytes less
		 * than the actual space left at this point */
		ssize_t ret = encode_timestamped_telemetry_to_buf(&ts[i], &buffer[pos], left - 2);
		if (ret == -ENOMEM) {
			/* Entry did not fit into buffer, just stop here */
			if (ts_encoded > 0) {
				/* remove trailing comma */
				left++;
				pos--;
			}
			break;
		}
		if (ret < 0) {
			return ret;
		}

		pos += ret;
		left -= ret;
		ts_encoded++;
	}

	if (left < 2) {
		return -ENOMEM;
	}

	buffer[pos++] = ']';
	left--;

	buffer[pos++] = 0;
	left--;

	*len = pos;
	*ts_count = ts_encoded;

	return 0;
}
