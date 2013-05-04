#!/usr/bin/perl -w

#
# Copyright (C) 1995-2008 University of Karlsruhe.  All right reserved.
#
# This file is part of libFirm.
#
# This file may be distributed and/or modified under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation and appearing in the file LICENSE.GPL included in the
# packaging of this file.
#
# Licensees holding valid libFirm Professional Edition licenses may use
# this file in accordance with the libFirm Commercial License.
# Agreement provided with the Software.
#
# This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
# WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE.
#

# This script generates C code which emits assembler code for the
# assembler ir nodes. It takes a "emit" key from the node specification
# and substitutes lines starting with . with a corresponding fprintf().

use strict;
use Data::Dumper;

our $specfile   = $ARGV[0];
our $target_dir = $ARGV[1];

our $arch;
our %nodes;

my $return;

no strict "subs";
unless ($return = do $specfile) {
	die "Fatal error: couldn't parse $specfile: $@" if $@;
	die "Fatal error: couldn't do $specfile: $!"    unless defined $return;
	die "Fatal error: couldn't run $specfile"       unless $return;
}
use strict "subs";

my $target_c = $target_dir."/gen_".$arch."_emitter.c";
my $target_h = $target_dir."/gen_".$arch."_emitter.h";

# buffers for output
my $obst_func     = ""; # buffer for the emit functions
my $obst_register = ""; # buffer for emitter register code


foreach my $op (keys(%nodes)) {
	my %n = %{ $nodes{"$op"} };

	# skip this node description if no emit information is available
	next if (!defined($n{"emit"}));

	if ($n{"emit"} eq "") {
		$obst_register .= "\tbe_set_emitter(op_${arch}_${op}, be_emit_nothing);\n";
		next;
	}

	$obst_register .= "\tbe_set_emitter(op_${arch}_${op}, emit_${arch}_${op});\n";

	$obst_func .= "static void emit_${arch}_${op}(ir_node const *const node)\n";
	$obst_func .= "{\n";

	my @emit = split(/\n/, $n{"emit"});

	foreach my $template (@emit) {
		if ($template ne '') {
			$obst_func .= "\t${arch}_emitf(node, \"$template\");\n";
		}
	}

	$obst_func .= "}\n\n";
}

open(OUT, ">$target_h") || die("Could not open $target_h, reason: $!\n");

my $creation_time = localtime(time());

my $tmp = uc($arch);

print OUT<<EOF;
/**
 * \@file
 * \@brief Function prototypes for the emitter functions.
 * \@note  DO NOT EDIT THIS FILE, your changes will be lost.
 *        Edit $specfile instead.
 *        created by: $0 $specfile $target_dir
 * \@date  $creation_time
 */
#ifndef FIRM_BE_${tmp}_GEN_${tmp}_EMITTER_H
#define FIRM_BE_${tmp}_GEN_${tmp}_EMITTER_H

#include "irnode.h"
#include "${arch}_emitter.h"

void ${arch}_register_spec_emitters(void);

#endif

EOF

close(OUT);

open(OUT, ">$target_c") || die("Could not open $target_c, reason: $!\n");

$creation_time = localtime(time());

print OUT<<EOF;
/**
 * \@file
 * \@brief     Generated functions to emit code for assembler ir nodes.
 * \@note      DO NOT EDIT THIS FILE, your changes will be lost.
 *            Edit $specfile instead.
 *            created by: $0 $specfile $target_dir
 * \@date      $creation_time
 */
#include <stdio.h>
#include <assert.h>

#include "irnode.h"
#include "irop_t.h"
#include "irprog_t.h"
#include "beemitter.h"

#include "gen_${arch}_emitter.h"
#include "${arch}_new_nodes.h"
#include "${arch}_emitter.h"

$obst_func

/**
 * Enters the emitter functions for handled nodes into the generic
 * pointer of an opcode.
 */
void $arch\_register_spec_emitters(void)
{
$obst_register
}

EOF

close(OUT);
