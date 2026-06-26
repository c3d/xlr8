#ifndef SAVE_H
#define SAVE_H
// ****************************************************************************
//  save.h                                                         XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     A simple helper class using RTTI to save/restore a state
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

template <typename value_type>
struct save
// ----------------------------------------------------------------------------
//   Save a value and reset it to what it was on scope exit
// ----------------------------------------------------------------------------
{
    save(value_type &ref, value_type value): ref(ref), saved(ref)
    {
        ref = value;
    }
    ~save()
    {
        ref = saved;
    }
    value_type  &ref;
    value_type  saved;
};

#endif // SAVE_H
