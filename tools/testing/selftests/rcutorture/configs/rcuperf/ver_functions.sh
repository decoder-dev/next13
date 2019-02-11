#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Torture-suite-dependent shell functions for the rest of the scripts.
#
# Copyright (C) IBM Corporation, 2015
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

# rcuperf_param_nreaders bootparam-string
#
# Adds nreaders rcuperf module parameter if not already specified.
rcuperf_param_nreaders () {
	if ! echo "$1" | grep -q "rcuperf.nreaders"
	then
		echo rcuperf.nreaders=-1
	fi
}

# rcuperf_param_nwriters bootparam-string
#
# Adds nwriters rcuperf module parameter if not already specified.
rcuperf_param_nwriters () {
	if ! echo "$1" | grep -q "rcuperf.nwriters"
	then
		echo rcuperf.nwriters=-1
	fi
}

# per_version_boot_params bootparam-string config-file seconds
#
# Adds per-version torture-module parameters to kernels supporting them.
per_version_boot_params () {
	echo $1 `rcuperf_param_nreaders "$1"` \
		`rcuperf_param_nwriters "$1"` \
		rcuperf.perf_runnable=1 \
		rcuperf.shutdown=1 \
		rcuperf.verbose=1
}
