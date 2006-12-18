#ifndef _BEARCH_IA32_T_H_
#define _BEARCH_IA32_T_H_

#include "firm_config.h"

#include "pmap.h"
#include "debug.h"
#include "bearch_ia32.h"
#include "ia32_nodes_attr.h"
#include "set.h"

#include "../be.h"
#include "../bemachine.h"

#ifdef NDEBUG
#define SET_IA32_ORIG_NODE(n, o)
#else
#define SET_IA32_ORIG_NODE(n, o) set_ia32_orig_node(n, o);
#endif /* NDEBUG */

/* some typedefs */

/**
 * Bitmask for the backend optimization settings.
 */
typedef enum _ia32_optimize_t {
	IA32_OPT_INCDEC    = 1,   /**< optimize add/sub 1/-1 to inc/dec */
	IA32_OPT_DOAM      = 2,   /**< do address mode optimizations */
	IA32_OPT_LEA       = 4,   /**< optimize address calculations into LEAs */
	IA32_OPT_PLACECNST = 8,   /**< place constants in the blocks where they are used */
	IA32_OPT_IMMOPS    = 16,  /**< create operations with immediate operands */
	IA32_OPT_EXTBB     = 32,  /**< do extended basic block scheduling */
	IA32_OPT_PUSHARGS  = 64,  /**< create pushs for function argument passing */
} ia32_optimize_t;

/**
 * Architectures. Clustered for easier macro implementation,
 * do not change.
 */
typedef enum cpu_support {
	arch_i386,          /**< i386 */
	arch_i486,          /**< i486 */
	arch_pentium,       /**< Pentium */
	arch_pentium_pro,   /**< Pentium Pro */
	arch_pentium_mmx,   /**< Pentium MMX */
	arch_pentium_2,     /**< Pentium II */
	arch_pentium_3,     /**< Pentium III */
	arch_pentium_4,     /**< Pentium IV */
	arch_pentium_m,     /**< Pentium M */
	arch_core,          /**< Core */
	arch_k6,            /**< K6 */
	arch_athlon,        /**< Athlon */
	arch_athlon_64,     /**< Athlon64 */
	arch_opteron,       /**< Opteron */
} cpu_support;

/** checks for l <= x <= h */
#define _IN_RANGE(x, l, h)  ((unsigned)((x) - (l)) <= (unsigned)((h) - (l)))

/** returns true if it's Intel architecture */
#define ARCH_INTEL(x)       _IN_RANGE((x), arch_i386, arch_core)

/** returns true if it's AMD architecture */
#define ARCH_AMD(x)         _IN_RANGE((x), arch_k6, arch_opteron)

#define IS_P6_ARCH(x)       (_IN_RANGE((x), arch_pentium_pro, arch_core) || \
                             _IN_RANGE((x), arch_athlon, arch_opteron))

/** floating point support */
typedef enum fp_support {
	fp_none,  /**< no floating point instructions are used */
	fp_x87,   /**< use x87 instructions */
	fp_sse2   /**< use SSE2 instructions */
} fp_support;

/** Sets the used flag to the current floating point architecture. */
#define FP_USED(cg)  ((cg)->used_fp = (cg)->fp_kind)

/** Returns non-zero if the current floating point architecture is SSE2. */
#define USE_SSE2(cg) ((cg)->fp_kind == fp_sse2)

/** Returns non-zero if the current floating point architecture is x87. */
#define USE_x87(cg)  ((cg)->fp_kind == fp_x87)

/** Sets the flag to enforce x87 simulation. */
#define FORCE_x87(cg) ((cg)->force_sim = 1)

typedef struct _ia32_isa_t ia32_isa_t;

/**
 * IA32 code generator
 */
