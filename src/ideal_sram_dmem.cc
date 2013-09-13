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
 * Example MGSim-TCE simulator for machines with a single ideal SRAM
 * data memory.
 *
 * @author Pekka Jääskeläinen 2013 (pjaaskel-no.spam-cs.tut.fi)
 */

#include <cstdlib>

#include "tce_mgsim.hh"
// Memory system(s) to use:
#include "arch/mem/SerialMemory.h"

int main(int argc, char** argv) {

    MGSim mgsim(argv[1]);

    size_t memFreq = mgsim.cfg->getValue<size_t>("MemoryFreq");
    size_t coreFreq = mgsim.cfg->getValue<size_t>("CoreFreq");
    assert (memFreq == coreFreq);

    Simulator::Clock& clock = mgsim.k.CreateClock(memFreq);
    Simulator::Object root("", clock);

    MGSimTTACore tta(
        "core0", "minimal_with_stdout.adf", "hello.tpef",
        root, clock, *mgsim.cfg);
    
    Simulator::SerialMemory* smem =
        new Simulator::SerialMemory("data", root, clock, *mgsim.cfg);

    tta.replaceMemoryModel("data", *smem);

    MGSimDynamicLSU lsu("LSU", tta, *smem, *mgsim.cfg);

    smem->Initialize();
    
    mgsim.DoSteps(10000);

    return EXIT_SUCCESS;
}
