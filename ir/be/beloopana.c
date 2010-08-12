/*
 * Copyright (C) 1995-2008 University of Karlsruhe.  All right reserved.
 *
 * This file is part of libFirm.
 *
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 *
 * Licensees holding valid libFirm Professional Edition licenses may use
 * this file in accordance with the libFirm Commercial License.
 * Agreement provided with the Software.
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.
 */

/**
 * @file
 * @brief       Compute register pressure in loops.
 * @author      Christian Wuerdig
 * @date        19.02.2007
 * @version     $Id$
 */
#include "config.h"

#include "set.h"
#include "pset.h"
#include "irnode.h"
#include "irtools.h"
#include "irloop_t.h"
#include "error.h"
#include "debug.h"

#include "bearch.h"
#include "belive.h"
#include "besched.h"
#include "beloopana.h"
#include "bemodule.h"

DEBUG_ONLY(static firm_dbg_module_t *dbg = NULL);

#define HASH_LOOP_INFO(info) (HASH_PTR((info)->loop) ^ HASH_PTR((info)->cls))

typedef struct be_loop_info_t {
	ir_loop                     *loop;
	const arch_register_class_t *cls;
	unsigned                    max_pressure;
} be_loop_info_t;

struct be_loopana_t {
	set      *data;
	ir_graph *irg;
};

static int cmp_loop_info(const void *a, const void *b, size_t size)
{
	const be_loop_info_t *i1 = a;
	const be_loop_info_t *i2 = b;
	(void) size;

	return ! (i1->loop == i2->loop && i1->cls == i2->cls);
}

/**
 * Compute the highest register pressure in a block.
 * @param irg       The graph.
 * @param block     The block to compute pressure for.
 * @param cls       The register class to compute pressure for.
 * @return The highest register pressure in the given block.
 */
static unsigned be_compute_block_pressure(const ir_graph *irg,
                                          ir_node *block,
                                          const arch_register_class_t *cls)
{
	be_lv_t      *lv = be_get_irg_liveness(irg);
	ir_nodeset_t  live_nodes;
	ir_node      *irn;
	int          max_live;

	DBG((dbg, LEVEL_1, "Processing Block %+F\n", block));

	/* determine largest pressure with this block */
	ir_nodeset_init(&live_nodes);
	be_liveness_end_of_block(lv, cls, block, &live_nodes);
	max_live   = ir_nodeset_size(&live_nodes);

	sched_foreach_reverse(block, irn) {
		int cnt;

		if (is_Phi(irn))
			break;

		be_liveness_transfer(cls, irn, &live_nodes);
		cnt      = ir_nodeset_size(&live_nodes);
		max_live = MAX(cnt, max_live);
	}

	DBG((dbg, LEVEL_1, "Finished with Block %+F (%s %u)\n", block, cls->name, max_live));

	ir_nodeset_destroy(&live_nodes);
	return max_live;
}

/**
 * Compute the highest register pressure in a loop and it's sub-loops.
 * @param loop_ana  The loop ana object.
 * @param loop      The loop to compute pressure for.
 * @param cls       The register class to compute pressure for.
 * @return The highest register pressure in the given loop.
 */
static unsigned be_compute_loop_pressure(be_loopana_t *loop_ana, ir_loop *loop,
                                         const arch_register_class_t *cls)
{
	int            i, max;
	unsigned       pressure;
	be_loop_info_t *entry, key;

	DBG((dbg, LEVEL_1, "\nProcessing Loop %d\n", loop->loop_nr));
	assert(get_loop_n_elements(loop) > 0);
	pressure = 0;

	/* determine maximal pressure in all loop elements */
	for (i = 0, max = get_loop_n_elements(loop); i < max; ++i) {
		unsigned     son_pressure;
		loop_element elem = get_loop_element(loop, i);

		if (*elem.kind == k_ir_node)
			son_pressure = be_compute_block_pressure(loop_ana->irg, elem.node, cls);
		else {
			assert(*elem.kind == k_ir_loop);
			son_pressure = be_compute_loop_pressure(loop_ana, elem.son, cls);
		}

		pressure = MAX(pressure, son_pressure);
	}
	DBG((dbg, LEVEL_1, "Done with loop %d, pressure %u for class %s\n", loop->loop_nr, pressure, cls->name));

	/* update info in set */
	key.loop            = loop;
	key.cls             = cls;
	key.max_pressure    = 0;
	entry               = set_insert(loop_ana->data, &key, sizeof(key), HASH_LOOP_INFO(&key));
	entry->max_pressure = MAX(entry->max_pressure, pressure);

	return pressure;
}

