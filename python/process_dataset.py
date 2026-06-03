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
    src = sys.argv[1]
    dst = sys.argv[2]
    tree = sys.argv[3]

    with gzip.open(src, 'rt', encoding='utf-8') as f:
        json_dataset = json.loads(f.read())

    samples = []
    labels = []
    for sample in json_dataset:
        samples.append(sample['vector'])
        labels.append(sample['label'] == 'fraud')

    samples = numpy.array(samples, dtype='float32')
    labels = numpy.array(labels)

    clf = KNeighborsClassifier(5, algorithm='kd_tree', leaf_size=15).fit(samples, labels)
    data, idx_array, node_data, node_bounds = clf._tree.get_arrays()

    #reorder data to use kdtree indices
    samples = samples[idx_array, :]
    labels = labels[idx_array]

    write_dataset(dst, samples, labels, 'float32')
    write_tree(tree, node_data, node_bounds, 'float32')

    stem = dst.rpartition('.')[0]
    write_dataset(f'{stem}_f16.dat', samples, labels, 'float16')

    i16 = np.round(samples * 10000).astype(np.int16)
    write_dataset(f'{stem}_i16.dat', i16, labels, 'int16')

    stem = tree.rpartition('.')[0]
    write_tree(f'{stem}_f16.dat', node_data, node_bounds, 'float16')

if __name__ == '__main__':
    main()
