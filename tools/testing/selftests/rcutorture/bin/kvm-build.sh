#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Build a kvm-ready Linux kernel from the tree in the current directory.
#
# Usage: kvm-build.sh config-template build-dir
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

config_template=${1}
if test -z "$config_template" -o ! -f "$config_template" -o ! -r "$config_template"
then
	echo "kvm-build.sh :$config_template: Not a readable file"
	exit 1
fi
builddir=${2}
if test -z "$builddir" -o ! -d "$builddir" -o ! -w "$builddir"
then
	echo "kvm-build.sh :$builddir: Not a writable directory, cannot build into it"
	exit 1
fi

T=/tmp/test-linux.sh.$$
trap 'rm -rf $T' 0
mkdir $T

cp ${config_template} $T/config
cat << ___EOF___ >> $T/config
CONFIG_INITRAMFS_SOURCE="$TORTURE_INITRD"
CONFIG_VIRTIO_PCI=y
CONFIG_VIRTIO_CONSOLE=y
___EOF___

configinit.sh $T/config O=$builddir
retval=$?
if test $retval -gt 1
then
	exit 2
fi
ncpus=`cpus2use.sh`
make O=$builddir -j$ncpus $TORTURE_KMAKE_ARG > $builddir/Make.out 2>&1
retval=$?
if test $retval -ne 0 || grep "rcu[^/]*": < $builddir/Make.out | egrep -q "Stop|Error|error:|warning:" || egrep -q "Stop|Error|error:" < $builddir/Make.out
then
	echo Kernel build error
	egrep "Stop|Error|error:|warning:" < $builddir/Make.out
	echo Run aborted.
	exit 3
fi
