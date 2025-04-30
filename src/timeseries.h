#ifndef TB_TIMESERIES_H
#define TB_TIMESERIES_H

#include <thingsboard.h>

/**
 * Encodes multiple `struct thingsboard_timeseries` into an json array.
 *
 * Note: only encodes as much `struct thingsboard_timeseries` as fit into the
 * given `len`.
 *
 * Returns negative values for error, positive values for the amount of
 * `struct thingsboard_timeseries` which fitted into `buffer`.
 */
ssize_t thingsboard_timeseries_to_buf(const struct thingsboard_timeseries *ts, size_t ts_count,
				      char *buffer, size_t len);

#endif /* TB_TIMESERIES_H */
