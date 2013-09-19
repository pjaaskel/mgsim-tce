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
#include "ExecutingOperation.hh"
#include "Operation.hh"
#include "Operand.hh"
#include "SimValue.hh"
#include "SimulatorCLI.hh"

/**
   Division of responsiblities to implement the correct lock timings
   with the MGSim model.

   Global lock is requested by a load-store unit (LSU) in case of
   a) an arbitration conflict: cannot issue the access at all
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
   FU output ports (advance the cycle of the core) if the lock is not 
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
   
 */


//#define DEBUG_TCE_MGSIM


////// MGSimTTACore //////////////////////////////////////////////////////
MGSimTTACore::MGSimTTACore(
    const TCEString& coreName, const TCEString& adfFileName,
    const TCEString& initialProgram, Simulator::Object& parent, 
    Simulator::Clock& clock, Config& config) :
    Simulator::Object(TCEString("tce.") + coreName, parent, clock), 
    SimpleSimulatorFrontend(adfFileName, initialProgram),
    enabled_("b_enabled", *this, GetClock(), true),
    clockAdvanceProcess_(
        *this, "clock-advance", 
        Simulator::delegate::create<
            MGSimTTACore, &MGSimTTACore::mgsimClockAdvance>(*this)),
    lockRequests_(0) {
    enabled_.Sensitive(clockAdvanceProcess_);
    config.registerObject(*this, GetName());
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

    initializeDataMemories(&as);
}

Simulator::Result
MGSimTTACore::mgsimClockAdvance() {
    switch (GetKernel()->GetCyclePhase()) {
    case Simulator::PHASE_ACQUIRE: 
#ifdef DEBUG_TCE_MGSIM
        Application::logStream()
            << std::endl
            << "#### start of ttasim cycle " << cycleCount() + 1
            << " mgsim cycle " << GetKernel()->GetCycleNo()
            << std::endl;
#endif
        if (!isGlobalLockRequested()) {
            // advance the TTA core's simulation clock to receive the
            // possible new memory requests from the core's LSU simulation
            // model(s)
            step();
            if (isFinished()) {
                enabled_.Clear();
#ifdef DEBUG_TCE_MGSIM 
                Application::logStream() << "### core simulation finished" << std::endl;
#endif
            }
            if (false && cycleCount() == 7) {
                SimulatorCLI cli(frontend());
                cli.run();                                 
            }
        } else {
#ifdef DEBUG_TCE_MGSIM
            Application::logStream() << "### core locked" << std::endl;
#endif
        }
        // keep trying to issue the pending accesses in the LSUs, 
        // even if the TTA core is locked
        for (LoadStoreUnitVec::iterator i = lsus_.begin(); 
             i != lsus_.end(); ++i) {
            MGSimDynamicLSU& lsu = **i;
            lsu.tryIssuePending();
        }
        break;
    
    case Simulator::PHASE_COMMIT:
        for (LoadStoreUnitVec::iterator i = lsus_.begin(); 
             i != lsus_.end(); ++i) {
            MGSimDynamicLSU& lsu = **i;
            // TODO: how to know which of the requests were committed?
            // here it just assumes all LSUs got their access committed?
            lsu.commitPending();
        }
        break;
    default:
        return Simulator::SUCCESS;
    }
    return Simulator::SUCCESS;
}

/**
 * Check if any of the LSUs have requested a global lock.
 */
bool
MGSimTTACore::isGlobalLockRequested() const {
    return lockRequests_ > 0;
}

/**
 * Sets the given MGSim wrapper load-store unit as the
 * LSU model for the given ADF LSU.
 *
 * This should be called from the MGSimDynamicLSU constructor
 * to register the LSU model to the parent TTA.
 */
void
MGSimTTACore::addDynamicLSU(
    TCEString adfLSUName, MGSimDynamicLSU& lsu) {
    setOperationSimulator(adfLSUName, lsu);
    lsus_.push_back(&lsu);
}


