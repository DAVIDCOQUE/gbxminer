#pragma once

/* In C++ bool is a keyword; macroising it makes <xkeycheck.h> reject the
 * translation unit. C still needs these on the older MSVC this targets. */
#ifndef __cplusplus

#define false   0
#define true    1

#define bool int

#endif
