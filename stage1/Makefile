#******************************************************************************
# Makefile<stage1>                                                 XLR8 project
#******************************************************************************
#
#  File Description:
#
#
#
#
#
#
#
#
#
#
#******************************************************************************
#  (C) 2024 Christophe de Dinechin <christophe@dinechin.org>
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

SOURCES=xl-stage1.cpp
PRODUCTS=xl-stage1.exe

xl-stage1.cpp: compiler.xl
	../xl-stage0 $< > $@

MIQ=../make-it-quick/
include $(MIQ)rules.mk
