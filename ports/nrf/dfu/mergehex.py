#!/usr/bin/python
# This file is part of the MicroPython project, http://micropython.org/
#
# The MIT License (MIT)
#
# Copyright (c) 2018 Ayke van Laethem
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

import argparse
import struct
import binascii

class Block:
    def __init__(self, address, data):
        self.address = address
        self.data = data

    def __len__(self):
        return len(self.data)

    @property
    def is_uicr(self):
        # UICR address? (Nordic-specific)
        return self.address & 0xfffff000 == 0x10001000

    def assert_alignment(self, pagesize):
        if self.is_uicr:
            # require exactly one UICR register
            assert len(self.data) == 4 and self.address & 3 == 0
            return
        if self.address % pagesize != 0:
            raise ValueError('page should be aligned to the block size (0x%x), but is at address 0x%x' % (pagesize, address))

    def split_pages(self, pagesize):
        self.assert_alignment(pagesize)
        if len(self) <= pagesize:
            yield self
        else:
            address = self.address
            for i in range(0, len(self), pagesize):
                yield Block(address, self.data[i:i+pagesize])
                address += pagesize

    def blocknumber(self, pagesize):
        self.assert_alignment(pagesize)
        if self.is_uicr:
            return self.address
        return self.address // pagesize


def readhex(path):
    # Resources:
    # https://en.wikipedia.org/wiki/Intel_HEX
    # http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.faqs/ka9903.html
    block = None
    base_address = 0
    for line in open(path, 'r'):
        line = line.strip()
        if not line:
            continue
        if not line.startswith(':'):
            raise ValueError('Intel hex files should start with a colon')
        line = line[1:]
        line = binascii.unhexlify(line)
        line = line[:-1] # drop checksum
        datalen, address, record_type = struct.unpack('>BHB', line[:4])
        data = line[4:]
        if len(data) != datalen:
            raise ValueError('data length doesn\'t match data length in record')
        if record_type == 0: # Data
            if block is None:
                block = Block(base_address + address, data)
            else:
                if block.address + len(block) != base_address + address:
                    # address changed, create a new block
                    yield block
                    block = Block(base_address + address, data)
                else:
                    block.data += data
        elif record_type == 1: # EOF
            # Ignore, this should be the last line anyway
            pass
        elif record_type == 2: # Extended Segment Address
            if block is not None:
                yield block
                block = None
            base_address = struct.unpack('>H', data)[0] * 16
        elif record_type == 3: # Start Segment Address
            # Ignore this one, but set the base_address to make sure we
            # don't accidentally read more blocks.
            # No idea why this record even exists, it's only relevant for
            # ancient Intel processors. It appears to contain the start
            # address of the .text segment.
            base_address = None
        elif record_type == 4: # Extended Linear Address
            if block is not None:
                yield block
                block = None
            base_address = struct.unpack('>H', data)[0] << 16
        else:
            raise ValueError('unknown record type: %d' % record_type)

    if block is not None:
        yield block


def write_line(fout, record_type, data=b'', address=0):
    line = struct.pack('>BHB', len(data), address, record_type)
    line += data
    checksum = (256 - sum(map(ord, line))) & 0xff
    line += struct.pack('B', checksum)
    line = ':%s\n' % binascii.hexlify(line).decode('utf-8').upper()
    fout.write(line)

def mergehex(infiles, outfile, pagesize):
    pages = {}

    # Read all pages. Infiles may overwrite pages of a previous infile.
    for infile in infiles:
        for block in readhex(infile):
            for page in block.split_pages(pagesize):
                pages[page.blocknumber(pagesize)] = page

    # Write Intel hex file
    with open(outfile, 'w') as fout:
        base_address = 0
        for pageid in sorted(pages.keys()):
            page = pages[pageid]
            if page.address - base_address >= 2**16:
                # Write Extended Linear Address.
                segment_address = page.address >> 16
                base_address = segment_address << 16
                write_line(fout, 4, struct.pack('>H', segment_address))
            for i_block in range(0, len(page), 16):
                # Write data (16 bytes)
                address = page.address + i_block
                data = page.data[i_block:i_block+16]
                if len(data) == 0:
                    continue
                write_line(fout, 0, data, address - base_address)
        write_line(fout, 1) # EOF


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Merge multiple Intel hex files')
    parser.add_argument('-p', '--pagesize', type=int, default=1024, help='Page size (e.g. 1024, 4096)')
    parser.add_argument('outfile', help='Output hex file')
    parser.add_argument('infiles', nargs='+', help='Input hex files')
    args = parser.parse_args()
    # TODO check that the page size is a power of two
    mergehex(args.infiles, args.outfile, args.pagesize)
