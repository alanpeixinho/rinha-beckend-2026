#pragma once

#include <concepts>
#include <stdfloat>
#include <cstdint>
#include <type_traits>

using dataset_dtype = std::int16_t;
using bounds_dtype  = std::int16_t;

template <typename dtype>
inline float to_float(dtype val) {
    if constexpr (std::is_same_v<dtype, std::int16_t>) {
        //float16_t can cause precision errors
        //this way we have 4 decimal places, which seems to be enough
        return float(val) * 0.0001f;
    } else {
        return float(val);
    }
}

struct Dataset {
    int nsamples, nfeats;
    dataset_dtype* samples = nullptr;
    bool* labels = nullptr;
};

struct SampleDist {
    int i;
    float dist;
    const bool operator<(const SampleDist& sd) {
        return dist < sd.dist;
    }
};

struct __attribute__((packed)) KDTreeNode {
    int start;
    int end;
    bool is_leaf;
};

constexpr int K_NEIGHBORS = 5;

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
