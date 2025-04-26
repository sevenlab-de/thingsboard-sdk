#ifndef TB_TIMESERIES_H
#define TB_TIMESERIES_H

#include <thingsboard.h>

int thingsboard_timeseries_to_buf(const struct thingsboard_timeseries *ts, size_t ts_count,
				  char *buffer, size_t len);

#endif /* TB_TIMESERIES_H */
