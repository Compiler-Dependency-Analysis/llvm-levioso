import optparse
import sys
import os
import logging
import collections

import DumpOptions

parser = optparse.OptionParser()
DumpOptions.addOptions(parser)

(options, args) = parser.parse_args()

address_file = ""
llvm_file = ""

if options.debug:
    address_file = options.debug
else:
    logging.error("[ ERROR ] Please specify the input file.")
    exit(0)

if options.llvm:
    llvm_file = options.llvm
else:
    logging.error("[ ERROR ] Please specify the input file.")
    exit(0)

counter = 0

PC_map = dict()

with open(address_file, "r") as code:
    for line in code:
        if line[0] == '0' and line[1] == 'x':
            segments = line.split()
            ADDR = hex(int(segments[0], 16))
            address = format(int(segments[0], 16), 'x')
            line_number = int(segments[1])
            col_number = int(segments[2])
            if col_number >= 10000:
                string = str(line_number) + "," + str(col_number)
                PC_map[string] = str(address)


BR_dict = dict()
inst_list = list()
BR = ""
skipBranch = 0

with open(llvm_file, "r") as br_file:
    for line in br_file:
        if line[0] == '*':
            segments = line.split()
            string = segments[1] + "," + segments[2]
            if string in PC_map:
                BR = PC_map[string]
                skipBranch = 0
                print("List of dependents of Branch %s:" % PC_map[string])
            else:
                skipBranch = 1
        elif line[0] == '-' and skipBranch == 0:
            BR_dict[BR] = inst_list
            inst_list = []
            print("[End of dependents list]")
        elif skipBranch == 0 and len(line.split()) > 1:
            segments = line.split()
            string = segments[0] + "," + segments[1]
            if string in PC_map:
                inst_list.append(PC_map[string])
                print(PC_map[string])
