## Set the Tiramisu path
TIRAMISU_PATH=../../../../

# Set to true if you want to run the reference when running Tiramisu code
USE_REFERENCE_WITH_TIRAMISU=1

########################################################################
## General settings
CC=gcc
CXX=g++
CFLAGS=-O3 -mavx2 -fno-rtti
CXXFLAGS=-O3 -std=c++11 -fno-rtti

## No need to modify the rest
LIB_DIR=-L${TIRAMISU_PATH}/build/ -L${TIRAMISU_PATH}/3rdParty/Halide/bin/
INCLUDE_DIR=-I${TIRAMISU_PATH}/include/ -I${TIRAMISU_PATH}/3rdParty/Halide/include/
#TIRAMISU_GENERATED_OBJ=../tiramisu_make_local_single_double_block/generated_tiramisu_make_local_single_double_block_r1.o ../tiramisu_make_local_single_double_block/generated_tiramisu_make_local_single_double_block_r2.o  ../tiramisu_make_dibaryon_correlator/generated_tiramisu_make_dibaryon_correlator.o
TIRAMISU_GENERATED_OBJ=../tiramisu_make_fused_dibaryon_blocks_correlator/generated_tiramisu_make_fused_dibaryon_blocks_correlator.o

reference:
	$(CC) $(CFLAGS) qblocks_2pt.c qblocks_2pt_test.c -o qblocks_2pt_test

tiramisu:
	$(CXX) $(CXXFLAGS) ${INCLUDE_DIR} -DUSE_REFERENCE=0 -c tiramisu_wrapper.cpp -o tiramisu_wrapper
	$(CC) $(CFLAGS) -c -DUSE_REFERENCE=$(USE_REFERENCE_WITH_TIRAMISU) -DUSE_TIRAMISU=1 qblocks_2pt.c -o qblocks_2pt.o
	$(CC) $(CFLAGS) -c -DUSE_REFERENCE=$(USE_REFERENCE_WITH_TIRAMISU) -DUSE_TIRAMISU=1 qblocks_2pt_test.c -o qblocks_2pt_test.o
	$(CXX) $(CXXFLAGS) ${INCLUDE_DIR} ${LIB_DIR} tiramisu_wrapper qblocks_2pt.o qblocks_2pt_test.o ${TIRAMISU_GENERATED_OBJ} -lHalide -o qblocks_2pt_test

run:
	DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH}:${TIRAMISU_PATH}/3rdParty/Halide/bin/:${TIRAMISU_PATH}/build/ LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${TIRAMISU_PATH}/3rdParty/Halide/bin/:${TIRAMISU_PATH}/build/ ./qblocks_2pt_test

clean:
	rm -rf *.o qblocks_2pt_test tiramisu_wrapper tiramisu
