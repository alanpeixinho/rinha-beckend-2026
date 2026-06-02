#include "knn.h"
#include "server.h"
#include <simdjson.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace std;

constexpr int MAX_KNOWN_MERCHANTS = 8;

struct FraudScoreRequest {
    char id[16];
    char merchant_id[12];
    char mcc[8];
    char known_merchants[MAX_KNOWN_MERCHANTS][16];
    int known_merchants_count;
    float amount;
    int installments;
    char requested_at[32];
    float cust_avg_amount;
    int tx_count_24h;
    float merchant_avg_amount;
    float km_from_home;
    int is_online;
    int card_present;
    int has_last_tx;
    char last_tx_timestamp[32];
    float km_from_current;
};

using namespace simdjson;

struct FraudScoreRequest parse_fraud_score_request(const char* body) {
    struct FraudScoreRequest r = {};
    size_t len = strlen(body);

    dom::parser parser;
    dom::element doc;
    if (parser.parse(body, len).get(doc) != SUCCESS) return r;

    auto get_str = [](simdjson_result<dom::element> el, char* dst, int sz) {
        dom::element e;
        if (el.get(e) == SUCCESS) {
            const char* s;
            if (e.get_c_str().get(s) == SUCCESS) {
                size_t n = strlen(s);
                if (n >= (size_t)sz) n = sz - 1;
                memcpy(dst, s, n);
                dst[n] = '\0';
            }
        }
    };

    get_str(doc["id"], r.id, sizeof(r.id));

    dom::element tx_el;
    if (doc["transaction"].get(tx_el) == SUCCESS) {
        double d;
        if (tx_el["amount"].get_double().get(d) == SUCCESS) r.amount = (float)d;
        int64_t i;
        if (tx_el["installments"].get_int64().get(i) == SUCCESS) r.installments = (int)i;
        get_str(tx_el["requested_at"], r.requested_at, sizeof(r.requested_at));
    }

    dom::element cust_el;
    if (doc["customer"].get(cust_el) == SUCCESS) {
        double d;
        if (cust_el["avg_amount"].get_double().get(d) == SUCCESS) r.cust_avg_amount = (float)d;
        int64_t i;
        if (cust_el["tx_count_24h"].get_int64().get(i) == SUCCESS) r.tx_count_24h = (int)i;

        dom::array km_arr;
        if (cust_el["known_merchants"].get_array().get(km_arr) == SUCCESS) {
            int idx = 0;
            for (dom::element el : km_arr) {
                if (idx >= MAX_KNOWN_MERCHANTS) break;
                const char* s;
                if (el.get_c_str().get(s) == SUCCESS) {
                    size_t n = strlen(s);
                    if (n >= 16) n = 15;
                    memcpy(r.known_merchants[idx], s, n);
                    r.known_merchants[idx][n] = '\0';
                    idx++;
                }
            }
            r.known_merchants_count = idx;
        }
    }

    dom::element merch_el;
    if (doc["merchant"].get(merch_el) == SUCCESS) {
        get_str(merch_el["id"], r.merchant_id, sizeof(r.merchant_id));
        get_str(merch_el["mcc"], r.mcc, sizeof(r.mcc));
        double d;
        if (merch_el["avg_amount"].get_double().get(d) == SUCCESS) r.merchant_avg_amount = (float)d;
    }

    dom::element term_el;
    if (doc["terminal"].get(term_el) == SUCCESS) {
        bool b;
        if (term_el["is_online"].get_bool().get(b) == SUCCESS) r.is_online = b ? 1 : 0;
        if (term_el["card_present"].get_bool().get(b) == SUCCESS) r.card_present = b ? 1 : 0;
        double d;
        if (term_el["km_from_home"].get_double().get(d) == SUCCESS) r.km_from_home = (float)d;
    }

    dom::element last_tx_el;
    if (doc["last_transaction"].get(last_tx_el) == SUCCESS && !last_tx_el.is_null()) {
        r.has_last_tx = 1;
        get_str(last_tx_el["timestamp"], r.last_tx_timestamp, sizeof(r.last_tx_timestamp));
        double d;
        if (last_tx_el["km_from_current"].get_double().get(d) == SUCCESS) r.km_from_current = (float)d;
    }

    return r;
}

float mcc_map(const char* mcc) {
        if (strcmp(mcc, "5411") == 0)
            return 0.15;
        if (strcmp(mcc, "5812") == 0)
            return 0.30;
        if (strcmp(mcc, "5912") == 0)
            return 0.20;
        if (strcmp(mcc, "5944") == 0)
            return 0.45;
        if (strcmp(mcc, "7801") == 0)
            return 0.80;
        if (strcmp(mcc, "7802") == 0)
            return 0.75;
        if (strcmp(mcc, "7995") == 0)
            return 0.85;
        if (strcmp(mcc, "4511") == 0)
            return 0.35;
        if (strcmp(mcc, "5311") == 0)
            return 0.25;
        if (strcmp(mcc, "5999") == 0)
            return 0.50;
        return 0.50;
}

bool is_known_merchant(const char* merchant_id, const char (*known_merchants)[16], int count) {
    for (int i = 0; i < count; ++i) {
        if (strcmp(merchant_id, known_merchants[i]) == 0)
            return true;
    }
    return false;
}