typedef struct _ia32_code_gen_t {
	const arch_code_generator_if_t *impl;          /**< implementation */
	ir_graph                       *irg;           /**< current irg */
	const arch_env_t               *arch_env;      /**< the arch env */
	set                            *reg_set;       /**< set to memorize registers for non-ia32 nodes (e.g. phi nodes) */
	ia32_isa_t                     *isa;           /**< for fast access to the isa object */
	be_irg_t                       *birg;          /**< The be-irg (contains additional information about the irg) */
	ir_node                        **blk_sched;    /**< an array containing the scheduled blocks */
	ia32_optimize_t                opt;            /**< contains optimization information */
	nodeset                        *kill_conv;     /**< Remember all convs to be killed */
	int                            arch;           /**< instruction architecture */
	int                            opt_arch;       /**< optimize for architecture */
	char                           fp_kind;        /**< floating point kind */
	char                           used_fp;        /**< which floating point unit used in this graph */
	char                           force_sim;      /**< set to 1 if x87 simulation should be enforced */
	char                           dump;           /**< set to 1 if graphs should be dumped */
	DEBUG_ONLY(firm_dbg_module_t   *mod;)          /**< debugging module */
} ia32_code_gen_t;

/**
 * IA32 ISA object
 */
struct _ia32_isa_t {
	arch_isa_t            arch_isa;       /**< must be derived from arch_isa_t */
	pmap                  *regs_16bit;    /**< Contains the 16bits names of the gp registers */
	pmap                  *regs_8bit;     /**< Contains the 8bits names of the gp registers */
	pmap                  *types;         /**< A map of modes to primitive types */
	pmap                  *tv_ent;        /**< A map of entities that store const tarvals */
	ia32_optimize_t       opt;            /**< contains optimization information */
	int                   arch;           /**< instruction architecture */
	int                   opt_arch;       /**< optimize for architecture */
	int                   fp_kind;        /**< floating point kind */
	ia32_code_gen_t       *cg;            /**< the current code generator */
	FILE                  *out;           /**< output file */
	const be_machine_t    *cpu;           /**< the abstract machine */
#ifndef NDEBUG
	struct obstack        *name_obst;     /**< holds the original node names (for debugging) */
#endif /* NDEBUG */
};

typedef struct _ia32_irn_ops_t {
	const arch_irn_ops_if_t *impl;
	ia32_code_gen_t         *cg;
} ia32_irn_ops_t;

/* this is a struct to minimize the number of parameters
   for transformation walker */
typedef struct _ia32_transform_env_t {
	dbg_info          *dbg;        /**< The node debug info */
	ir_graph          *irg;        /**< The irg, the node should be created in */
	ir_node           *block;      /**< The block, the node should belong to */
	ir_node           *irn;        /**< The irn, to be transformed */
	ir_mode           *mode;       /**< The mode of the irn */
	ia32_code_gen_t   *cg;         /**< The code generator */
	DEBUG_ONLY(firm_dbg_module_t *mod;) /**< The firm debugger */
} ia32_transform_env_t;

typedef struct _ia32_intrinsic_env_t {
	ir_graph  *irg;           /**< the irg, these entities belong to */
	ir_entity *ll_div_op1;    /**< entity for first div operand (move into FPU) */
	ir_entity *ll_div_op2;    /**< entity for second div operand (move into FPU) */
	ir_entity *ll_d_conv;     /**< entity for converts ll -> d */
	ir_entity *d_ll_conv;     /**< entity for converts d -> ll */
} ia32_intrinsic_env_t;

/**
 * Returns the unique per irg GP NoReg node.
 */
ir_node *ia32_new_NoReg_gp(ia32_code_gen_t *cg);

/**
 * Returns the unique per irg FP NoReg node.
 */
ir_node *ia32_new_NoReg_fp(ia32_code_gen_t *cg);

/**
 * Returns gp_noreg or fp_noreg, depending on input requirements.
 */
ir_node *ia32_get_admissible_noreg(ia32_code_gen_t *cg, ir_node *irn, int pos);

/**
 * Maps all intrinsic calls that the backend support
 * and map all instructions the backend did not support
 * to runtime calls.
 */
void ia32_handle_intrinsics(void);

/**
 * Ia32 implementation.
 *
 * @param method   the method type of the emulation function entity
 * @param op       the emulated ir_op
 * @param imode    the input mode of the emulated opcode
 * @param omode    the output mode of the emulated opcode
 * @param context  the context parameter
 */
ir_entity *ia32_create_intrinsic_fkt(ir_type *method, const ir_op *op,
                                     const ir_mode *imode, const ir_mode *omode,
                                     void *context);

#endif /* _BEARCH_IA32_T_H_ */
