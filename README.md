# GZIPdecomp_Alveo
Decompression of GZIP files for Xilinx Alveo

This repository contains source code for a GZIP decompression algorithm on Xilinx Alveo Field Prgrammable Gate Arrays.

This project is strongly influenced by Joerg Ibsen's "tiny inflate": https://github.com/jibsen/tinf

The argument parser was influenced by Jesse Laning's "argparse": https://github.com/jamolnng/argparse

# Prerequisites

- Xilinx Runtime 2018.2 or higher
- A Xilinx Alveo (deploy an development) Shell (U200, U250, U50 or U280)
- A Linux OS supported by Xilinx Vitis
- An ISO C++11 compatible compiler (tested with g++ >= 7) with OpenMP support
- Vitis 2019.2
- (optional) doxygen and graphviz for the generation of the documentation
- (optional) LaTeX and Gnu Make for the pdf documentation

# Build

- import the project file "tinfcpp.zip" in Vitis IDE
- choose a number of compute units if more than one are needed
- change the host compiler in the settings of the configuration if needed
- choose a configuration and build (hammer symbol)
- the binary is available in folder Emulation-SW, Emulation-HW or Hardware, depending on the build configuration
- the device binaries are located at "build-configuration"/tinfcpp-Default, you may rename or move them

# Usage

tinfcpp [OPTION]... [FILE]...

Uncompress FILEs (by default in-place).

Mandatory arguments to long options are mandatory for short options too.

  -c, --stdout      write on standard output, keep original files unchanged

  -d, --decompress  decompress

  -f, --force       force overwrite of output file and compress links

  -h, --help        give this help

  -k, --keep        keep (don't delete) input files

  -l, --list        list compressed file contents

  -L, --license     display software license

  -n, --no-name     do not save or restore the original name and timestamp

  -N, --name        save or restore the original name and timestamp

  -q, --quiet       suppress all warnings

  -r, --recursive   operate recursively on directories

      --rsyncable   make rsync-friendly archive

  -S, --suffix=SUF  use suffix SUF on compressed files

      --synchronous synchronous output (safer if system crashes, but slower)

  -t, --test        test compressed file integrity

  -v, --verbose     verbose mode

  -V, --version     display version number

  -1, --fast        compress faster

  -9, --best        compress better

  -b, --binary      path to the device binary (default: ../binary_container_1.xclbin)

With no FILE, or when FILE is -, standard input is read.

- any compatible binary at any place can be loaded when specified properly with the "-b" option
- with exception of "-b" the options are fully compatible to the usual "gunzip" command on most linux systems
- "-b" must be the last option
- The number of OMP threads must match the number of compute units. More leads to an error, less causes some kernels to be unoccupied. Set the environmen varibale OMP_NUM_THREADS to the desired value, otherwise the system default is used.
  
- generate full documentation in doc by running "doxygen Doxyfile"
- type "make" in doc/latex if you want a pdf file
