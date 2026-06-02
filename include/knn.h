#pragma once

#include <stdfloat>

using dataset_dtype = std::float16_t;
using bounds_dtype = std::float32_t;

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

struct __attribute__((packed)) KDTreeNode {
    int start;
    int end;
    bool is_leaf;
};

struct KDTree {
    int nnodes, nfeats;
    KDTreeNode* nodes = nullptr;
    bounds_dtype* lower_bounds = nullptr;
    bounds_dtype* upper_bounds = nullptr;
};

Dataset load_dataset(const char* filepath);
void destroy_dataset(Dataset dataset);
float knn_bf_score(const Dataset dataset, const float* query, int k);

KDTree load_kdtree(const char* filepath);
void destroy_kdtree(KDTree);
float knn_kdtree_score(Dataset dataset, KDTree tree, float* query, int k);
