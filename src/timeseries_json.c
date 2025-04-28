#include <errno.h>
#include <string.h>

#include <zephyr/data/json.h>

#include "timeseries.h"

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

ssize_t encode_timestamped_telemetry_to_buf(const struct thingsboard_timeseries *ts, char *buffer,
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

ssize_t thingsboard_timeseries_to_buf(const struct thingsboard_timeseries *ts, size_t ts_count,
				      char *buffer, size_t len)
{
	/* The JSON encoder has no way to describe an array of object of different type.
	 * But as in our object, a field might or might not be set, every element might be
	 * different. Thus, encode each object separately and make the array manually.
	 */
	size_t pos = 0;
	size_t left = len;
	size_t ts_encoded = 0;

	/* We have at least the opening and closing brackets and zero delimiter */
	if (left < 3) {
		return -ENOMEM;
	}

	buffer[pos++] = '[';
	left--;

	for (size_t i = 0; i < ts_count; i++) {
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

	return ts_encoded;
}
