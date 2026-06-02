#include <algorithm>
#include <cmath>
#include <cstdio>
#include "knn.h"

using namespace std;

void destroy_dataset(Dataset dataset) {
    delete [] dataset.samples;
    delete [] dataset.labels;
}

static inline double squared_l2_dist(const float* x1, const dataset_dtype* x2, int n) {
    double sum = 0.0f;

    #pragma GCC unroll 14
    for (int i = 0; i < n; ++i) {
        const float diff = float(x1[i]) - float(x2[i]);
        sum += diff * diff;
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
        const double dist = squared_l2_dist(query, &(dataset.samples[i * nfeats]), nfeats);
        if (dist < neighbors[0].dist) {
            pop_heap(neighbors, neighbors + k);
            neighbors[k - 1] = SampleDist{.i = i, .dist = dist};
            push_heap(neighbors, neighbors + k);
        }
    }

    sort_heap(neighbors, neighbors + k);

    double score = 0.0;
    for (int i = 0; i < k; ++i) {
        score += dataset.labels[neighbors[i].i];
    }
    return (float)(score / k);
}
