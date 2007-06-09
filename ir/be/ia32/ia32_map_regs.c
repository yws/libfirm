/*
 * Copyright (C) 1995-2007 University of Karlsruhe.  All right reserved.
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
 * @brief       Register param constraints and some other register handling tools.
 * @author      Christian Wuerdig
 * @version     $Id$
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "pmap.h"

#include "ia32_map_regs.h"
#include "ia32_new_nodes.h"
#include "gen_ia32_regalloc_if.h"
#include "bearch_ia32_t.h"
#include "../benodesets.h"

static int maxnum_gpreg_args = 3;   /* maximum number of int arguments passed in registers; default 3 */
static int maxnum_sse_args = 5;   /* maximum number of float arguments passed in registers; default 5 */

/* this is the order of the assigned registers usesd for parameter passing */

const arch_register_t *gpreg_param_reg_std[] = {
	&ia32_gp_regs[REG_EAX],
	&ia32_gp_regs[REG_EDX],
	&ia32_gp_regs[REG_ECX],
	&ia32_gp_regs[REG_EBX],
	&ia32_gp_regs[REG_EDI],
	&ia32_gp_regs[REG_ESI]
};

const arch_register_t *gpreg_param_reg_this[] = {
	&ia32_gp_regs[REG_ECX],
	&ia32_gp_regs[REG_EAX],
	&ia32_gp_regs[REG_EDX],
	&ia32_gp_regs[REG_EBX],
	&ia32_gp_regs[REG_EDI],
	&ia32_gp_regs[REG_ESI]
};

const arch_register_t *fpreg_sse_param_reg_std[] = {
	&ia32_xmm_regs[REG_XMM0],
	&ia32_xmm_regs[REG_XMM1],
	&ia32_xmm_regs[REG_XMM2],
	&ia32_xmm_regs[REG_XMM3],
	&ia32_xmm_regs[REG_XMM4],
	&ia32_xmm_regs[REG_XMM5],
	&ia32_xmm_regs[REG_XMM6],
	&ia32_xmm_regs[REG_XMM7]
};

const arch_register_t *fpreg_sse_param_reg_this[] = {
	NULL,  /* in case of a "this" pointer, the first parameter must not be a float */
	&ia32_xmm_regs[REG_XMM0],
	&ia32_xmm_regs[REG_XMM1],
	&ia32_xmm_regs[REG_XMM2],
	&ia32_xmm_regs[REG_XMM3],
	&ia32_xmm_regs[REG_XMM4],
	&ia32_xmm_regs[REG_XMM5],
	&ia32_xmm_regs[REG_XMM6],
	&ia32_xmm_regs[REG_XMM7]
};



/* Mapping to store registers in firm nodes */

struct ia32_irn_reg_assoc {
	const ir_node *irn;
	const arch_register_t *reg;
};

int ia32_cmp_irn_reg_assoc(const void *a, const void *b, size_t len) {
	const struct ia32_irn_reg_assoc *x = a;
	const struct ia32_irn_reg_assoc *y = b;

	return x->irn != y->irn;
}

static struct ia32_irn_reg_assoc *get_irn_reg_assoc(const ir_node *irn, set *reg_set) {
	struct ia32_irn_reg_assoc templ;
	unsigned int hash;

	templ.irn = irn;
	templ.reg = NULL;
	hash = nodeset_hash(irn);

	return set_insert(reg_set, &templ, sizeof(templ), hash);
}

void ia32_set_firm_reg(ir_node *irn, const arch_register_t *reg, set *reg_set) {
	struct ia32_irn_reg_assoc *assoc = get_irn_reg_assoc(irn, reg_set);
	assoc->reg = reg;
}

const arch_register_t *ia32_get_firm_reg(const ir_node *irn, set *reg_set) {
	struct ia32_irn_reg_assoc *assoc = get_irn_reg_assoc(irn, reg_set);
	return assoc->reg;
}

void ia32_build_16bit_reg_map(pmap *reg_map) {
	pmap_insert(reg_map, &ia32_gp_regs[REG_EAX], "ax");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EBX], "bx");
	pmap_insert(reg_map, &ia32_gp_regs[REG_ECX], "cx");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EDX], "dx");
	pmap_insert(reg_map, &ia32_gp_regs[REG_ESI], "si");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EDI], "di");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EBP], "bp");
	pmap_insert(reg_map, &ia32_gp_regs[REG_ESP], "sp");
}

void ia32_build_8bit_reg_map(pmap *reg_map) {
	pmap_insert(reg_map, &ia32_gp_regs[REG_EAX], "al");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EBX], "bl");
	pmap_insert(reg_map, &ia32_gp_regs[REG_ECX], "cl");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EDX], "dl");
}

void ia32_build_8bit_reg_map_high(pmap *reg_map) {
	pmap_insert(reg_map, &ia32_gp_regs[REG_EAX], "ah");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EBX], "bh");
	pmap_insert(reg_map, &ia32_gp_regs[REG_ECX], "ch");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EDX], "dh");
}

const char *ia32_get_mapped_reg_name(pmap *reg_map, const arch_register_t *reg) {
	pmap_entry *e = pmap_find(reg_map, (void *)reg);

	//assert(e && "missing map init?");
	if (! e) {
		printf("FIXME: ia32map_regs.c:122: returning fake register name for ia32 with 32 register\n");
		return reg->name;
	}

	return e->value;
}

/**
 * Check all parameters and determine the maximum number of parameters
 * to pass in gp regs resp. in fp regs.
 *
 * @param n       The number of parameters
 * @param modes   The list of the parameter modes
 * @param n_int   Holds the number of int parameters to be passed in regs after the call
 * @param n_float Holds the number of float parameters to be passed in regs after the call
 * @return        The number of the last parameter to be passed in register
 */
int ia32_get_n_regparam_class(ia32_code_gen_t *cg, int n, ir_mode **modes,
                              int *n_int, int *n_float)
{
	int i, finished = 0;
	int max_fp_regs;

	if(USE_SSE2(cg)) {
		max_fp_regs = maxnum_sse_args;
	} else {
		max_fp_regs = 0;
	}

	*n_int   = 0;
	*n_float = 0;

	for (i = 0; i < n && !finished; i++) {
		if (mode_is_int(modes[i]) || mode_is_reference(modes[i])) {
			*n_int = *n_int + 1;
		}
		else if (mode_is_float(modes[i])) {
			*n_float = *n_float + 1;
		}
		else {
			finished = 1;
		}

		/* test for maximum */
		if (*n_int == maxnum_gpreg_args || *n_float == max_fp_regs) {
			finished = 1;
		}
	}

	return i - 1;
}


/**
 * Returns the register for parameter nr.
 *
 * @param n     The number of parameters
 * @param modes The list of the parameter modes
 * @param nr    The number of the parameter to return the requirements for
 * @param cc    The calling convention
 * @return      The register
 */
const arch_register_t *ia32_get_RegParam_reg(ia32_code_gen_t *cg, unsigned cc,
                                             unsigned nr, ir_mode *mode)
{
	if(mode_is_float(mode)) {
		if(!USE_SSE2(cg))
			return NULL;
		assert(nr < maxnum_sse_args);
		if(cc & cc_this_call) {
			return fpreg_sse_param_reg_this[nr];
		}
		return fpreg_sse_param_reg_std[nr];
	}

	assert(mode_is_int(mode) || mode_is_reference(mode));

	if(cc & cc_this_call) {
		assert(nr < maxnum_gpreg_args);
		return gpreg_param_reg_this[nr];
	}
	return gpreg_param_reg_std[nr];
}
