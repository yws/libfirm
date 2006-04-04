/**
 * This file contains macros to update ia32 firm statistics
 * @author Christian Wuerdig
 * $Id$
 */
#ifndef _IA32_DBG_STAT_H_
#define _IA32_DBG_STAT_H_

#include "irhooks.h"
#include "dbginfo_t.h"
#include "firmstat.h"

#define SIZ(x)    sizeof(x)/sizeof((x)[0])

/**
 * Merge the debug info due to a LEA creation.
 *
 * @param oldn  the node
 * @param n     the new lea
 */
#define DBG_OPT_LEA1(oldn, n)                                \
	do {                                                    \
		hook_merge_nodes(&n, 1, &oldn, 1, FS_BE_IA32_LEA);  \
		__dbg_info_merge_pair(n, oldn, dbg_backend);        \
	} while(0)


/**
 * Merge the debug info due to a LEA creation.
 *
 * @param oldn1  the old node
 * @param oldn2  an additional old node
 * @param n      the new lea
 */
#define DBG_OPT_LEA2(oldn1, oldn2, n)                              \
	do {                                                           \
		ir_node *ons[2];                                           \
		ons[0] = oldn1;                                            \
		ons[1] = oldn2;                                            \
		hook_merge_nodes(&n, 1, ons, SIZ(ons), FS_BE_IA32_LEA);    \
		__dbg_info_merge_sets(&n, 1, ons, SIZ(ons), dbg_backend);  \
	} while(0)

/**
 * Merge the debug info due to a LEA creation.
 *
 * @param oldn1  the old node
 * @param oldn2  an additional old node
 * @param oldn3  an additional old node
 * @param n      the new lea
 */
#define DBG_OPT_LEA3(oldn1, oldn2, oldn3, n)                       \
	do {                                                           \
		ir_node *ons[3];                                           \
		ons[0] = oldn1;                                            \
		ons[1] = oldn2;                                            \
		ons[2] = oldn3;                                            \
		hook_merge_nodes(&n, 1, ons, SIZ(ons), FS_BE_IA32_LEA);    \
		__dbg_info_merge_sets(&n, 1, ons, SIZ(ons), dbg_backend);  \
	} while(0)

/**
 * Merge the debug info due to a LEA creation.
 *
 * @param oldn1  the old node
 * @param oldn2  an additional old node
 * @param oldn3  an additional old node
 * @param oldn4  an additional old node
 * @param n      the new lea
 */
#define DBG_OPT_LEA4(oldn1, oldn2, oldn3, oldn4, n)                \
	do {                                                           \
		ir_node *ons[4];                                           \
		ons[0] = oldn1;                                            \
		ons[1] = oldn2;                                            \
		ons[2] = oldn3;                                            \
		ons[3] = oldn4;                                            \
		hook_merge_nodes(&n, 1, ons, SIZ(ons), FS_BE_IA32_LEA);    \
		__dbg_info_merge_sets(&n, 1, ons, SIZ(ons), dbg_backend);  \
	} while(0)

/**
 * Merge the debug info due to a Load with LEA creation.
 *
 * @param oldn  the lea
 * @param n     the new load
 */
#define DBG_OPT_LOAD_LEA(oldn, n)                                \
	do {                                                         \
		hook_merge_nodes(&n, 1, &oldn, 1, FS_BE_IA32_LOAD_LEA);  \
		__dbg_info_merge_pair(n, oldn, dbg_backend);             \
	} while(0)

/**
 * Merge the debug info due to a Store with LEA creation.
 *
 * @param oldn  the lea
 * @param n     the new store
 */
#define DBG_OPT_STORE_LEA(oldn, n)                                \
	do {                                                          \
		hook_merge_nodes(&n, 1, &oldn, 1, FS_BE_IA32_STORE_LEA);  \
		__dbg_info_merge_pair(n, oldn, dbg_backend);              \
	} while(0)

/**
 * Merge the debug info due to a source address mode creation.
 *
 * @param oldn  the old load
 * @param n     the new op
 */
#define DBG_OPT_AM_S(oldn, n)                                \
	do {                                                     \
		hook_merge_nodes(&n, 1, &oldn, 1, FS_BE_IA32_AM_S);  \
		__dbg_info_merge_pair(n, oldn, dbg_backend);         \
	} while(0)

/**
 * Merge the debug info due to a destination address mode creation.
 *
 * @param load   the old load
 * @param store  yhe old store
 * @param n      the new op
 */
#define DBG_OPT_AM_D(load, store, n)                               \
	do {                                                           \
		ir_node *ons[2];                                           \
		ons[0] = load;                                             \
		ons[1] = store;                                            \
		hook_merge_nodes(&n, 1, ons, SIZ(ons), FS_BE_IA32_AM_D);   \
		__dbg_info_merge_sets(&n, 1, ons, SIZ(ons), dbg_backend);  \
	} while(0)

#endif /* _IA32_DBG_STAT_H_ */
