/**
 * Author:    Matthias Braun
 * Date:      05.05.2006
 * Copyright: (c) Universitaet Karlsruhe
 * License:   This file protected by GPL -  GNU GENERAL PUBLIC LICENSE.
 * CVS-Id:    $Id$
 */

/**
 * @file beverify.h
 *
 * Various verify routines that check a scheduled graph for correctness
 *
 * @author Matthias Braun
 */
#ifndef BEVERIFY_H_
#define BEVERIFY_H_

#include "bechordal.h"

/**
 * Verifies, that the register pressure for a given register class doesn't exceed the limit
 * of available registers.
 *
 * @param arch_env   An architecture environment
 * @param cls        The register class to check
 * @param irg        The irg to check
 * @return			 1 if the pressure is valid, 0 otherwise
 */
int be_verify_register_pressure(const arch_env_t *arch_env, const arch_register_class_t* cls, ir_graph *irg);

/**
 * Does some sanity checks on the schedule.
 *
 * @param irg	The irg to check
 * @return		1 if the schedule is valid, 0 otherwise
 */
int be_verify_schedule(ir_graph *irg);

#endif /* BEVERIFY_H_ */
