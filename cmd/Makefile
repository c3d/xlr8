#******************************************************************************
# Makefile<cmd>                                                   xlr8 project
#******************************************************************************
#
#  File Description:
#
#    Command-line program for XLR8
#
#    This configures the runtime to load a file and run it
#
#
#
#
#
#
#******************************************************************************
#  (C) 2025 Christophe de Dinechin <christophe@dinechin.org>
#  This software is licensed under the terms outlined in LICENSE.txt
#******************************************************************************
#  This file is part of xlr8.
#
#  xlr8 is free software: you can redistribute it and/or modify
#  it under the terms outlined in the LICENSE.txt file
#
#  xlr8 is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#******************************************************************************

SOURCES=xlr8.cpp
PRODUCTS=xlr8.exe

INCLUDES=../runtime ../backend ../recorder
LIBRARIES=../runtime/xlr8.dll ../recorder/recorder.dll

VERSION:=$(shell git describe 2> /dev/null || echo "unkown-version")
DEFINES=XLR8_VERSION='"$(VERSION)"'

CXX_STD=gnu++17

MIQ=../make-it-quick/
include $(MIQ)rules.mk
