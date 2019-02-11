#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Check the console output from an rcutorture run for oopses.
# The "file" is a pathname on the local system, and "title" is
# a text string for error-message purposes.
#
# Usage: parse-console.sh file title
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

file="$1"
title="$2"

. functions.sh

if grep -Pq '\x00' < $file
then
	print_warning Console output contains nul bytes, old qemu still running?
fi
egrep 'Badness|WARNING:|Warn|BUG|===========|Call Trace:|Oops:|detected stalls on CPUs/tasks:|self-detected stall on CPU|Stall ended before state dump start|\?\?\? Writer stall state|rcu_.*kthread starved for' < $file | grep -v 'ODEBUG: ' | grep -v 'Warning: unable to open an initial console' > $1.diags
if test -s $1.diags
then
	print_warning Assertion failure in $file $title
	# cat $1.diags
	summary=""
	n_badness=`grep -c Badness $1`
	if test "$n_badness" -ne 0
	then
		summary="$summary  Badness: $n_badness"
	fi
	n_warn=`grep -v 'Warning: unable to open an initial console' $1 | egrep -c 'WARNING:|Warn'`
	if test "$n_warn" -ne 0
	then
		summary="$summary  Warnings: $n_warn"
	fi
	n_bugs=`egrep -c 'BUG|Oops:' $1`
	if test "$n_bugs" -ne 0
	then
		summary="$summary  Bugs: $n_bugs"
	fi
	n_calltrace=`grep -c 'Call Trace:' $1`
	if test "$n_calltrace" -ne 0
	then
		summary="$summary  Call Traces: $n_calltrace"
	fi
	n_lockdep=`grep -c =========== $1`
	if test "$n_badness" -ne 0
	then
		summary="$summary  lockdep: $n_badness"
	fi
	n_stalls=`egrep -c 'detected stalls on CPUs/tasks:|self-detected stall on CPU|Stall ended before state dump start|\?\?\? Writer stall state' $1`
	if test "$n_stalls" -ne 0
	then
		summary="$summary  Stalls: $n_stalls"
	fi
	n_starves=`grep -c 'rcu_.*kthread starved for' $1`
	if test "$n_starves" -ne 0
	then
		summary="$summary  Starves: $n_starves"
	fi
	print_warning Summary: $summary
else
	rm $1.diags
fi
