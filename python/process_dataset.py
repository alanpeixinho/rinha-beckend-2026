#/usr/bin/env python3

import sys
import numpy
import gzip
import json
import struct

def main():
    src = sys.argv[1]
    dst = sys.argv[2]

    with gzip.open(src, 'rt', encoding='utf-8') as f:
        json_dataset = json.loads(f.read())

    samples = []
    labels = []
    for sample in json_dataset:
        samples.append(sample['vector'])
        labels.append(sample['label'] == 'fraud')

    samples = numpy.array(samples, dtype='float32')
    labels = numpy.array(labels)

    with open(dst, 'wb') as f:
        dims = struct.pack('<ii', *samples.shape)
        f.write(dims)
        f.write(samples.tobytes())
        f.write(labels.tobytes())

if __name__ == '__main__':
    main()
