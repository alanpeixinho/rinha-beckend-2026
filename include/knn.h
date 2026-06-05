#pragma once

#include <stdfloat>
#include <cstdint>
#include <type_traits>

using dataset_dtype = std::int16_t;
using bounds_dtype  = std::int16_t;

template <typename dtype>
inline float to_float(dtype val) {
    if constexpr (std::is_same_v<dtype, std::int16_t>) {
        //float16_t can cause precision errors
        //this way we have more than decimal places, which seems to be enough
        return float(val) * 0.00005f;
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

// bounds per node: [l0..l(nfeats-1), u0..u(nfeats-1)] — two contiguous float blocks
// total stride per node = nfeats * 2
struct KDTree {
    int nnodes, nfeats;
    KDTreeNode* nodes = nullptr;
    bounds_dtype* bounds = nullptr;
};

Dataset load_dataset(const char* filepath);
void destroy_dataset(Dataset dataset);
float knn_bf_score(const Dataset dataset, const float* query, int k);

KDTree load_kdtree(const char* filepath);
void destroy_kdtree(KDTree);
float knn_kdtree_score(Dataset dataset, KDTree tree, float* query, int k);
