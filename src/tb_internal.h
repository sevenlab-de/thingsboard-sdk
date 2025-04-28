#ifndef _TB_INTERNAL_H_
#define _TB_INTERNAL_H_

#include "thingsboard.h"

#ifdef CONFIG_THINGSBOARD_TIME
void thingsboard_start_time_sync(void);
#endif /* CONFIG_THINGSBOARD_TIME */

extern const char *thingsboard_access_token;

#endif /* _TB_INTERNAL_H_ */
