TCE_PREFIX=${HOME}/local/
MGSIM_SRC_ROOT=${HOME}/src/mgsim
MGSIM_BUILD_ROOT=${HOME}/src/mgsim/build
MGSIM_SOURCES=\
	${MGSIM_SRC_ROOT}/arch/mem/SerialMemory.cpp \
	${MGSIM_SRC_ROOT}/arch/mem/ParallelMemory.cpp \
	${MGSIM_SRC_ROOT}/arch/mem/DDRMemory.cpp \
	${MGSIM_SRC_ROOT}/arch/mem/BankedMemory.cpp \
	${MGSIM_SRC_ROOT}/arch/mem/DDR.cpp \
    ${MGSIM_SRC_ROOT}/arch/IOBus.cpp \
    ${MGSIM_SRC_ROOT}/arch/VirtualMemory.cpp \
    ${MGSIM_SRC_ROOT}/arch/BankSelector.cpp \
    ${MGSIM_SRC_ROOT}/arch/dev/Display.cpp \
    ${MGSIM_SRC_ROOT}/arch/dev/IODeviceDatabase.cpp \
    ${MGSIM_SRC_ROOT}/arch/simtypes.cpp \
    ${MGSIM_SRC_ROOT}/arch/symtable.cpp \
    ${MGSIM_SRC_ROOT}/sim/breakpoints.cpp \
    ${MGSIM_SRC_ROOT}/sim/config.cpp \
    ${MGSIM_SRC_ROOT}/sim/configparser.cpp \
    ${MGSIM_SRC_ROOT}/sim/except.cpp \
    ${MGSIM_SRC_ROOT}/sim/kernel.cpp \
    ${MGSIM_SRC_ROOT}/sim/ports.cpp \
    ${MGSIM_SRC_ROOT}/sim/readfile.cpp \
    ${MGSIM_SRC_ROOT}/sim/sampling.cpp \
    ${MGSIM_SRC_ROOT}/sim/storagetrace.cpp

MGSIM_OBJECTS=$(patsubst %.cpp,%.o,${MGSIM_SOURCES})
TCE_LIBS=-ltce

CPPFLAGS=-I${TCE_PREFIX}/include -I${TCE_PREFIX}/include/tce \
-I${MGSIM_SRC_ROOT}/ -I${MGSIM_BUILD_ROOT}
LDFLAGS=-L${TCE_PREFIX}/lib
CXXFLAGS=-g -Wno-c++11-extensions -std=c++11
CXX=clang++

%.o: %.cpp
	$(CXX) ${CPPFLAGS} ${CXXFLAGS} $< -O3 -c -o $@

build: ideal_dpsram_dmem ideal_sram_dmem dram_dmem

run: build ideal_dpsram_dmem.ini dram_dmem dram_dmem.ini 
	@echo "*** ideal SRAM, single port, single LSU"
	./ideal_sram_dmem ideal_sram_dmem.ini
	@echo
	@echo "*** DDR, single port, single LSU"
	./dram_dmem dram_dmem.ini
	@echo
	@echo "*** ideam SRAM, dual port, dual LSU"
	./ideal_dpsram_dmem ideal_dpsram_dmem.ini

ideal_dpsram_dmem: ideal_dpsram_dmem.cc hello-2lsu.tpef tce_mgsim.o ${MGSIM_OBJECTS}
	$(CXX) ${CPPFLAGS} ${CXXFLAGS} ${LDFLAGS} $< ${MGSIM_OBJECTS} tce_mgsim.o ${TCE_LIBS} -O3 -o $@

ideal_sram_dmem: ideal_sram_dmem.cc hello.tpef tce_mgsim.o ${MGSIM_OBJECTS}
	$(CXX) ${CPPFLAGS} ${CXXFLAGS} ${LDFLAGS} $< ${MGSIM_OBJECTS} tce_mgsim.o ${TCE_LIBS} -O3 -o $@

dram_dmem: dram_dmem.cc hello.tpef tce_mgsim.o ${MGSIM_OBJECTS}
	$(CXX) ${CPPFLAGS} ${CXXFLAGS} ${LDFLAGS} $< ${MGSIM_OBJECTS} tce_mgsim.o ${TCE_LIBS} -O3 -o $@

tce_mgsim.o: tce_mgsim.hh tce_mgsim.cc 
	$(CXX) ${CPPFLAGS} ${CXXFLAGS} -c tce_mgsim.cc -O3 -o $@

# A test program for the single LSU machine.
hello.tpef: hello.c minimal_with_stdout.adf
	tcecc hello.c -O3 -a minimal_with_stdout.adf -o hello.tpef

# A test program for the dual LSU machine.
hello-2lsu.tpef: hello.c dual-lsu.adf
	tcecc hello.c -O3 -a dual-lsu.adf -o hello-2lsu.tpef

clean:
	rm -f ideal_sram_dmem tce_mgsim.o clustered_multibank dram_smem ideal_dpsram_dmem 

