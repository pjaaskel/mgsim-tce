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

#include "tce_mgsim.hh"
#include "Application.hh"
#include "Machine.hh"
#include "MemorySystem.hh"

/**
   Division of responsiblities to implement the correct lock timings
   with the MGSim model.

   Global lock is requested by a load-store unit (LSU) in case of
   a) arbitration conflict: cannot issue the access at all
   due to contention somewhere in the memory system
   b) dynamic latencies: the result could not be received
   within the static architectural latency of the memory
   operation for the LSU. Need to stall until the result is
   received and ready to be placed to the output register.

   We need to receive the memory access requests from all
   LSUs at the current cycle (in fact, from all LSUs of
   all TTAs, if there's more than one) before we can decide 
   whether to lock the core at the next cycle. Thus, the TTA core 
   model can collect the new requests and place new results to the
   output ports (advance the cycle of the core) if the lock is not 
   requested. Then the LSU model can place new requests to the pipeline 
   model. Let the TTA core to actually issue the requests so it gets 
   to know and update the locking status. 

   It can implement the dynamic latency locking by checking
   if there is at least one result in the pipeline which has
   not arrived yet after the architectural latency. It can
   notice it from the latency counter of the DetailedOperationSimulator
   model of an inflight operation reaching zero. Note: the architectural 
   latency counter should be updated only if there is no global lock as 
   it  reflects the instruction latency seen by the programmer, 
   not the actual clock cycles spent to serve the request.
   This should occur automatically if we do not propagate
   clock to the core in case there is at least one glock
   requesting LSU.

   How to model the glocks? Should the LSU model need to
   know about locks at all or should the TTA core model
   take care all of it? Could be doable that way if the
   TTA core model gets access to all in flight operations
   and sees their latency counters.
 */


////// MGSimTTACore //////////////////////////////////////////////////////
MGSimTTACore::MGSimTTACore(
    const TCEString& coreName, const TCEString& adfFileName,
    const TCEString& initialProgram, Simulator::Object& parent, 
    Simulator::Clock& clock) :
    Simulator::Object(TCEString("tce.") + coreName, parent, clock), 
    SimpleSimulatorFrontend(adfFileName, initialProgram) {
}

MGSimTTACore::~MGSimTTACore() {
}

void
MGSimTTACore::replaceMemoryModel(
    const TCEString& addressSpaceName, 
    Simulator::IMemory& mgsimMem) {

    MemorySystem& memSystem = memorySystem();

    const TTAMachine::AddressSpace& as = 
        memSystem.addressSpace(addressSpaceName);
    MGSimTCEMemory* memWrapper = new MGSimTCEMemory(as, mgsimMem);

    memSystem.addAddressSpace(
        as, MemorySystem::MemoryPtr(memWrapper), true);
}

////// MGSimDynamicLSU ///////////////////////////////////////////////////
MGSimDynamicLSU::MGSimDynamicLSU(
    const TCEString& lsuName, 
    MGSimTTACore& parentTTA,
    Simulator::IMemory& mgsimMem,
    Config& config) : 
    Simulator::Object(
        parentTTA.GetName() + "." + lsuName,
        parentTTA, parentTTA.GetClock()),
    mgsimMemory_(mgsimMem), 
    enabled_("b_enabled", *this, GetClock(), true),
    memoryOutgoingProcess_(
        *this, "semd-memory-requests", 
        Simulator::delegate::create<
            MGSimDynamicLSU, &MGSimDynamicLSU::mgsimCycleAdvance>(*this))
                                    
{
    /* TODO: register as a user of the memory */
    parentTTA.setOperationSimulator(lsuName, *this);
    enabled_.Sensitive(memoryOutgoingProcess_);
    config.registerObject(*this, GetName());

    Simulator::StorageTraceSet traces; 
    Simulator::StorageTraceSet st; 
    memClientID_ = 
        mgsimMemory_.RegisterClient(
            *this, memoryOutgoingProcess_, traces, st, true);
    memoryOutgoingProcess_.SetStorageTraces(opt(traces));

}

/**
 * A cycle advance method called from MGSim (three times per cycle?).
 *
 * TODO:
 - Check pending memory accesses in the pipeline and issue them to
   memory, collect results.
 - If issue fails (an arbitration conflict), request lock from the
   parent.
 - How to collect the memory operation issued at the current cycle
   first before calling the mgsimCycleAdvance?
 */
Simulator::Result
MGSimDynamicLSU::mgsimCycleAdvance() {
    PRINT_VAR(GetKernel()->GetCycleNo());
    return Simulator::SUCCESS;
}

MGSimDynamicLSU::~MGSimDynamicLSU() {
}

/**
 * This is called only a a non-locked cycle of TTA.
 *
 * TODO:
 
 * This will place the results from the pending access requests
 * queue to t
 */
bool
MGSimDynamicLSU::simulateStage(ExecutingOperation& operation) {
    abortWithError("Unimplemented.");
}

