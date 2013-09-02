#include <cstdlib>

#include "tce_mgsim.hh"

int main(int argc, char** argv) {

    MGSim mgsim(argv[1]);

    size_t memFreq = mgsim.cfg->getValue<size_t>("MemoryFreq");
    size_t coreFreq = mgsim.cfg->getValue<size_t>("CoreFreq");
    assert (memFreq == coreFreq);

    Simulator::Clock& clock = mgsim.k.CreateClock(memFreq);
    Simulator::Object root("", clock);

    MGSimTTACore tta(
        "core0", "minimal_with_stdout.adf",
        root, clock);
    
    MGSimSerialMemory* mem = 
        new MGSimSerialMemory("data", root, clock, *env.cfg);
    tta.replaceMemoryModel("data", mem);

    MGSimDynamicLatencyLSU lsu("LSU", tta);

    mem->Initialize();
    
    tta.loadProgram("hello.tpef");

    return EXIT_SUCCESS;
}
