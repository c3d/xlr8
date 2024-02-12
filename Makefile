# ******************************************************************************
# Makefile                                                         XLR8 project
# ******************************************************************************
#
# File description:
#
#     The top-level makefile for XLR8, an extensible programming language
#     and runtime
#
#
#
#
#
#
#
# ******************************************************************************
# This software is licensed under the GNU General Public License v3+
# (C) 2015-2021, Christophe de Dinechin <christophe@dinechin.org>
# ******************************************************************************
# This file is part of XLR8
#
# XLR8 is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License,
# or (at your option) any later version.
#
# XLR8 is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with XLR8, in a file named COPYING.
# If not, see <https://www.gnu.org/licenses/>.
# ******************************************************************************

include version.mk
SUBDIRS=recorder bootstrap

MIQ=make-it-quick/
include $(MIQ)rules.mk
$(MIQ)rules.mk:
	$(PRINT_BUILD) git submodule update --init --recursive

# Do not run tests in the recorder
RECURSE_FLAGS_recorder=RUN_TESTS=

ref-% run-%:
	$(PRINT_COMMAND) cd tests; make $@
base rebase baseline:
	$(PRINT_COMMAND) cd tests; make $@
