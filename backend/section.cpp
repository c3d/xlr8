// ****************************************************************************
//  section.cpp                                                    XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Implementation of growable code/data sections
//
//
//
//
//
//
//
//
// ****************************************************************************
//   (C) 2026 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the terms outlined in LICENSE.txt
// ****************************************************************************
//   This file is part of XLR8.
//
//   XLR8 is free software: you can redistribute it and/or modify
//   it under the terms outlined in the LICENSE.txt file
//
//   XLR8 is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// ****************************************************************************

#include "section.h"


void section::realloc(size_t need)
// ----------------------------------------------------------------------------
//   Grow the section buffer to hold at least `need` bytes
// ----------------------------------------------------------------------------
{
    if (need <= data_allocated)
        return;
    size_t new_size = data_allocated ? data_allocated * 2 : 256;
    while (new_size < need)
        new_size *= 2;
    byte *new_data = (byte *) std::realloc(data, new_size);
    if (!new_data)
        return;
    data = new_data;
    data_allocated = new_size;
}


void section::append(byte c)
// ----------------------------------------------------------------------------
//   Append one byte at the end of the section
// ----------------------------------------------------------------------------
{
    if (data_offset + 1 > data_allocated)
        realloc(data_offset + 1);
    data[data_offset++] = c;
}


void section::append32(uint32_t c)
// ----------------------------------------------------------------------------
//   Append a 32-bit little-endian value
// ----------------------------------------------------------------------------
{
    if (data_offset + 4 > data_allocated)
        realloc(data_offset + 4);
    data[data_offset++] = byte(c);
    data[data_offset++] = byte(c >> 8);
    data[data_offset++] = byte(c >> 16);
    data[data_offset++] = byte(c >> 24);
}


void section::patch32(size_t at, uint32_t c)
// ----------------------------------------------------------------------------
//   Patch a 32-bit little-endian value at a given offset
// ----------------------------------------------------------------------------
{
    if (at + 4 > data_offset)
        return;
    data[at]     = byte(c);
    data[at + 1] = byte(c >> 8);
    data[at + 2] = byte(c >> 16);
    data[at + 3] = byte(c >> 24);
}
