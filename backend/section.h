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
    void append8(byte c)
    // ------------------------------------------------------------------------
    //   Append one byte at the end of the section
    // ------------------------------------------------------------------------
    {
        push_back(c);
    }

    void patch8(hwcode_t at, byte c)
    // ------------------------------------------------------------------------
    //   Patch a single byte at a given offset
    // ------------------------------------------------------------------------
    {
        if (at >= size())
            return;
        section &s = *this;
        s[at] = c;
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

    void patch32le(hwcode_t at, uint32_t c)
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

    uint32_t value32le(hwcode_t at)
    // ------------------------------------------------------------------------
    //   Return the existing little-endian 32-bit value stored here
    // ------------------------------------------------------------------------
    {
        if (at + 4 > size())
            return uint32_t(0xDEADBEEF);
        section &s = *this;
        return
            uint32_t(s[at + 0]) << 0  |
            uint32_t(s[at + 1]) << 8  |
            uint32_t(s[at + 2]) << 16 |
            uint32_t(s[at + 3]) << 24;
    }

    void append32be(uint32_t c)
    // ------------------------------------------------------------------------
    //   Append a 32-bit big-endian value
    // ------------------------------------------------------------------------
    {
        push_back(byte(c >> 24));
        push_back(byte(c >> 16));
        push_back(byte(c >> 8));
        push_back(byte(c >> 0));
    }

    void patch32be(hwcode_t at, uint32_t c)
    // ------------------------------------------------------------------------
    //   Patch a 32-bit big-endian value at a given offset
    // ------------------------------------------------------------------------
    {
        if (at + 4 > size())
            return;
        section &s = *this;
        s[at]      = byte(c >> 24);
        s[at + 1]  = byte(c >> 16);
        s[at + 2]  = byte(c >> 8);
        s[at + 3]  = byte(c >> 0);
    }

    uint32_t value32be(hwcode_t at)
    // ------------------------------------------------------------------------
    //   Return the existing big-endian 32-bit value stored here
    // ------------------------------------------------------------------------
    {
        if (at + 4 > size())
            return uint32_t(0xDEADBEEF);
        section &s = *this;
        return
            uint32_t(s[at + 0]) << 24 |
            uint32_t(s[at + 1]) << 16 |
            uint32_t(s[at + 2]) << 8  |
            uint32_t(s[at + 3]) << 0;
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
