#pragma once

#include <stdfloat>

using dataset_dtype = std::float16_t;

struct Dataset {
    int nsamples, nfeats;
    dataset_dtype* samples = nullptr;
    bool* labels = nullptr;
};

struct SampleDist {
    int i;
    double dist;
    const bool operator<(const SampleDist& sd) {
        return dist < sd.dist;
    }
};

Dataset load_dataset(const char* filepath);
void destroy_dataset(Dataset dataset);
float knn_bf_score(const Dataset dataset, const float* query, int k);
