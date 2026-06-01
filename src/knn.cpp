#include <algorithm>
#include <cmath>
#include <cstdio>
#include "knn.h"

using namespace std;

void destroy_dataset(Dataset dataset) {
    delete [] dataset.samples;
    delete [] dataset.labels;
}

inline float squared_l2_dist(const float* x1, const float* x2, int n) {
    float sum = 0.0f;

    #pragma omp simd reduction(+:sum) aligned(x1, x2: 32)
    for (int i = 0; i < n; ++i) {
        const float diff = x1[i] - x2[i];
        sum += diff * diff;
    }
    return sum;
}

Dataset load_dataset(const char* filepath) {
    int nrows, ncols;
    float* data;
    bool* labels;
    FILE* f = fopen(filepath, "rb");
    fread(&nrows, sizeof(int), 1, f);
    fread(&ncols, sizeof(int), 1, f);
    data = new float[nrows * ncols];
    labels = new bool[nrows * ncols];
    fread(data, sizeof(float), nrows * ncols, f);
    fread(labels, sizeof(bool), nrows * ncols, f);
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

    fill(neighbors,  neighbors +k, SampleDist {-1, INFINITY});

    // max heap
    make_heap(neighbors, neighbors + k);

    for (int i = 0; i < nsamples; ++i) {
        const float dist = squared_l2_dist(query, &(dataset.samples[i * nfeats]), nfeats);
        if (dist < neighbors[0].dist) {
            pop_heap(neighbors, neighbors + k);
            neighbors[k-1] = SampleDist {
                .i = i,
                .dist = dist
            };
            push_heap(neighbors, neighbors + k);
        }
    }

    float score = 0.0;
    for (int i = 0; i < k; ++i) {
        score += dataset.labels[neighbors[i].i];
    }
    return score / k;
}
