/*
    Copyright (c) 2002-2013 Tampere University of Technology.

    This file is part of TTA-Based Codesign Environment (TCE).

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
 */

/**
 * @file dram_dmem.cc
 *
 * Example MGSim-TCE simulator for machines with a single DRAM
 * data memory.
 *
 * @author Pekka Jääskeläinen 2013 (pjaaskel-no.spam-cs.tut.fi)
 */

// Define this in order to compare the simulation states in lock
// step to the TCE's core-only simulation. 
//#define TANDEM_VERIFICATION

#include <cstdlib>

#include "tce_mgsim.hh"
// Memory system(s) to use:
#include "arch/mem/DDRMemory.h"

int main(int argc, char** argv) {

    MGSim mgsim(argv[1]);

    // for some reason these should be the same, the DDR's
    // frequency should be controlled in the :Freq of the
    // channel configuration
    size_t memFreq, coreFreq;
    memFreq = coreFreq = mgsim.cfg->getValue<size_t>("CoreFreq");

    Simulator::Clock& coreClock = mgsim.k.CreateClock(coreFreq);
    Simulator::Clock& memClock = mgsim.k.CreateClock(memFreq);

    Simulator::Object root("", coreClock);

    MGSimTTACore tta(
        "core0", "minimal_with_stdout.adf", "hello.tpef",
        root, coreClock, *mgsim.cfg);
    
    Simulator::DDRMemory* ddrmem =
        new Simulator::DDRMemory(
            "data", root, memClock, *mgsim.cfg, "DIRECT");

    tta.replaceMemoryModel("data", *ddrmem);

    ddrmem->Initialize();
    
#ifdef TANDEM_VERIFICATION    
    tta.simulateInTandem(mgsim);
#else
    while (!tta.isFinished())
        mgsim.DoSteps(1);
#endif
    
    tta.printStats();

    return EXIT_SUCCESS;
}
