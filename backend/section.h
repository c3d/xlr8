#ifndef SECTION_H
#define SECTION_H
// ****************************************************************************
//  section.h                                                      XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     A growable byte buffer for emitted machine code, inspired by TCC's
//     Section type.
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

#include "types.h"

#include <cstdint>
#include <vector>


struct section : std::vector<byte>
// ----------------------------------------------------------------------------
//   A growable buffer for emitted code or data
// ----------------------------------------------------------------------------
{
    void append(byte c)
    // ------------------------------------------------------------------------
    //   Append one byte at the end of the section
    // ------------------------------------------------------------------------
    {
        push_back(c);
    }

    void append32le(uint32_t c)
    // ------------------------------------------------------------------------
    //   Append a 32-bit little-endian value
    // ------------------------------------------------------------------------
    {
        push_back(byte(c));
        push_back(byte(c >> 8));
        push_back(byte(c >> 16));
        push_back(byte(c >> 24));
    }

    void patch32(size_t at, uint32_t c)
    // ------------------------------------------------------------------------
    //   Patch a 32-bit little-endian value at a given offset
    // ------------------------------------------------------------------------
    {
        if (at + 4 > size())
            return;
        section &s = *this;
        s[at]      = byte(c);
        s[at + 1]  = byte(c >> 8);
        s[at + 2]  = byte(c >> 16);
        s[at + 3]  = byte(c >> 24);
    }

    void patch8(size_t at, byte c)
    // ------------------------------------------------------------------------
    //   Patch a single byte at a given offset
    // ------------------------------------------------------------------------
    {
        if (at >= size())
            return;
        section &s = *this;
        s[at] = c;
    }

    size_t allocated() const
    // ------------------------------------------------------------------------
    //   Return reserved capacity (TinyCC-compatible name)
    // ------------------------------------------------------------------------
    {
        return capacity();
    }
};

#endif // SECTION_H
