# ******************************************************************************
# Makefile                                                            XL project
# ******************************************************************************
#
# File description:
#
#    Makefile for the XL runtime environment
#
#
#
#
#
#
#
#
# ******************************************************************************
# This software is licensed under the GNU General Public License v3
# (C) 2024, Christophe de Dinechin <christophe@dinechin.org>
# ******************************************************************************
# This file is part of XL
#
# XL is free software: you can r redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# XL is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with XL, in a file named COPYING.
# If not, see <https://www.gnu.org/licenses/>.
# ******************************************************************************

include ../version.mk

# Set to 'none' to disable LLVM
COMPILER=none

# Set expand to expand the bytecode for debuggability
EXPAND=

# List of source files to process
SOURCES=					\
	../recorder/recorder.c			\
	../recorder/recorder_ring.c		\
	ast.cpp					\
	cdecls.cpp				\
	context.cpp				\
	errors.cpp				\
	gc.cpp					\
	interpreter.cpp				\
	main.cpp				\
	module.cpp				\
	native.cpp				\
	options.cpp				\
	parser.cpp				\
	renderer.cpp				\
	runtime.cpp				\
	scanner.cpp				\
	syntax.cpp				\
	utf8_fileutils.cpp			\
	xl.cpp

HEADERS=$(HEADERS_$(VARIANT))
HEADERS_lib=$(wildcard ../include/*.h *.tbl)

PRODUCTS=xl-stage0.exe

GIT_VERSION=	$(shell git describe --always --tags --dirty=-dirty)
DEFINES=	$(DEFINES_$(COMPILER))					\
		XL_VERSION='"$(PACKAGE_VERSION)-$(GIT_VERSION)"'	\
		XL_COMPATIBLE_VERSION='"$(PACKAGE_VERSION)"'

INCLUDES=. .. ../include


CONFIG= sigaction                       \
        struct_stat                     \
        posix_memalign                  \
        mingw_aligned_malloc            \
        <regex.h>                       \
        <sys/mman.h>                    \
        <sys/socket.h>                  \
        libregex                        \
        drand48                         \
        glob                            \
        uchar ushort uint ulong longlong

CXX_STD=c++14

MIQ=../make-it-quick/
include $(MIQ)rules.mk
