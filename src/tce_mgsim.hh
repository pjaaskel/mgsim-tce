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

// TCE headers
#include <SimpleSimulatorFrontend.hh>
#include <DetailedOperationSimulator.hh>
#include <AddressSpace.hh>
#include <Memory.hh>

// MGSim headers
#include "arch/Memory.h"
#include "arch/VirtualMemory.h"
#include "sim/breakpoints.h"
#include "sim/config.h"
#include "sim/configparser.h"
#include "sim/readfile.h"
#include "sim/except.h"
#include "sim/kernel.h"

class MGSimDynamicLSU;
class MGSim;

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
class MGSimTTACore : 
    public Simulator::Object, 
    public SimpleSimulatorFrontend {
public:
    MGSimTTACore(
        const TCEString& coreName, const TCEString& adfFileName,
        const TCEString& initialProgram, Simulator::Object& parent, 
        Simulator::Clock& clock, Config& config);
    virtual ~MGSimTTACore();

    void replaceMemoryModel(
        const TCEString& addressSpaceName, 
        Simulator::IMemory& mgsimMem);

    bool isLockRequested() const;

    void setLockRequest() { lockRequests_++; }
    void unsetLockRequest() { lockRequests_--; }

    bool simulateInTandem(MGSim& mgim);

    // returns the total clock cycles, including stalls
    uint64_t clockCycleCount() const;

    void printStats(std::ostream* out=NULL) const;

    void addDynamicLSU(TCEString adfLSUName, MGSimDynamicLSU* lsu);

    Simulator::Process& clockAdvanceProcess() { return clockAdvanceProcess_; }

    // MGSim interface
    virtual Simulator::Result mgsimClockAdvance();

private:
    typedef std::vector<MGSimDynamicLSU*> LoadStoreUnitVec;
    Simulator::SingleFlag enabled_;
    Simulator::Process clockAdvanceProcess_;
    // Number of LSUs that have requested a lock.
    std::size_t lockRequests_;
    // The LSUs that interact with MGSim memory models. 
    LoadStoreUnitVec lsus_;
    Config& config_;
    // The previously simulated TTA cycle. To avoid simulating the
    // same cycle more than once.
    uint64_t lastSimulatedTTACycle_;
};

/**
 * Interfaces between the memory system model of the MGSim and the operations
 * in a load-store unit of a TTA core.
 */
class MGSimDynamicLSU : 
    public Simulator::Object, public Simulator::IMemoryCallback, 
    public DetailedOperationSimulator {
public:
    MGSimDynamicLSU(
        const TCEString& lsuName, 
        MGSimTTACore& parentTTA,
        Simulator::IMemory& mgsimMem,
        Config& config);
    virtual ~MGSimDynamicLSU();

    void tryIssuePending();
    void commitPending();
    bool needsLock() const;

    // TCE interface: simulate a step in the FU pipeline for an on-flight
    // operation.
    virtual bool simulateStage(ExecutingOperation& operation);

    // MGSim interface
    virtual bool OnMemoryReadCompleted(
        Simulator::MemAddr addr, const char* data);
    virtual bool OnMemoryWriteCompleted(Simulator::WClientID wid);
    virtual bool OnMemoryInvalidated(Simulator::MemAddr addr);
    virtual Simulator::Object& GetMemoryPeer();
    virtual Simulator::Result memoryPortCycle();

private:
    typedef std::deque<ExecutingOperation*> ExecutingOperationFIFO;
    Simulator::SingleFlag enabled_;
    Simulator::Process memoryPort_;
    // The MGSim Memory model accessed by this LSU.
    Simulator::IMemory& mgsimMemory_;    
    //Simulator::SingleFlag enabled_;
    //Simulator::Process memoryOutgoingProcess_;
    Simulator::MCID memClientID_;
    MGSimTTACore& parentTTA_;
    // The size of a data block transferred between the memory
    // and the LSU.
    size_t dataBusWidth_;
    // The operations that is not yet been committed to the 
    // memory system (e.g. due to arbitration conflicts).
    ExecutingOperation* pendingOperation_;
    ExecutingOperationFIFO incompleteOperations_;
    // If a result that does not arrive in the architectural latency
    // is detected, this is set to true until the result arrives.
    bool lateResult_;
};

/**
 * A glue wrapper between the TTASim Memory interface and the MGSim
 * memory models.
 */
class MGSimTCEMemory : public Memory {
public:
    MGSimTCEMemory(
        const TTAMachine::AddressSpace& as,
        Simulator::IMemory& mgsimMemory);
    virtual ~MGSimTCEMemory();

    /// The TCE side write/read methods.
    virtual Memory::MAU read(Word address);
    virtual void write(Word address, Memory::MAU data);

private:
    Simulator::IMemory& mgsimMemory_;
};


/**
 * A wrapper for the MGSim simulation kernel.
 *
 * @todo rename to MGSimTCE
 */
class MGSim {
public:
    MGSim(const char *conf);

    Config* cfg;
    Simulator::Kernel k;
    Simulator::BreakPointManager bps;

    void DoSteps(Simulator::CycleNo nCycles);

private:
    ConfigMap overrides;
    // any additional strings that should be carried around by the Config class
    std::vector<std::string> extras; 

};

#endif
