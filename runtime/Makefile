#******************************************************************************
# Makefile<runtime>                                                xlr8 project
#******************************************************************************
#
#  File Description:
#
#    Dynamic runtime for XLR8
#
#    This runtime evaluates the dynamic bytecode
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

SOURCES=runtime.cpp scanner.cpp native-bridge.cpp native-run.cpp
PRODUCTS=xlr8.dll

INCLUDES=../recorder ../backend ../runtime
LIBRARIES=../recorder/recorder.dll ../backend/backend.lib

CXX_STD=gnu++17

MIQ=../make-it-quick/
include $(MIQ)rules.mk
