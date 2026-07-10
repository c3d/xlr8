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

#include "../runtime/types.h"

#include <cstdlib>
#include <cstring>


struct section
// ----------------------------------------------------------------------------
//   A growable buffer for emitted code or data
// ----------------------------------------------------------------------------
{
    section(): data(), data_offset(), data_allocated() { }

    ~section()
    {
        std::free(data);
    }

    byte   *data;
    size_t  data_offset;
    size_t  data_allocated;

    size_t  allocated() const   { return data_allocated; }
    size_t  size() const        { return data_offset; }
    byte    operator[](size_t i) const { return data[i]; }

    void    realloc(size_t need);
    void    append(byte c);
    void    append32(uint32_t c);
    void    patch32(size_t at, uint32_t c);
};

#endif // SECTION_H
