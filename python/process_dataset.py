#/usr/bin/env python3

import sys
import numpy
import gzip
import json
import struct

from sklearn.neighbors import KNeighborsClassifier

def write_dataset(path, samples, labels, dtype):
    with open(path, 'wb') as f:
        dims = struct.pack('<ii', *samples.shape)
        f.write(dims)
        f.write(samples.astype(dtype).tobytes())
        f.write(labels.tobytes())

def write_tree(path, node_data, node_bounds, dtype):
    with open(path, 'wb') as f:
        nnodes = len(node_data)
        nfeats = node_bounds.shape[2]
        f.write(struct.pack('<ii', nnodes, nfeats))
        for i in range(nnodes):
            idx_start, idx_end, is_leaf, _ = node_data[i]
            f.write(struct.pack('<i', idx_start))
            f.write(struct.pack('<i', idx_end))
            f.write(struct.pack('<?', is_leaf))
        f.write(node_bounds.astype(dtype).tobytes())

def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('src', help='input references.json[.gz]')
    parser.add_argument('dst', help='output dataset file')
    parser.add_argument('tree', help='output kd-tree file')
    parser.add_argument('--leaf-size', type=int, default=16, help='kd-tree leaf size (default: 16)')
    args = parser.parse_args()
    src = args.src
    dst = args.dst
    tree = args.tree
    leaf_size = args.leaf_size

    with (gzip.open if src.endswith('.gz') else open)(src, 'rt', encoding='utf-8') as f:
        json_dataset = json.loads(f.read())

    samples = []
    labels = []
    for sample in json_dataset:
        samples.append(sample['vector'])
        labels.append(sample['label'] == 'fraud')

    samples = numpy.array(samples, dtype='float32')
    labels = numpy.array(labels)

    clf = KNeighborsClassifier(5, algorithm='kd_tree', leaf_size=leaf_size).fit(samples, labels)
    data, idx_array, node_data, node_bounds = clf._tree.get_arrays()

    #reorder data to use kdtree indices
    samples = samples[idx_array, :]
    labels = labels[idx_array]

    write_dataset(dst, samples, labels, 'float32')
    write_tree(tree, node_data, node_bounds, 'float32')

    stem = dst.rpartition('.')[0]
    i16 = numpy.round(samples * 10000).astype(numpy.int16)
    write_dataset(f'{stem}_i16.dat', i16, labels, 'int16')


    stem = tree.rpartition('.')[0]

    # compute int16 bounds directly from int16-quantized samples
    # this guarantees bounds are exact in int16 space (no rounding errors)
    lower_i16 = numpy.full((node_data.shape[0], samples.shape[1]), 32767, dtype=numpy.int16)
    upper_i16 = numpy.full((node_data.shape[0], samples.shape[1]), -32768, dtype=numpy.int16)
    for i in range(node_data.shape[0]):
        idx_start, idx_end, is_leaf, _ = node_data[i]
        if idx_start < idx_end:
            chunk = i16[idx_start:idx_end]
            lower_i16[i] = chunk.min(axis=0)
            upper_i16[i] = chunk.max(axis=0)

    # expand bounds by 1 int16 unit to account for quantization error at boundaries
    lower_i16 = numpy.clip(lower_i16 - 1, -32768, 32767).astype(numpy.int16)
    upper_i16 = numpy.clip(upper_i16 + 1, -32768, 32767).astype(numpy.int16)

    i16_bounds = numpy.stack([lower_i16, upper_i16])
    write_tree(f'{stem}_i16.dat', node_data, i16_bounds, 'int16')

if __name__ == '__main__':
    main()
