#ifndef LEB128_H
#define LEB128_H
// ****************************************************************************
//  leb128.h                                                      XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Operations on LEB128-encoded data
//
//
//
//
//
//
//
//
// ****************************************************************************
//   (C) 2022 Christophe de Dinechin <christophe@dinechin.org>
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

#include <cstddef>

template <typename Int = uint>
inline Int leb128(byte_p &b)
// ----------------------------------------------------------------------------
//   Return the leb128 value at pointer
// ----------------------------------------------------------------------------
{
    const bool is_signed = Int(~0ULL) < Int(0);
    Int        result    = 0;
    unsigned   shift     = 0;
    byte_p     bp        = b;
    do
    {
        result |= Int(*bp & 0x7F) << shift;
        shift += 7;
    } while (*bp++ & 0x80);

    if (is_signed && (bp[-1] & 0x40))
        result |= Int(~0ULL << (shift - 1));
    b = bp;
    return result;
}


template<typename Int = uint>
inline byte *leb128(byte *bp, Int value)
// ----------------------------------------------------------------------------
//   Write the LEB value at pointer
// ----------------------------------------------------------------------------
{
    const bool is_signed = Int(~0ULL) < Int(0);
    do
    {
        *bp++ = (value & 0x7F) | 0x80;
        value = Int(value >> 7);
    } while ((!is_signed && value != 0) ||
             (is_signed
              && (value != Int(~0ULL) || (~bp[-1] & 0x40))
              && (value != 0          || ( bp[-1] & 0x40))));
    bp[-1] &= ~0x80;
    return bp;
}


template<typename Int>
inline size_t leb128size(Int value)
// ----------------------------------------------------------------------------
//   Compute the size required for a given integer value
// ----------------------------------------------------------------------------
{
    const bool is_signed = Int(~0ULL) < Int(0);
    size_t     result    = 0;
    bool       signbit   = false;
    do
    {
        signbit = value & 0x40;
        value = Int(value >> 7);
        result++;
    } while ((!is_signed && value != 0) ||
             (is_signed
              && (value != Int(~0ULL)   || !signbit)
              && (value != 0            ||  signbit)));
    return result;
}


template<typename Data>
inline size_t leb128size(byte_p bp)
// ----------------------------------------------------------------------------
//   Compute the size of an LEB128 value at pointer
// ----------------------------------------------------------------------------
{
    byte_p s = bp;
    do { } while (*bp++ & 0x80);
    return bp - s;
}


template<typename Data>
inline Data *leb128skip(Data *ptr)
// ----------------------------------------------------------------------------
//   Skip LEB128 data
// ----------------------------------------------------------------------------
{
    const byte *p = (const byte *) ptr;
    while ((*p++) & 0x80);
    return (Data *) p;
}

#endif // LEB128_H
