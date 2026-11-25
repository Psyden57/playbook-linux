#!/bin/sh
# Set up the cross-toolchains for this repository.
#
#   . tools/env.sh            # or: source tools/env.sh
#   tools/env.sh --check      # validate, print versions, change nothing
#
# The toolchains are NOT part of the repository: they live in the gitignored
# payload as
#
#   local/toolchains/armv7-eabihf      ARMv7 hard-float (Buildroot, gcc 14.3.0)
#   local/toolchains/armv7-musl-eabihf ARMv7 musl (musl.cc, gcc 11.2.1) — OPTIONAL
#   local/toolchains/qnx660-master     QNX SDP 6.6 (as/ld/objcopy/gcc)
#
# The two glibc/QNX toolchains are required.  The musl one is optional and only
# needed to build binaries that run on the unit's musl root (e.g.
# tools/bench/gpu/dbmfix.c, and the session-69 probes in tools/bench/egl/); the
# ARM toolchain above is glibc and its output will not load there.  Get it from
# https://musl.cc/arm-linux-musleabihf-cross.tgz and unpack it so the compiler
# lands at local/toolchains/armv7-musl-eabihf/bin/arm-linux-musleabihf-gcc.
#
# Override any location with PB_ARM_TOOLCHAIN / PB_MUSL_TOOLCHAIN / QNX660
# (QNX_HOST is derived from QNX660 when it is set).  Nothing outside this
# workspace is required; the old …/wsl-migration copies are not referenced.
#
# Sourcing sets PATH, CROSS_COMPILE, QNX660, QNX_HOST and QNX_TARGET, plus
# MUSL_CC when the musl toolchain is present.
# `--check` does not modify the environment; it exits non-zero if either
# REQUIRED toolchain is missing or unusable (a missing musl one is only a note).

# A sourced script cannot rely on $0 (that is the shell).  Resolve the
# repository from BASH_SOURCE when available, else from this file's path when
# executed, else by walking up from the current directory to the .git marker.
_env_find_root() {
	if [ -n "${BASH_SOURCE:-}" ] && [ -f "${BASH_SOURCE}" ]; then
		CDPATH= cd -- "$(dirname -- "${BASH_SOURCE}")/.." && pwd
		return
	fi
	if [ -f "$0" ] && [ "${0##*/}" = "env.sh" ]; then
		CDPATH= cd -- "$(dirname -- "$0")/.." && pwd
		return
	fi
	_d=$PWD
	while [ "$_d" != "/" ] && [ -n "$_d" ]; do
		if [ -d "$_d/.git" ] || [ -f "$_d/tools/env.sh" ]; then
			echo "$_d"
			return
		fi
		_d=$(dirname -- "$_d")
	done
	echo "$PWD"
}

_env_root=$(_env_find_root)
_env_arm=${PB_ARM_TOOLCHAIN:-$_env_root/local/toolchains/armv7-eabihf}
_env_musl=${PB_MUSL_TOOLCHAIN:-$_env_root/local/toolchains/armv7-musl-eabihf}
if [ -n "${QNX660:-}" ]; then
	_env_qnx=$QNX660
else
	_env_qnx=$_env_root/local/toolchains/qnx660-master
fi
_env_qnx_host=$_env_qnx/host/linux/x86
_env_qnx_target=$_env_qnx/target/qnx6

_env_arm_gcc=$_env_arm/bin/arm-linux-gcc
_env_musl_gcc=$_env_musl/bin/arm-linux-musleabihf-gcc
_env_qnx_gcc=$_env_qnx_host/usr/bin/arm-unknown-nto-qnx6.6.0eabi-gcc

_env_missing=
[ -x "$_env_arm_gcc" ] || _env_missing="$_env_missing ARM($_env_arm)"
[ -x "$_env_qnx_gcc" ] || _env_missing="$_env_missing QNX($_env_qnx)"

if [ "${1:-}" = "--check" ]; then
	if [ -n "$_env_missing" ]; then
		echo "env.sh: missing:$_env_missing" >&2
		echo "env.sh: copy the toolchains to local/toolchains/ or set" >&2
		echo "env.sh: PB_ARM_TOOLCHAIN / QNX660." >&2
		exit 1
	fi
	echo "repo: $_env_root"
	echo "ARM toolchain: $_env_arm"
	"$_env_arm_gcc" --version | head -1
	echo "QNX toolchain: $_env_qnx"
	QNX_HOST=$_env_qnx_host QNX_TARGET=$_env_qnx_target "$_env_qnx_gcc" --version 2>&1 | head -1
	if [ -x "$_env_musl_gcc" ]; then
		echo "musl toolchain: $_env_musl"
		"$_env_musl_gcc" --version | head -1
	else
		echo "musl toolchain: absent (optional — needed for unit-side binaries)"
	fi
	exit 0
fi

if [ -n "$_env_missing" ]; then
	echo "env.sh: missing:$_env_missing" >&2
	echo "env.sh: copy the toolchains to local/toolchains/ or set" >&2
	echo "env.sh: PB_ARM_TOOLCHAIN / QNX660." >&2
	unset _d _env_root _env_arm _env_musl _env_qnx _env_qnx_host _env_qnx_target \
	      _env_arm_gcc _env_musl_gcc _env_qnx_gcc _env_missing
	return 1 2>/dev/null || exit 1
fi

# ARM cross tools first; the QNX host bin LAST so its GNU make 3.82 does not
# shadow the host's make (the kernel needs >= 4.0).  The image builder invokes
# the QNX as/ld/objcopy by absolute path, so they need no PATH entry.
# The musl toolchain is deliberately NOT on PATH (it is not the default
# compiler); use MUSL_CC or its absolute path.
PATH=$_env_arm/bin:$PATH:$_env_qnx_host/usr/bin
export PATH
export CROSS_COMPILE=arm-linux-
if [ -x "$_env_musl_gcc" ]; then
	MUSL_CC=$_env_musl_gcc
	export MUSL_CC
fi
export QNX660=$_env_qnx
export QNX_HOST=$_env_qnx_host
export QNX_TARGET=$_env_qnx_target

unset _d _env_root _env_arm _env_musl _env_qnx _env_qnx_host _env_qnx_target \
      _env_arm_gcc _env_musl_gcc _env_qnx_gcc _env_missing