bool
MGSimDynamicLSU::OnMemoryReadCompleted(
    Simulator::MemAddr addr, const char* data) {
    abortWithError("Unimplemented.");
}

bool
MGSimDynamicLSU::OnMemoryWriteCompleted(Simulator::WClientID wid) {
    abortWithError("Unimplemented.");
}

bool
MGSimDynamicLSU::OnMemoryInvalidated(Simulator::MemAddr addr) {
    abortWithError("Unimplemented.");
}

Simulator::Object&
MGSimDynamicLSU::GetMemoryPeer() {
    return *this;
}

////// MGSimTCEMemory ////////////////////////////////////////////////////
MGSimTCEMemory::MGSimTCEMemory(
    const TTAMachine::AddressSpace& as, 
    Simulator::IMemory& mgsimMemory) : 
    Memory(as.start(), as.end(), as.width()),
    mgsimMemory_(mgsimMemory) {
    assert(dynamic_cast<Simulator::IMemoryAdmin*>(&mgsimMemory) != NULL);
}

MGSimTCEMemory::~MGSimTCEMemory() {
}

/**
 * This method is for an "admin" read, i.e., non-timing
 * accurate reading from the memory, e.g., for inspecting
 * the contents of the memory in the debugger.
 *
 * The more accurate simulation is done in the LSU model.
 */ 
Memory::MAU
MGSimTCEMemory::read(Word address) {
    Memory::MAU data;
    dynamic_cast<Simulator::IMemoryAdmin&>(mgsimMemory_).Read(
        address, &data, 1);
    return data;
}

/**
 * This method is for "admin" write, i.e., non-timing
 * writing to the memory, e.g., for pre-simulation 
 * data memory initialization.
 *
 * The more accurate simulation is done in the LSU model.
 */ 
void
MGSimTCEMemory::write(Word address, Memory::MAU data) {
    dynamic_cast<Simulator::IMemoryAdmin&>(mgsimMemory_).Write(
        address, &data, NULL, 1);
}

////// MGSim /////////////////////////////////////////////////////////////
MGSim::MGSim(const char* conf)
    : overrides(), extras(), k(bps), bps(k, 0) {
    ConfigMap defaults;
    ConfigParser parser(defaults);
    parser(read_file(conf));
    cfg = new Config(defaults, overrides, extras);
}

void
MGSim::DoSteps(Simulator::CycleNo nCycles) {

    using namespace Simulator;

    bps.Resume();
    RunState state = k.Step(nCycles);
    switch(state) {
    case STATE_ABORTED:
        if (bps.NewBreaksDetected())
        {
            std::ostringstream ss;
            bps.ReportBreaks(ss);
            throw std::runtime_error(ss.str());
        }
        else
            // The simulation was aborted, because the user interrupted it.
            throw std::runtime_error("Interrupted!");
        break;

    case STATE_IDLE:
        // An idle state might actually be deadlock if there's a
        // suspended thread.  So check all cores to see if they're
        // really done.
        /*
          // REPLACE WITH YOUR OWN IDLE CHECK
        for (DRISC* p : m_procs)
            if (!p->IsIdle())
            {
                goto deadlock;
            }
        */

        // If all cores are done, but there are still some remaining
        // processes, and all the remaining processes are stalled,
        // then there is a deadlock too.  However since the kernel
        // state is idle, there cannot be any running process left. So
        // either there are no processes at all, or they are all
        // stalled. Deadlock only exists in the latter case, so
        // we only check for the existence of an active process.
        for (const Clock* clock = k.GetActiveClocks(); clock != NULL; clock = clock->GetNext())
        {
            if (clock->GetActiveProcesses() != NULL)
            {
                goto deadlock;
            }
        }

        break;

    case STATE_DEADLOCK:
    deadlock:
    {
        std::cerr << "Deadlock at cycle " << k.GetCycleNo()
                  << "; replaying the last cycle:" << std::endl;

        int savemode = k.GetDebugMode();
        k.SetDebugMode(-1);
        (void) k.Step(1);
        k.SetDebugMode(savemode);

        std::ostringstream ss;
        ss << "Stalled processes:" << std::endl;

        // See how many processes are in each of the states
        unsigned int num_stalled = 0, num_running = 0;

        for (const Clock* clock = k.GetActiveClocks(); clock != NULL; clock = clock->GetNext())
        {
            for (const Process* process = clock->GetActiveProcesses(); process != NULL; process = process->GetNext())
            {
                switch (process->GetState())
                {
                case STATE_DEADLOCK:
                    ss << "  " << process->GetName() << std::endl;
                    ++num_stalled;
                    break;
                case STATE_RUNNING:
                    ++num_running;
                    break;
                default:
                    UNREACHABLE;
                    break;
                }
            }
        }

        ss << std::endl
           << "Deadlock! (at cycle " << k.GetCycleNo() << ')' << std::endl
           << "(" << num_stalled << " processes stalled;  " << num_running << " processes running)";
        throw DeadlockException(ss.str());
        UNREACHABLE;
    }

    default:
        break;
    }

}
