TCE_PREFIX=${HOME}/local/
MGSIM_SRC_ROOT=${HOME}/src/mgsim

CPPFLAGS=-I${TCE_PREFIX}/include
LDFLAGS=-L${TCE_PREFIX}/lib

ideal_sram_dmem: ideal_sram_dmem.cc tce_mgsim.o
	$(CXX) ${CPPFLAGS} ${LDFLAGS} $< -O3 -o $@

tce_mgsim.o: tce_mgsim.hh tce_mgsim.cc
	$(CXX) ${CPPFLAGS} ${LDFLAGS} $< -O3 -o $@

clustered_multibank: clustered_multibank.cc
	$(CXX) ${LDFLAGS} ${LDFLAGS} $< -O3 -o $@
