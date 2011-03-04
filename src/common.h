/*!
 * \file common.h
 *
 * \author Lubos Slovak <lubos.slovak@nic.cz>
 *
 * \brief Common macros, includes and utilities.
 *
 * \addtogroup utils
 * @{
 */

#ifndef _CUTEDNS_COMMON_H_
#define _CUTEDNS_COMMON_H_

#include <signal.h>
#include <stdint.h>

/* Common types and constants.
 */

typedef unsigned int uint;

#define PROJECT_NAME "CuteDNS" // Project name
#define PROJECT_VER  0x000100  // 0xMMIIRR (MAJOR,MINOR,REVISION)
#define PROJECT_EXEC "cutedns" // Project executable
#define ZONEPARSER_EXEC "zoneparser" // Zoneparser executable

/* Server. */
#define CPU_ESTIMATE_MAGIC 2   // Extra threads above the number of processors
#define DEFAULT_THR_COUNT 1    // Default thread count for socket manager
#define DEFAULT_PORT 53531     // Default port

/* Sockets. */
#define TCP_BACKLOG_SIZE 5     // TCP listen backlog size

/* Memory allocator. */
//#define MEM_SLAB_CAP 3   // Cap slab_cache empty slab count (undefined = inf)
#define MEM_COLORING       // Slab cache coloring

//#define USE_HASH_TABLE
#define TEST_WITH_LDNS

/* Common includes.
 */

#include "print.h"
#include "log.h"
#include "debug.h"

/* Common inlines.
 */
#include <stdio.h>
static inline int fread_safe(void *dst, size_t size, size_t n, FILE *fp)
{
	int rc = fread(dst, size, n, fp);
	if (rc != n) {
		log_warning("fread: invalid read %d (expected %zu)\n", rc, n);
	}

	return rc == n;
}


/* Common macros.
 */

#define ERR_ALLOC_FAILED log_error("Allocation failed at %s:%d (%s ver.%x)\n", \
				  __FILE__, __LINE__, PROJECT_NAME, PROJECT_VER)

#define CHECK_ALLOC_LOG(var, ret) \
	do { \
		if ((var) == NULL) { \
			ERR_ALLOC_FAILED; \
			return (ret); \
		} \
	} while (0)

#define CHECK_ALLOC(var, ret) \
	do { \
		if ((var) == NULL) { \
			return (ret); \
		} \
	} while (0)

/* Eliminate compiler warning with unused parameters. */
#define UNUSED(param) (param) = (param)

/* Minimum and maximum macros. */
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))

/* Optimisation macros. */
#ifndef likely
#define likely(x)       __builtin_expect((x),1)
#endif
#ifndef unlikely
#define unlikely(x)     __builtin_expect((x),0)
#endif


#define perf_begin() \
do { \
 struct timeval __begin; \
 gettimeofday(&__begin, 0)

#define perf_end(d) \
 struct timeval __end; \
 gettimeofday(&__end, 0); \
 unsigned long __us = (__end.tv_sec - __begin.tv_sec) * 1000L * 1000L; \
 __us += (__end.tv_usec - __begin.tv_usec); \
 (d) = __us; \
} while(0)

//#define STAT_COMPILE
#define TEST_WITH_LDNS

#endif /* _CUTEDNS_COMMON_H_ */

/*! @} */
