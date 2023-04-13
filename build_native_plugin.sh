#!/bin/bash

LIB_DIR="/home/dbarbera/Repositories/self_contained_c_xyz/Go_xyz_plus/ps_plus/build"
LIB_PATH="$LIB_DIR/libps_plus.so"
CGO_LDFLAGS="-L$LIB_DIR" LD_LIBRARY_PATH="$LIB_DIR" go build -buildmode=plugin -o plugin.so xyz_plus.go

#This could be part of a cmake build script, and LIB_DIR will be determined by cmake default output directory