bool
MGSimTTACore::OnMemoryReadCompleted(
    Simulator::MemAddr addr, const char* data) {
#ifdef DEBUG_TCE_MGSIM
    Application::logStream() << "a read completed" << std::endl;
    PRINT_VAR(GetKernel()->GetCycleNo());
    PRINT_VAR(cycleCount());
#endif
    for (LoadStoreUnitVec::iterator i = lsus_.begin(); 
         i != lsus_.end(); ++i) {
        MGSimDynamicLSU& lsu = **i;
        // the first LSU that is found that is waiting for the
        // addr is assumed to be receiving the request
        // @todo: if MCID was accessible here we could route
        // the result directly to the correct LSU
        if (lsu.OnMemoryReadCompleted(addr, data)) break;
    }
    return true;

}

bool
MGSimTTACore::OnMemoryWriteCompleted(Simulator::WClientID wid) {
#ifdef DEBUG_TCE_MGSIM
    Application::logStream() << "a write completed" << std::endl;
#endif
    for (LoadStoreUnitVec::iterator i = lsus_.begin(); 
         i != lsus_.end(); ++i) {
        MGSimDynamicLSU& lsu = **i;
        // @todo: if MCID was accessible here we could route
        // the result directly to the correct LSU
        if (lsu.OnMemoryWriteCompleted(wid)) break;
    }
    return true;
}

bool
MGSimTTACore::OnMemoryInvalidated(Simulator::MemAddr addr) {
    abortWithError("Unimplemented.");
}

