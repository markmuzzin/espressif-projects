#!/usr/bin/env python3
import argparse, sys, re, os.path

TEMPLATE = """
#include <stdint.h>
static const uint8_t %ITEMNAME%[] = {
"""

parser = argparse.ArgumentParser(description="Process a binary file")
parser.add_argument('input', type=argparse.FileType('rb'), help="Input binary file")
parser.add_argument('output', type=argparse.FileType('wb'), help="Output binary file", nargs="?")

def bin2header(source, destination):
    content = source.read()
    source_len = len(content)
    as_hex = [ ("0x%02x" % b) for b in content ]
    content = ""
    offset = 0
    for chunk in chunk_by(as_hex, 16):
        content += "    /* 0x%04x: */ " % offset
        content += ", ".join(chunk)
        content += ",\n"
        offset += 16
    output = TEMPLATE
    output = output.replace("%CONTENT%", content)
    output = output.replace("%SOURCE%", sys.argv[1])
    itemname = re.sub(r'[^a-zA-Z0-9]', '_',  os.path.splitext(os.path.basename(destination.name))[0])
    output = output.replace("%ITEMNAME%", itemname)
    destination.write(bytes(output, 'UTF-8'))

def chunk_by(seq, chunk_size):
    """ Yield successive chunks of length chunk_size from seq. """
    for i in range(0, len(seq), chunk_size):
        yield seq[i:i+chunk_size]


if __name__ == "__main__":
    args = parser.parse_args()
    output = args.output
    if output is None:
        path = os.path.splitext(args.input.name)[0] + ".h"
        output = open(path, "wb")
    bin2header(args.input, output)
