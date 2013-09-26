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

#include "SimulatorFrontend.hh"

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
    lockRequests_(0), config_(config), 
    lastSimulatedTTACycle_((uint64_t)-1) {
    enabled_.Sensitive(clockAdvanceProcess_);
    config.registerObject(*this, GetName());
}

MGSimTTACore::~MGSimTTACore() {
    for (size_t i = 0; i < lsus_.size(); ++i) 
        delete lsus_[i];
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


    // find all the load-store units that refer to the given
    // memory and replace them with models that interfaces with
    // the MGSim
    TTAMachine::Machine::FunctionUnitNavigator nav = machine().functionUnitNavigator();
    for (int i = 0; i < nav.count(); ++i) {
        const TTAMachine::FunctionUnit& fu = *nav.item(i);
        if (!fu.hasAddressSpace() || fu.addressSpace() != &as) continue;
        addDynamicLSU(
            fu.name(), new MGSimDynamicLSU(fu.name(), *this, mgsimMem, config_));
    }
}

Simulator::Result
MGSimTTACore::mgsimClockAdvance() {
    if (lastSimulatedTTACycle_ == GetKernel()->GetCycleNo())
        return Simulator::SUCCESS;

#ifdef DEBUG_TCE_MGSIM
    Application::logStream()
        << std::endl
        << "#### start of ttasim cycle " << cycleCount() + 1
        << " mgsim cycle " << GetKernel()->GetCycleNo()
        << std::endl;
#endif

    if (!isLockRequested()) {
        // Advance the TTA core's simulation clock to receive the
        // possible new memory requests from the core's LSU simulation
        // model(s). Do it only once per MGSim cycle.
        step();
        if (isFinished()) {
            enabled_.Clear();
#ifdef DEBUG_TCE_MGSIM 
            Application::logStream() << "### core simulation finished" << std::endl;
#endif
        }
    } else {
#ifdef DEBUG_TCE_MGSIM
        Application::logStream() << "### core locked" << std::endl;
#endif
    }
    lastSimulatedTTACycle_ = GetKernel()->GetCycleNo();
    return Simulator::SUCCESS;
}

/**
 * Check if any of the LSUs have requested a lock.
 */
bool
MGSimTTACore::isLockRequested() const {
    for (size_t i = 0; i < lsus_.size(); ++i) {
        if (lsus_[i]->needsLock()) return true;
    }
    return false;
}

uint64_t
MGSimTTACore::clockCycleCount() const {
    return GetKernel()->GetCycleNo();
}

void
MGSimTTACore::printStats(std::ostream* out) const {
    if (out == NULL) out = &std::cout;

    *out << "### clock cycles: " << clockCycleCount() << std::endl
         << "### instr cycles: " << cycleCount() << std::endl
         << "### stall cycles: " << clockCycleCount() - cycleCount() 
         << " (" << (clockCycleCount() - cycleCount())*100 / cycleCount() << "%)"
         << std::endl;
    
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
    TCEString adfLSUName, MGSimDynamicLSU* lsu) {
    setOperationSimulator(adfLSUName, *lsu);
    lsus_.push_back(lsu);
}

/**
 * Lockstep simulate against the pure TTA engine of TCE.
 *
 * Useful for debugging the simulation, but only when using
 * a single core.
 */
bool
MGSimTTACore::simulateInTandem(MGSim& mgsim) {
    
    SimpleSimulatorFrontend interp(machine(), program());

    assert (interp.cycleCount() == 0 && cycleCount() == 0);

    while (!isFinished()) {
        try {
            interp.step();
            while (cycleCount() != interp.cycleCount())
                mgsim.DoSteps(1);
            if (!interp.frontend().compareState(frontend(), &std::cerr))
                return false;
        } catch (const Exception& e) {
            std::cerr << "Simulation error: " << e.errorMessage() << std::endl;
            return false;
        }
    }
    return true;
}

////// MGSimDynamicLSU ///////////////////////////////////////////////////
// @todo rename to MGSimTTALSU
MGSimDynamicLSU::MGSimDynamicLSU(
    const TCEString& lsuName, 
    MGSimTTACore& parentTTA,
    Simulator::IMemory& mgsimMem,
    Config& config) : 
    Simulator::Object(
        parentTTA.GetName() + "." + lsuName,
        parentTTA, parentTTA.GetClock()),
    enabled_("b_enabled", *this, GetClock(), true),
    memoryPort_(
        *this, "memory-port", 
        Simulator::delegate::create<
            MGSimDynamicLSU, &MGSimDynamicLSU::memoryPortCycle>(*this)),
    mgsimMemory_(mgsimMem), parentTTA_(parentTTA),
    // The MGSim memories are always accessed a "cache line" at a time
    // (even if not using a cache), call it "data bus width" here.
    dataBusWidth_(config.getValue<Simulator::CycleNo>("CacheLineSize")),
    pendingOperation_(NULL) {

    config.registerObject(*this, GetName());

    Simulator::StorageTraceSet traces; 
    Simulator::StorageTraceSet st; 
    memClientID_ =
        mgsimMem.RegisterClient(*this, memoryPort_, traces, st, true);
    memoryPort_.SetStorageTraces(opt(traces));
    enabled_.Sensitive(memoryPort_);
}

