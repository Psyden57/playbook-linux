#!/bin/sh
# Set up both cross-toolchains for this repository.
#
#   . tools/env.sh            # or: source tools/env.sh
#   tools/env.sh --check      # validate, print versions, change nothing
#
# The toolchains are NOT part of the repository: they live in the gitignored
# payload as
#
#   local/toolchains/armv7-eabihf     ARMv7 hard-float (Buildroot, gcc 14.3.0)
#   local/toolchains/qnx660-master    QNX SDP 6.6 (as/ld/objcopy/gcc)
#
# Override either location with PB_ARM_TOOLCHAIN / QNX660 (QNX_HOST is derived
# from QNX660 when it is set).  Nothing outside this workspace is required; the
# old …/wsl-migration copies are not referenced.
#
# Sourcing sets PATH, CROSS_COMPILE, QNX660, QNX_HOST and QNX_TARGET.
# `--check` does not modify the environment; it exits non-zero if either
# toolchain is missing or unusable.

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
if [ -n "${QNX660:-}" ]; then
	_env_qnx=$QNX660
else
	_env_qnx=$_env_root/local/toolchains/qnx660-master
fi
_env_qnx_host=$_env_qnx/host/linux/x86
_env_qnx_target=$_env_qnx/target/qnx6

_env_arm_gcc=$_env_arm/bin/arm-linux-gcc
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
	exit 0
fi

if [ -n "$_env_missing" ]; then
	echo "env.sh: missing:$_env_missing" >&2
	echo "env.sh: copy the toolchains to local/toolchains/ or set" >&2
	echo "env.sh: PB_ARM_TOOLCHAIN / QNX660." >&2
	unset _d _env_root _env_arm _env_qnx _env_qnx_host _env_qnx_target \
	      _env_arm_gcc _env_qnx_gcc _env_missing
	return 1 2>/dev/null || exit 1
fi

# ARM cross tools first; the QNX host bin LAST so its GNU make 3.82 does not
# shadow the host's make (the kernel needs >= 4.0).  The image builder invokes
# the QNX as/ld/objcopy by absolute path, so they need no PATH entry.
PATH=$_env_arm/bin:$PATH:$_env_qnx_host/usr/bin
export PATH
export CROSS_COMPILE=arm-linux-
export QNX660=$_env_qnx
export QNX_HOST=$_env_qnx_host
export QNX_TARGET=$_env_qnx_target

unset _d _env_root _env_arm _env_qnx _env_qnx_host _env_qnx_target \
      _env_arm_gcc _env_qnx_gcc _env_missing
