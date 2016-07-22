#pragma once

#if !defined(__cplusplus)
#define bool int8_t
#define true 1
#define false 0
#define __bool_true_false_are_defined 1
#endif

#if defined(WIN32) && !defined(__cplusplus)
#define inline __inline
#endif
#define snprintf _snprintf

#include "../media-io-defs.h"