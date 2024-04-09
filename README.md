
# LLVM Static Compiler Pass for Levioso

### Ali Hajiabadi<sup>&#10038;</sup>, Archit Agarwal<sup>†</sup>, Andreas Diavastos<sup>&#10038;</sup>, Trevor E. Carlson<sup>&#10038;</sup>

<sup>&#10038;</sup>National University of Singapore&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<sup>†</sup> University of California, San Diego

## Introduntion

This repo contains the static compiler analysis passes of Levioso (DAC'24). The goal of our compiler analysis is to detect dependent instructions of conditional branches in the x86 backend of LLVM.

## Citation

```
@inproceedings{levioso2024,
  title = {Levioso: Efficient Compiler-Informed Secure Speculation},
  author = {Ali Hajiabadi and Archit Agarwal and Andreas Diavastos and Trevor E. Carlson},
  booktitle = {Proceedings of the 61st Design Automation Conference (DAC)},
  year = {2024},
}
```

## Building llvm-levioso

### Tested Environment and Tools

Tool | Version
--- | --- 
Ubuntu | Ubuntu 20.04
C/C++ Compiler | GNU 9.4.0 
Python | 3.8.10
Ninja | 1.10.0
CMake | 3.16.3

The rest of requirements will be informed with LLVM build scripts.

### Building Commands

Here are the commands to build llvm-levioso, assuming to install LLVM in `$INSTALL_PATH`:

```
clone https://github.com/Compiler-Dependency-Analysis/llvm-levioso.git
cd llvm-levioso
mkdir _build && cd _build
cmake -G Ninja -DCMAKE_INSTALL_PREFIX=$INSTALL_PATH -DLLVM_ENABLE_PROJECTS="clang" -DLLVM_TARGETS_TO_BUILD=X86 -DCMAKE_BUILD_TYPE=Debug -DLLVM_ENABLE_ASSERTIONS=On ../llvm/
ninja
ninja install
```

**Note**: The main compiler pass of Levioso is implemented in `llvm/lib/Target/X86/X86ReconvergenceDetection.cpp` and is built with the LLVM project, specified above.
 
## Compilation with llvm-levioso

For C/C++ prorgrams, clang and clang++ are used for compilation. To enable the Levioso pass, we need to pass post-RA scheduling flags to clang/clang++. Here is the command to compile a C program and generate Levioso metadata information (i.e., the list of dependent instructions for each conditional branch); we use `qsort` application from MiBench for test (see `levioso-tests` folder):

```
cd levioso-tests/qsort
$INSTALL_PATH/bin/clang -static -O3 -g qsort_large.c -mllvm -misched-postra -mllvm -enable-post-misched -o qsort_large -lm
```

After compilation, a file named `levioso_dependency_info.txt` is generated that contains the dependency information (a list of branches and their dependents). In Section _Post-Processing Scripts_, we provide the scripts and commands to convert this metadata to PC information, which can later be used for further analysis (e.g., gem5 evaluation).

**Note**: We embed dependency metadata in the debug information of the generated executable. Hence, ensure that `-g` is passed in compilation.

## Post-Processing Scripts

This the final is step to extract the PC of the analyzed branches and their dependent instructions. In other words, this step generates the list of PCs using the dependency metadata generated during compilation (`levioso_dependency_info.txt`) and the debug information of the executable. Here are the commands to (1) dump the debug information of the executable by `llvm-dwarfdump`, and (2) generate the list of PCs of branches and their dependents:

```
$INSTALL_PATH/bin/llvm-dwarfdump -debug-line qsort_large > qsort_large.debug
python ../../levioso-scripts/dump_dependent.py --debug qsort_large.debug --llvm levioso_dependency_info.txt
```

The final output has a structure of this format for a program with $n$ conditional branches (`PC(inst)` means the PC of instruction `inst`):
```
List of dependents of Branch PC(BR_1):
PC(Dep_1)
PC(Dep_2)
...
[End of dependents list]
...
List of dependents of Branch PC(BR_n):
PC(Dep_1)
PC(Dep_2)
...
[End of dependents list]
```
