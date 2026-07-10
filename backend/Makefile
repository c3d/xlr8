#******************************************************************************
# Makefile<backend>                                                XLR8 project
#******************************************************************************
#
#  File Description:
#
#    Native code generation backend for XLR8
#
#
#
#
#
#
#******************************************************************************
#  (C) 2026 Christophe de Dinechin <christophe@dinechin.org>
#  This software is licensed under the terms outlined in LICENSE.txt
#******************************************************************************
#  This file is part of XLR8.
#
#  XLR8 is free software: you can redistribute it and/or modify
#  it under the terms outlined in the LICENSE.txt file
#
#  XLR8 is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#******************************************************************************

SOURCES=arch.cpp backend.cpp section.cpp \
        i386.cpp x86_64.cpp arm64.cpp riscv64.cpp
PRODUCTS=backend.lib

INCLUDES=../runtime

CXX_STD=gnu++17

MIQ=../make-it-quick/
include $(MIQ)rules.mk