Simulator::Object&
MGSimTTACore::GetMemoryPeer() {
    return *this;
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
    parentTTA_(parentTTA),
    // The MGSim memories are always accessed a "cache line" at a time
    // (even if not using a cache), call it "data bus width" here.
    dataBusWidth_(config.getValue<Simulator::CycleNo>("CacheLineSize")),
    pendingOperation_(NULL) {

    parentTTA.addDynamicLSU(lsuName, *this);
    config.registerObject(*this, GetName());

    Simulator::StorageTraceSet traces; 
    Simulator::StorageTraceSet st; 
    memClientID_ = 
        mgsimMemory_.RegisterClient(
            parentTTA, parentTTA.clockAdvanceProcess(), traces, st, true);
    parentTTA.clockAdvanceProcess().SetStorageTraces(opt(traces));

}

Simulator::Result
MGSimDynamicLSU::mgsimCycleAdvance() {
    // Do nothing here at the moment. The TTA core model's cycle advance
    // implements the ttasim clock advance and locking semantics.
    return Simulator::SUCCESS;
}

/**
 * Tries to issue a memory request initiated in the current cycle.
 */ 
void
MGSimDynamicLSU::tryIssuePending() {

    // issue any pending memory requests
    // pending memory request means any request that have not been
    // accepted to the memory system yet, e.g., due to arbitration conflicts
    if (pendingOperation_ == NULL) return;

    ExecutingOperation& operation = *pendingOperation_;
    const Operation& op = operation.operation();
    if (op.readsMemory()) {
        assert(op.operand(1).isAddress() && op.operand(2).isMemoryData());
        Simulator::MemAddr addr = operation.io(1).uIntWordValue();
        size_t size = 
            op.operand(2).elementWidth() * op.operand(2).elementCount();
#ifdef DEBUG_TCE_MGSIM
        Application::logStream() 
            << size << "b read from " << addr << std::endl;
#endif
       // The starting address of the data block to access. 
        Simulator::MemAddr blockStart = (addr/dataBusWidth_)*dataBusWidth_;

        mgsimMemory_.Read(memClientID_, blockStart);
    } else if (op.writesMemory()) {
        assert(op.operand(1).isAddress() && op.operand(2).isMemoryData());
        Simulator::MemAddr addr = operation.io(1).uIntWordValue();

        // The starting address of the data block to access. 
        Simulator::MemAddr blockStart = (addr/dataBusWidth_)*dataBusWidth_;

#ifdef DEBUG_TCE_MGSIM        
        PRINT_VAR(addr);
        PRINT_VAR(blockStart);
#endif

        size_t operationSize = 
            op.operand(2).elementWidth() * op.operand(2).elementCount() / 8;

        // no point in populating the data here, we are only checking for
        // arbitration conflicts

        Simulator::MemData data;
#ifdef DEBUG_TCE_MGSIM
        Application::logStream() 
            << "b a write to " << addr << ", issue attempt " << std::endl;
#endif

        mgsimMemory_.Write(
            memClientID_, blockStart, data, (Simulator::WClientID)-1);
    } else {
        abortWithError("Got non memory operation in the pending operations?");
    }
}

/**
 * Commits any pending memory requests.
 *
 * This is called when the memory access is known to go through to
 * the memory system without any stalls.
 */ 
void
MGSimDynamicLSU::commitPending() {

    if (pendingOperation_ == NULL) return;

    ExecutingOperation& operation = *pendingOperation_;
    const Operation& op = operation.operation();
    if (op.readsMemory()) {
        assert(op.operand(1).isAddress() && op.operand(2).isMemoryData());
        Simulator::MemAddr addr = operation.io(1).uIntWordValue();
        size_t size = 
            op.operand(2).elementWidth() * op.operand(2).elementCount();
#ifdef DEBUG_TCE_MGSIM
        Application::logStream() 
            << size << "b read from " << addr << " committed " << std::endl;
#endif
        Simulator::MemAddr blockStart = (addr/dataBusWidth_)*dataBusWidth_;

        mgsimMemory_.Read(memClientID_, blockStart);
    } else if (op.writesMemory()) {

        assert(op.operand(1).isAddress() && op.operand(2).isMemoryData());
        Simulator::MemAddr addr = operation.io(1).uIntWordValue();

        // The starting address of the data block to access. 
        Simulator::MemAddr blockStart = (addr/dataBusWidth_)*dataBusWidth_;

#ifdef DEBUG_TCE_MGSIM        
        PRINT_VAR(addr);
        PRINT_VAR(blockStart);
#endif

        size_t operationSize = 
            op.operand(2).elementWidth() * op.operand(2).elementCount() / 8;

        /* These can be removed after base.opp is fixed to have the
           correct data widths for the non 32-bit data operations. */
        if (op.name() == "STQ") operationSize = 8;
        else if (op.name() == "STH") operationSize = 16;

        // TODO: need to align the access to the cache line size and
        // mask only the bytes I want to write
        Simulator::MemData data;
        // mask out the bytes before the part we want to write
        for (int i = 0; i < (addr - blockStart); ++i) {
                data.mask[i] = false;
        }
        // the wanted bytes
        for (int i = (addr - blockStart); 
             i < (addr - blockStart + operationSize); ++i) {
            data.data[i] = operation.iostorage_[1].rawBytes()[i - (addr - blockStart)];
            data.mask[i] = true;
#ifdef DEBUG_TCE_MGSIM
            PRINT_VAR((int)operation.iostorage_[1].rawBytes()[i - (addr - blockStart)]);
#endif
        }
        // mask out the bytes after the part we want to write
        for (int i = addr - blockStart + operationSize; 
             i < dataBusWidth_; ++i) {
            data.mask[i] = false;
        }

#ifdef DEBUG_TCE_MGSIM
        Application::logStream() 
            << "b write to " << addr << " committed " << std::endl;
#endif

        mgsimMemory_.Write(
            memClientID_, blockStart, data, (Simulator::WClientID)-1);
    } else {
        abortWithError("Got non memory operation in the pending operations?");
    }
    pendingOperation_ = NULL;
    parentTTA_.unsetLockRequest();
}

MGSimDynamicLSU::~MGSimDynamicLSU() {
}

/**
 * This is called on each non-locked cycle of TTA via the ttasim
 * cycle advance call.
 *
 * This should collect the memory requests. However, they should be
 * issued in a method that is called also on TTA lock cycles because
 * the retrying of the unsuccessful requests must be done on each
 * cycle.
 */
bool
MGSimDynamicLSU::simulateStage(ExecutingOperation& operation) {

    const Operation& op = operation.operation();
    if (!(op.readsMemory() || op.writesMemory()))
        return false;
    
#ifdef DEBUG_TCE_MGSIM
    Application::logStream() 
        << &operation << " simulate stage " << operation.stage() 
        << " of " << op.name() << std::endl;
#endif
    if (operation.stage() == 0) {
        // a new memory operation triggered at this cycle
        pendingOperation_ = &operation;
        incompleteOperations_.push_back(&operation);
        // pessimistically assume we need a core lock -- cannot
        // know yet before the arbitration results arrive for the cycle
        parentTTA_.setLockRequest();
    }
    // check if the operation is going to write the result to
    // the FU register at the next cycle advance
    // and the result has not arrived
    // assume only one pending result at most per operation,
    // does not support multiple output memory operations yet
    if (operation.pendingResults_.size() > 0 &&
        operation.pendingResults_[0].cyclesToGo_ == 2 &&
        std::find(
            incompleteOperations_.begin(), incompleteOperations_.end(),
            &operation) != incompleteOperations_.end()) {
        // the result should be available at the next cycle,
        // assume we cannot get it as it might or might not
        // arrive at the currently simulated cycle
        parentTTA_.setLockRequest();
        lateResult_ = true;
    }
    return true;
}

bool
MGSimDynamicLSU::OnMemoryReadCompleted(
    Simulator::MemAddr addr, const char* data) {

    if (incompleteOperations_.size() == 0) return false;

    // check if the oldest pending operation wants this data,
    // assume the results arrive in order
    ExecutingOperation& operation = *incompleteOperations_.at(0);
    const Operation& op = operation.operation();
    if (!op.readsMemory()) return false;

    Simulator::MemAddr wordAddr = operation.io(1).uIntWordValue();
    size_t size = 
        op.operand(2).elementWidth() * op.operand(2).elementCount();
    Simulator::MemAddr blockAddr = (wordAddr/dataBusWidth_)*dataBusWidth_;
    if (blockAddr != addr) return false;

    /* These can be removed after base.opp is fixed to have the
       correct data widths for the non 32-bit data operations. */
    if (op.name() == "LDQ") size = 8;
    else if (op.name() == "LDH") size = 16;

#ifdef DEBUG_TCE_MGSIM
    Application::logStream() 
        << &operation << ": "
        << size << "b read (" << op.name() << ") from " << wordAddr 
        << " completed at stage " << operation.stage() << std::endl;
#endif

    for (size_t i = 0; i < size / 8; ++i) {
        operation.iostorage_[1].rawBytes()[i] = data[i + (wordAddr - blockAddr)];
#ifdef DEBUG_TCE_MGSIM
        PRINT_VAR((int)operation.iostorage_[1].rawBytes()[i]);
#endif
    }
    incompleteOperations_.pop_front();
    if (lateResult_) {
        // assume the result that we waited for has now arrived       
        parentTTA_.unsetLockRequest();
        lateResult_ = false;
    }
    return true;
}

bool
MGSimDynamicLSU::OnMemoryWriteCompleted(Simulator::WClientID wid) {

    if (incompleteOperations_.size() == 0) return false;
    ExecutingOperation& operation = *incompleteOperations_.at(0);
    const Operation& op = operation.operation();
    if (!op.writesMemory()) return false;
    
#ifdef DEBUG_TCE_MGSIM
    Application::logStream()
        << &incompleteOperations_.front() << " " << op.name() 
        << " completed" << std::endl;
#endif
       
    incompleteOperations_.pop_front();
    return true;
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
    //PRINT_VAR(address);
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
    //   PRINT_VAR(address);
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
