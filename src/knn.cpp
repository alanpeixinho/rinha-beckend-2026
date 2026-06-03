#include <algorithm>
#include <cmath>
#include <cstdio>
#include "knn.h"
#include "profiler.h"

using namespace std;

void destroy_dataset(Dataset dataset) {
    delete [] dataset.samples;
    delete [] dataset.labels;
}

static inline float squared_l2_dist(const float* x1, const dataset_dtype* x2, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float d = float(x1[i]) - float(x2[i]);
        sum += d * d;
    }
    return sum;
}

Dataset load_dataset(const char* filepath) {
    int nrows, ncols;
    dataset_dtype* data;
    bool* labels;
    FILE* f = fopen(filepath, "rb");
    fread(&nrows, sizeof(int), 1, f);
    fread(&ncols, sizeof(int), 1, f);
    data = new dataset_dtype[nrows * ncols];
    labels = new bool[nrows];
    fread(data, sizeof(dataset_dtype), nrows * ncols, f);
    fread(labels, sizeof(bool), nrows, f);
    fclose(f);
    return Dataset {
        .nsamples = nrows,
        .nfeats = ncols,
        .samples = data,
        .labels = labels
    };
}

float knn_bf_score(const Dataset dataset, const float* query, int k) {
    const int nsamples = dataset.nsamples;
    const int nfeats = dataset.nfeats;

    SampleDist neighbors[k];
    fill(neighbors, neighbors + k, SampleDist{-1, INFINITY});
    make_heap(neighbors, neighbors + k);

    for (int i = 0; i < nsamples; ++i) {
        const float dist = squared_l2_dist(query, &(dataset.samples[i * nfeats]), nfeats);
        if (dist < neighbors[0].dist) {
            pop_heap(neighbors, neighbors + k);
            neighbors[k - 1] = SampleDist{.i = i, .dist = dist};
            push_heap(neighbors, neighbors + k);
        }
    }

    sort_heap(neighbors, neighbors + k);

    float score = 0.0f;
    for (int i = 0; i < k; ++i) {
        score += dataset.labels[neighbors[i].i];
    }
    return score / k;
}

KDTree load_kdtree(const char* filepath) {

    int nnodes, nfeats;
    KDTreeNode* nodes;
    bounds_dtype* bounds;
    FILE* f = fopen(filepath, "rb");
    fread(&nnodes, sizeof(int), 1, f);
    fread(&nfeats, sizeof(int), 1, f);

    nodes = new KDTreeNode[nnodes];
    bounds = new bounds_dtype[2 * nnodes * nfeats];

    fread(nodes, sizeof(KDTreeNode), nnodes, f);
    fread(bounds, sizeof(bounds_dtype), 2 * nnodes * nfeats, f);

    fclose(f);
    return KDTree {
        .nnodes = nnodes,
        .nfeats = nfeats,
        .nodes = nodes,
        .lower_bounds = &bounds[0],
        .upper_bounds = &bounds[nnodes * nfeats]
    };
}

void destroy_kdtree(KDTree tree) {
    delete [] tree.nodes;
    delete [] tree.lower_bounds;
}

float min_dist_to_box_sq(const float* query, const bounds_dtype* lower,
        const bounds_dtype* upper, int nfeats) {
    float sum = 0.0f;
    for (int i = 0; i < nfeats; ++i) {
        const float diff0 = float(lower[i]) - float(query[i]);
        const float diff1 = float(query[i]) - float(upper[i]);
        float d = diff0 > diff1 ? diff0 : diff1;
        if (d < 0.0f) d = 0.0f;
        sum += d * d;
    }
    return sum;
}

void knn_kdtree_rec(Dataset dataset, KDTree tree, int node_idx,
                const float* query, SampleDist* heap, int k) {

    const int nfeats = dataset.nfeats;
    if (heap[0].dist != INFINITY) {
        const float max_dist = heap[0].dist;
        if (min_dist_to_box_sq(query, &tree.lower_bounds[node_idx * nfeats], &tree.upper_bounds[node_idx * nfeats],
                    nfeats) >= max_dist) {
            return;
        }
    }

    KDTreeNode node = tree.nodes[node_idx];
    if (node.is_leaf) {
        for (int i = node.start; i < node.end; ++i) {
            const dataset_dtype* point = &dataset.samples[i * nfeats];
            const float dist = squared_l2_dist(query, point, nfeats);
            if (dist < heap[0].dist) {
                pop_heap(heap, heap + k);
                heap[k-1] = SampleDist {
                    .i = i,
                    .dist = dist
                };
                push_heap(heap, heap + k);
            }
        }
        return;
    }

    const int left_child = 2 * node_idx + 1;
    const int right_child = 2 * node_idx + 2;

    const float left_box_dist = min_dist_to_box_sq(query,
            &tree.lower_bounds[left_child * nfeats], &tree.upper_bounds[left_child * nfeats], nfeats);
    const float right_box_dist = min_dist_to_box_sq(query,
            &tree.lower_bounds[right_child * nfeats], &tree.upper_bounds[right_child * nfeats], nfeats);

    if (left_box_dist < right_box_dist) {
        knn_kdtree_rec(dataset, tree, left_child, query, heap, k);
        knn_kdtree_rec(dataset, tree, right_child, query, heap, k);
    } else {
        knn_kdtree_rec(dataset, tree, right_child, query, heap, k);
        knn_kdtree_rec(dataset, tree, left_child, query, heap, k);
    }
}

float knn_kdtree_score(Dataset dataset, KDTree tree, float* query, int k) {
    ScopedTimer _t("knn_kdtree_score");
    SampleDist neighbors[K_NEIGHBORS];
    fill(neighbors, neighbors + k, SampleDist {
            .i = -1,
            .dist = INFINITY
        });
    knn_kdtree_rec(dataset, tree, 0, query, neighbors, k);

    float score = 0.0f;
    for (int i = 0; i < k; ++i) {
        score += dataset.labels[neighbors[i].i];
    }
    return score / k;
}