/**
 * Compute the register pressure for a class of all loops in a graph
 * @param irg   The graph
 * @param cls   The register class to compute the pressure for
 * @return The loop analysis object.
 */
be_loopana_t *be_new_loop_pressure_cls(ir_graph *irg,
                                       const arch_register_class_t *cls)
{
	be_loopana_t *loop_ana = XMALLOC(be_loopana_t);

	loop_ana->data = new_set(cmp_loop_info, 16);
	loop_ana->irg  = irg;

	DBG((dbg, LEVEL_1, "\n=====================================================\n", cls->name));
	DBG((dbg, LEVEL_1, " Computing register pressure for class %s:\n", cls->name));
	DBG((dbg, LEVEL_1, "=====================================================\n", cls->name));

	/* construct control flow loop tree */
	if (! (get_irg_loopinfo_state(irg) & loopinfo_cf_consistent)) {
		construct_cf_backedges(irg);
	}

	be_compute_loop_pressure(loop_ana, get_irg_loop(irg), cls);

	return loop_ana;
}

/**
 * Compute the register pressure for all classes of all loops in the irg.
 * @param irg  The graph
 * @return The loop analysis object.
 */
be_loopana_t *be_new_loop_pressure(ir_graph *irg,
                                   const arch_register_class_t *cls)
{
	be_loopana_t     *loop_ana = XMALLOC(be_loopana_t);
	ir_loop          *irg_loop = get_irg_loop(irg);
	const arch_env_t *arch_env = be_get_irg_arch_env(irg);
	int               i;

	loop_ana->data = new_set(cmp_loop_info, 16);
	loop_ana->irg  = irg;

	/* construct control flow loop tree */
	if (! (get_irg_loopinfo_state(irg) & loopinfo_cf_consistent)) {
		construct_cf_backedges(irg);
	}

	if (cls != NULL) {
		be_compute_loop_pressure(loop_ana, irg_loop, cls);
	} else {
		for (i = arch_env_get_n_reg_class(arch_env) - 1; i >= 0; --i) {
			const arch_register_class_t *cls = arch_env_get_reg_class(arch_env, i);
			DBG((dbg, LEVEL_1, "\n=====================================================\n", cls->name));
			DBG((dbg, LEVEL_1, " Computing register pressure for class %s:\n", cls->name));
			DBG((dbg, LEVEL_1, "=====================================================\n", cls->name));
			be_compute_loop_pressure(loop_ana, irg_loop, cls);
		}
	}

	return loop_ana;
}

/**
 * Returns the computed register pressure for the given class and loop.
 * @return The pressure or INT_MAX if not found
 */
unsigned be_get_loop_pressure(be_loopana_t *loop_ana, const arch_register_class_t *cls, ir_loop *loop)
{
	unsigned pressure = INT_MAX;
	be_loop_info_t *entry, key;

	assert(cls && loop);

	key.loop = loop;
	key.cls  = cls;
	entry    = set_find(loop_ana->data, &key, sizeof(key), HASH_LOOP_INFO(&key));

	if (entry)
		pressure = entry->max_pressure;
	else
		panic("Pressure not computed for given class and loop object.");

	return pressure;
}

/**
 * Frees the loop analysis object.
 */
void be_free_loop_pressure(be_loopana_t *loop_ana)
{
	del_set(loop_ana->data);
	xfree(loop_ana);
}

BE_REGISTER_MODULE_CONSTRUCTOR(be_init_loopana);
void be_init_loopana(void)
{
	FIRM_DBG_REGISTER(dbg, "firm.be.loopana");
}
