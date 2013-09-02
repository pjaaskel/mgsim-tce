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
 * @file tce_mgsim.hh
 *
 * Wrappers and utilities for connecting TTA core simulation models to
 * MGSim simulations.
 *
 * @author Pekka Jääskeläinen 2013 (pjaaskel-no.spam-cs.tut.fi)
 */

#ifndef TCE_MGSIM_HH
#define TCE_MGSIM_HH

#include <arch/Memory.h>
#include <Operation.hh>
#include <SimpleSimulatorFrontend.hh>
#include <DetailedOperationSimulator.hh>
#include <ExecutingOperation.hh>
#include <SimValue.hh>

#include "sim/breakpoints.h"
#include "sim/config.h"
#include "sim/configparser.h"
#include "sim/readfile.h"
#include "sim/except.h"
#include "sim/kernel.h"

// Memory system(s) to use:
#include "arch/mem/SerialMemory.h"

/**
 * A wrapper for the TTA core simulation model.
 *
 * Controls the global glock signal and monitors the global lock
 * requrests from the load-store units. Propagates the clock advance
 * events to the core.
 *
 * @todo 
      - propagate cycle advance to the core engine except when
        the global lock signal is asserted by one of the outside
        facing FUs (LSUs usually)
      - if glock is asserted (at least one of the LSUs have the global lock
        request signal up), advance the cycle of only the outside
        facing FUs
 */
class MGSimTTACore : public SimpleSimulatorFrontend {
public:
};

/**
 * Interfaces between the memory system model of the MGSim and the operations
 * in a load-store unit of a TTA core.
 */
class MGSimDynamicLSU : 
    public Simulator::Object, public Simulator::IMemoryCallback, 
    public DetailedOperationSimulator {
public:
    MGSimDynamicLSU();
    virtual ~MGSimDynamicSU();
};

/**
 * A glue wrapper between the TTASim Memory interface and the MGSim
 * memory models.
 */
class MGSimMemory : public Memory, public Simulator::IMemory {
public:
    
};


/**
 * A wrapper for the MGSim simulation kernel.
 */
class MGSim {
private:
    ConfigMap overrides;
    std::vector<std::string> extras; // any additional strings that should be carried around by the Config class

public:
    MGSim(const char *conf);

    Config* cfg;
    Simulator::Kernel k;
    Simulator::BreakPointManager bps;

    void DoSteps(Simulator::CycleNo nCycles);

};

#endif
