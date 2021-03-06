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
 * @file ideal_sram_dmem.cc
 *
 * Example MGSim-TCE simulator for machines with a single ideal parallel
 * SRAM data memory with two ports accessed by two LSUs.
 *
 * @author Pekka Jääskeläinen 2013 (pjaaskel-no.spam-cs.tut.fi)
 */

// Define this in order to compare the simulation states in lock
// step to the TCE's core-only simulation. Requires all TCE
// headers (run tools/scripts/install_all_headers in TCE tree).
//#define TANDEM_VERIFICATION

#include <cstdlib>

#include "tce_mgsim.hh"
// Memory system(s) to use:
#include "arch/mem/ParallelMemory.h"

#ifdef TANDEM_VERIFICATION
#include <tce/SimulatorFrontend.hh>
#endif

int main(int argc, char** argv) {

    MGSim mgsim(argv[1]);

    size_t memFreq = mgsim.cfg->getValue<size_t>("MemoryFreq");

    Simulator::Clock& clock = mgsim.k.CreateClock(memFreq);
    Simulator::Object root("", clock);

    MGSimTTACore tta(
        "core0", "dual-lsu.adf", "hello-2lsu.tpef",
        root, clock, *mgsim.cfg);
    
    Simulator::ParallelMemory* smem =
        new Simulator::ParallelMemory("data", root, clock, *mgsim.cfg);

    tta.replaceMemoryModel("data", *smem);

    smem->Initialize();

    while (!tta.isFinished())
        mgsim.DoSteps(1);
    
    tta.printStats();

    return EXIT_SUCCESS;
}