// Parse "YYYY-MM-DDThh:mm:ss" manually — no sscanf
static bool parse_iso8601(const char* ts, int& y, int& m, int& d, int& h, int& mi, int& s) {
    if (!ts || ts[0] < '0') return false;
    y = (ts[0]-'0')*1000 + (ts[1]-'0')*100 + (ts[2]-'0')*10 + (ts[3]-'0');
    if (ts[4] != '-') return false;
    m = (ts[5]-'0')*10 + (ts[6]-'0');
    if (ts[7] != '-') return false;
    d = (ts[8]-'0')*10 + (ts[9]-'0');
    if (ts[10] != 'T') return false;
    h = (ts[11]-'0')*10 + (ts[12]-'0');
    if (ts[13] != ':') return false;
    mi = (ts[14]-'0')*10 + (ts[15]-'0');
    if (ts[16] != ':') return false;
    s = (ts[17]-'0')*10 + (ts[18]-'0');
    return true;
}

static int day_of_week(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    y -= m < 3;
    int dow = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
    return (dow + 6) % 7;  // shift to Mon=0 .. Sun=6
}

static long long epoch_seconds(int y, int m, int d, int h, int mi, int s) {
    static const int md[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    y--;
    long long days = (long long)y * 365 + y/4 - y/100 + y/400;
    days -= 719162LL;
    days += md[m-1] + d - 1;
    if (m > 2 && ((y+1) % 4 == 0 && ((y+1) % 100 != 0 || (y+1) % 400 == 0)))
        days++;
    return days * 86400LL + h * 3600LL + mi * 60LL + s;
}

void request_to_vector(FraudScoreRequest r, float* feats) {
    const float max_amount = 10000;
    const float max_installments = 12;
    const float amount_vs_avg_ratio = 10;
    const float max_minutes = 1440;
    const float max_km =  1000;
    const float max_tx_count_24h = 20;
    const float max_merchant_avg_amount = 10000;

    float avg = r.cust_avg_amount > 0.0f ? r.cust_avg_amount : 1.0f;

    feats[0] = clamp(r.amount / max_amount, 0.0f, 1.0f);
    feats[1] = clamp(float(r.installments) / max_installments, 0.0f, 1.0f);
    feats[2] = clamp((r.amount / avg) / amount_vs_avg_ratio, 0.0f, 1.0f);

    int y, mon, d, h, mi, s;
    if (parse_iso8601(r.requested_at, y, mon, d, h, mi, s)) {
        feats[3] = float(h) / 23.0f;
        feats[4] = float(day_of_week(y, mon, d)) / 6.0f;

        if (r.has_last_tx) {
            int y2, mon2, d2, h2, mi2, s2;
            if (parse_iso8601(r.last_tx_timestamp, y2, mon2, d2, h2, mi2, s2)) {
                long long e1 = epoch_seconds(y, mon, d, h, mi, s);
                long long e2 = epoch_seconds(y2, mon2, d2, h2, mi2, s2);
                double mins = double(e1 - e2) / 60.0;
                feats[5] = clamp(float(mins) / max_minutes, 0.0f, 1.0f);
            } else {
                feats[5] = -1.0f;
            }
            feats[6] = clamp(r.km_from_current / max_km, 0.0f, 1.0f);
        } else {
            feats[5] = -1.0f;
            feats[6] = -1.0f;
        }
    } else {
        feats[3] = 0.0f;
        feats[4] = 0.0f;
        feats[5] = -1.0f;
        feats[6] = -1.0f;
    }

    feats[7] = clamp(r.km_from_home / max_km, 0.0f, 1.0f);
    feats[8] = clamp(float(r.tx_count_24h) / max_tx_count_24h, 0.0f, 1.0f);
    feats[9] = float(r.is_online);
    feats[10] = float(r.card_present);

    bool known = is_known_merchant(r.merchant_id, r.known_merchants, r.known_merchants_count);
    feats[11] = known ? 0.0f : 1.0f;

    float mcc = mcc_map(r.mcc);
    feats[12] = mcc;
    feats[13] = clamp(r.merchant_avg_amount / max_merchant_avg_amount, 0.0f, 1.0f);

    // Quantize all features to match training data (short/10000)
    for (int i = 0; i < 14; i++) {
        feats[i] = roundf(feats[i] * 10000.0f) / 10000.0f;
    }
}


size_t serialize_fraud_score_response(float score, char* response) {
    const char* bool_str[2] = {"false", "true"};
    return (size_t)sprintf(response, "{\"approved\": %s, \"fraud_score\": %2.1f}", bool_str[score <= 0.6f], score);
}

static Dataset dataset;
static KDTree kdtree;

static int fraud_score_handler(const char* body, char* resp, int resp_sz) {
    const int max_feats = 14;
    float query[max_feats];
    struct FraudScoreRequest r = parse_fraud_score_request(body);
    request_to_vector(r, query);
    const float score = knn_kdtree_score(dataset, kdtree, query, 5);
    return (int)serialize_fraud_score_response(score, resp);
}

static int ready_handler(const char* body, char* resp, int resp_sz) {
    strcpy(resp, "ok");
    return 2;
}

int main(int argc, const char** argv) {
    dataset = load_dataset(argv[1]);
    kdtree = load_kdtree(argv[2]);

    register_route("POST", "/fraud-score", fraud_score_handler);
    register_route("GET", "/ready", ready_handler);

    const char* sock_path = getenv("UDS_PATH");
    if (!sock_path) sock_path = "/sockets/api1.sock";

    run_server(sock_path);

    destroy_dataset(dataset);
    destroy_kdtree(kdtree);

    return 0;
}
