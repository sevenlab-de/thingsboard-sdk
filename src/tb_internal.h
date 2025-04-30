#ifndef _TB_INTERNAL_H_
#define _TB_INTERNAL_H_

#include "thingsboard.h"

void thingsboard_event(enum thingsboard_event event);

#ifdef CONFIG_THINGSBOARD_TIME
void thingsboard_start_time_sync(void);
#endif /* CONFIG_THINGSBOARD_TIME */

extern const char *thingsboard_access_token;

#ifdef CONFIG_THINGSBOARD_CONTENT_FORMAT_PROTOBUF

int thingsboard_attributes_decode(const char *buffer, size_t len, thingsboard_attributes *v);
int thingsboard_rpc_response_decode(const char *buffer, size_t len, thingsboard_rpc_response *rr);

int thingsboard_rpc_request_encode(const thingsboard_rpc_request *rq, char *buffer, size_t *len);
int thingsboard_telemetry_encode(const thingsboard_telemetry *v, char *buffer, size_t *len);
int thingsboard_timeseries_encode(const thingsboard_timeseries *ts, size_t *ts_count, char *buffer,
				  size_t *len);

#endif /* CONFIG_THINGSBOARD_CONTENT_FORMAT_PROTOBUF */

#endif /* _TB_INTERNAL_H_ */
