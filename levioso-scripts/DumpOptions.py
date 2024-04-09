#
# Author: Ali Hajiabadi
#

def addOptions(parser):
    parser.add_option("--debug", type="string",
            help="The file containing the debug information.")

    parser.add_option("--llvm", type="string",
            help="The file containing the LLVM branch dependency information.")