/**
 * MGSim simulation method for the memory port process.
 */
Simulator::Result
MGSimDynamicLSU::memoryPortCycle() {
#ifdef DEBUG_TCE_MGSIM
    Application::logStream() << GetName() << ": memoryPortCycle(): ";
#endif
    switch (GetKernel()->GetCyclePhase()) {
    case Simulator::PHASE_ACQUIRE: 
#ifdef DEBUG_TCE_MGSIM
        Application::logStream() << "ACQUIRE" << std::endl;
#endif
        parentTTA_.mgsimClockAdvance();
        // issue the operations that were triggered at the current cycle
        // in the TTA
        if (pendingOperation_ != NULL)
            tryIssuePending();
        break;
    case Simulator::PHASE_COMMIT:
#ifdef DEBUG_TCE_MGSIM
        Application::logStream() << "COMMIT" << std::endl;
#endif
        if (pendingOperation_ != NULL)
            commitPending();
        break;
    case Simulator::PHASE_CHECK:
#ifdef DEBUG_TCE_MGSIM
        Application::logStream() << "CHECK" << std::endl;
#endif
        break;
    default:
        break;
    }
    return Simulator::SUCCESS;
}

/**
 * Tries to issue a memory request initiated in the current cycle.
 */ 
void
MGSimDynamicLSU::tryIssuePending() {

#ifdef DEBUG_TCE_MGSIM
    Application::logStream() << GetName() << ": tryIssuePending()" << std::endl;
#endif

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
            << GetName() << ": " << size << "b read from " << addr << std::endl;
#endif
       // The starting address of the data block to access. 
        Simulator::MemAddr blockStart = (addr/dataBusWidth_)*dataBusWidth_;

        mgsimMemory_.Read(memClientID_, blockStart);
    } else if (op.writesMemory()) {
        assert(op.operand(1).isAddress() && op.operand(2).isMemoryData());
        Simulator::MemAddr addr = operation.io(1).uIntWordValue();

        // The starting address of the data block to access. 
        Simulator::MemAddr blockStart = (addr/dataBusWidth_)*dataBusWidth_;

        size_t operationSize = 
            op.operand(2).elementWidth() * op.operand(2).elementCount() / 8;

        // no point in populating the data here, we are only checking for
        // arbitration conflicts

        Simulator::MemData data;
#ifdef DEBUG_TCE_MGSIM
        Application::logStream() 
            << operationSize << "B write to " << addr << ", issue attempt " << std::endl;
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

#ifdef DEBUG_TCE_MGSIM
    Application::logStream() << GetName() << ": commitPending()" << std::endl;
#endif

    if (pendingOperation_ == NULL) return;

    ExecutingOperation& operation = *pendingOperation_;
    const Operation& op = operation.operation();
    if (op.readsMemory()) {
        assert(op.operand(1).isAddress() && op.operand(2).isMemoryData());
        Simulator::MemAddr addr = operation.io(1).uIntWordValue();
        size_t size = 
            op.operand(2).elementWidth() * op.operand(2).elementCount();
        Simulator::MemAddr blockStart = (addr/dataBusWidth_)*dataBusWidth_;

        if (!mgsimMemory_.Read(memClientID_, blockStart)) 
            assert("Could not commit a read" && false);

#ifdef DEBUG_TCE_MGSIM
        Application::logStream() 
            << GetName() << ": "
            << &operation << " "
            << size << "b read from " << addr << " committed " << std::endl;
#endif

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
            << operationSize << "B write to " << addr << " committed " << std::endl;
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
        << GetName() << ": " << &operation << " simulate TTA stage " << operation.stage() 
        << " of " << op.name() << std::endl;
#endif
    if (operation.stage() == 0) {
        // a new memory operation triggered at this cycle
#ifdef DEBUG_TCE_MGSIM
        assert (pendingOperation_ == NULL);
        if (std::find(
                incompleteOperations_.begin(), 
                incompleteOperations_.end(), &operation) != incompleteOperations_.end()) {
            abortWithError(
                "Operation already in the pending list. Illegal object reuse?");
        }

#endif
        pendingOperation_ = &operation;
        incompleteOperations_.push_back(&operation);
        // pessimistically assume we need a core lock -- cannot
        // know yet before the arbitration results arrive for the cycle
        //       parentTTA_.setLockRequest();
    }

#if 0
    // freeze the core in case the operation is getting out of the
    // FU pipeline at the next cycle without the memory operation (store)
    // finishing, or if any of the load results have not arrived in time.
    // @todo does not support multiple output operations of which results
    // arrive in different times
    if (operation.isLastPipelineStage() &&
        std::find(
            incompleteOperations_.begin(), incompleteOperations_.end(),
            &operation) != incompleteOperations_.end()) {
        // the result should be available at the next cycle,
        // assume we cannot get it as it might or might not
        // arrive at the currently simulated cycle
#ifdef DEBUG_TCE_MGSIM
    Application::logStream() 
        << GetName() << ": "
        << &operation << " at its last pipeline stage, need to lock at the next cycle"
        << std::endl;
#endif
        parentTTA_.setLockRequest();
        lateResult_ = true;
    }
#endif
    return true;
}

bool
MGSimDynamicLSU::OnMemoryReadCompleted(
    Simulator::MemAddr addr, const char* data) {

    if (GetKernel()->GetCyclePhase() != Simulator::PHASE_COMMIT)
        return true;

    assert(incompleteOperations_.size() > 0);

    // check if the oldest pending operation wants this data,
    // assume the results arrive in order
    ExecutingOperation& operation = *incompleteOperations_.at(0);
    const Operation& op = operation.operation();

    assert(op.readsMemory());
    
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
        << GetName() << ": "
        << &operation << " "
        << size << "b read (" << op.name() << ") from " << wordAddr 
        << " completed at stage " << operation.stage() << std::endl;
#endif

    for (size_t i = 0; i < size / 8; ++i) {
        operation.iostorage_[1].rawBytes()[i] = data[i + (wordAddr - blockAddr)];
#ifdef DEBUG_TCE_MGSIM
//        PRINT_VAR((int)operation.iostorage_[1].rawBytes()[i]);
#endif
    }
    incompleteOperations_.pop_front();
    if (lateResult_) {
        // assume the result that we waited for has now arrived       
#ifdef DEBUG_TCE_MGSIM
    Application::logStream() 
        << GetName() << ": "
        << &operation << " late result arrived"
        << std::endl;
#endif
        parentTTA_.unsetLockRequest();
        lateResult_ = false;
    }
    return true;
}

bool
MGSimDynamicLSU::OnMemoryWriteCompleted(Simulator::WClientID wid) {

    if (GetKernel()->GetCyclePhase() != Simulator::PHASE_COMMIT)
        return true;

    assert(incompleteOperations_.size() > 0);
    ExecutingOperation& operation = *incompleteOperations_.at(0);
    const Operation& op = operation.operation();

    assert(op.writesMemory());
    
#ifdef DEBUG_TCE_MGSIM
    Application::logStream()
        << GetName() << ": "
        << &incompleteOperations_.front() << " " << op.name() 
        << " completed" << std::endl;
#endif
       
    incompleteOperations_.pop_front();
#if 0
    if (lateResult_) {
        // assume the operation of which completion we waited for has
        // now finished
#ifdef DEBUG_TCE_MGSIM
    Application::logStream() 
        << GetName() << ": "
        << &operation << " late result arrived"
        << std::endl;
#endif
        parentTTA_.unsetLockRequest();
        lateResult_ = false;
    }
#endif
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

/**
 * The LSU needs to lock the core for the current cycle in
 * case of 
 * a) arbitration conflict: could not issue the memory request 
 * b) late completing operation: dynamic latencies with memory
 * operations grow larger than the architectural latency
 */
bool
MGSimDynamicLSU::needsLock() const {

    for (ExecutingOperationFIFO::const_iterator i = 
             incompleteOperations_.begin();
         i != incompleteOperations_.end();
         ++i) {
        const ExecutingOperation& operation = **i;

        if (operation.isLastPipelineStage()) {
            // the result should be available / operation completed at this cycle and 
            // it has not arrived yet
#ifdef DEBUG_TCE_MGSIM
            Application::logStream() 
                << GetName() << ": "
                << &operation 
                << " at its last pipeline stage, locking "
                << "until the result arrives"
                << std::endl;
#endif
            return true;
        }
    }
    return false;    
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

        for (const Clock* clock = k.GetActiveClocks(); 
             clock != NULL; clock = clock->GetNext())
        {
            for (const Process* process = clock->GetActiveProcesses(); 
                 process != NULL; process = process->GetNext())
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
           << "Deadlock! (at cycle " << k.GetCycleNo() << ')' 
           << std::endl
           << "(" << num_stalled << " processes stalled;  " 
           << num_running << " processes running)";
        throw DeadlockException(ss.str());
        UNREACHABLE;
    }

    default:
        break;
    }

}
