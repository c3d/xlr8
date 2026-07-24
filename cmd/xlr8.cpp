// ****************************************************************************
//  xlr8.cpp                                                       XLR8 project
// ****************************************************************************
//
//   File Description:
//
//     Command-line for XLR8
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

#include "recorder.h"
#include "runtime.h"
#include "scanner.h"
#include "types.h"
#include "save.h"

#include <iostream>
#include <fstream>


RECORDER(main,          16, "XLR8 main command-line options");
RECORDER(main_error,    16, "XLR8 main command-line options");


void help(char *progname)
// ----------------------------------------------------------------------------
//   Display the help for the command
// ----------------------------------------------------------------------------
{
    printf("Usage: %s [options...] [files..]\n", progname);
    printf("  -h : Display this help\n"
           "  -m : Select memory size\n"
           "  -O : Compile bytecode to native code and run on host CPU\n"
           "  -t : Select recoder traces\n");
}


static bool debug_message(runtime &rt)
// ----------------------------------------------------------------------------
//   This is an example opcode extension
// ----------------------------------------------------------------------------
{
    byte_p text = rt.pc;
    while (char c = *text++)
        std::cout << c;
    rt.pc = text;
    return true;
}


static bool run(runtime &rt, std::istream &input, bool optimize)
// ----------------------------------------------------------------------------
//   Run a given input stream
// ----------------------------------------------------------------------------
{
    scanner s(input, rt);
    scanner *olds = rt.scanning(&s);
    bool     ok   = rt.enter(rt.CALL_CLOBBERED_REGS);
    while (ok && input.good())
    {
        save frame(rt.frame, rt.frame);
        ok = s.scan();
        if (ok)
        {
            rt.iosetup(0, &input);
            rt.iosetup(1, &std::cout);
            rt.iosetup(2, &std::cerr);
            if (ok)
            {
                if (optimize)
                    ok = rt.run_native(s.pgmid);
                else
                    ok = rt.run();
            }
        }
    }
    rt.scanning(olds);
    if (ok)
        ok = rt.leave();
    if (!ok)
        record(main_error, "%s", rt.error());
    return ok;
}


int main(int argc, char **argv)
// ----------------------------------------------------------------------------
//   Parse the command line and run the compiler phases
// ----------------------------------------------------------------------------
{
    size_t memory = 32 * 1024;

    recorder_trace_set(".*(error|warn(ing)?)s?");
    if (cstring traces = getenv("XLR8_TRACES"))
        recorder_trace_set(traces);
    recorder_dump_on_common_signals(0, 0);

    int rtargs = 0;
    byte debug_opcode = 0;
    bool optimize = false;
    runtime::argument_list files;
    for (int a = 1; a < argc && !rtargs; a++)
    {
        cstring arg = argv[a];
        if (arg[0] == '-')
        {
            switch (arg[1])
            {
            case 'B':
                arg = arg[2] ? arg+2 : a+1 < argc ? argv[++a] : "";
                runtime::dump_file = arg;
                break;
            case 'd':
                arg = arg[2] ? arg+2 : a+1 < argc ? argv[++a] : "";
                debug_opcode = atoi(arg);
                break;

            default:
            case 'h':
                help(argv[0]);
                exit(0);
                break;
            case 'm':
                arg = arg[2] ? arg+2 : a+1 < argc ? argv[++a] : "";
                memory = atoi(arg) * 1024;
                break;
            case 'O':
                optimize = true;
                break;
            case 't':
                arg = arg[2] ? arg+2 : a+1 < argc ? argv[++a] : "";
                recorder_trace_set(arg);
                break;
            case '-':
                rtargs = a + 1;
                break;
            case 0:
                files.push_back(arg);
                break;
            }
        }
        else
        {
            files.push_back(arg);
        }
    }

    record(main, "XLR8 Compiler version %+s starting (%u opcodes, %zuK)",
           XLR8_VERSION, OP_COUNT, memory / 1024);
    runtime rt(memory, argc, argv);
    bool ok = true;
    if (debug_opcode)
        rt.user_opcode(debug_opcode, debug_message);
    for (auto file : files)
    {
        if (file == "-")
        {
            // Read from standard input
            ok = run(rt, std::cin, optimize);
        }
        else
        {
            // Read from a file
            std::ifstream input(file);
            ok = run(rt, input, optimize);
        }
    }

    record(main, "Compiler exit %+s", ok ? "OK" : "ERROR");

    if (RECORDER_TWEAK(main_error) > 1)
        recorder_dump();

    return ok ? 0 : -1;
}
