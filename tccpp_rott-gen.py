#!/usr/bin/env python3

# Generate the quine file. We'll do this by reading in the file given in the
# first argument, prepending an array of that file's data, and writing it out to
# the file given in our second argument.

import sys

if __name__ == '__main__':

    inp_fname = sys.argv[1]
    out_fname = sys.argv[2]

    with open(inp_fname, "r") as inp, open(out_fname, "w") as out:

        data = inp.read()
        serialized_data = ','.join(map(lambda c: f"0x{ord(c):02x}", data))

        out.write(f"static char rott_quine_data[] = {{{serialized_data}}};\n")
        out.write(data)
