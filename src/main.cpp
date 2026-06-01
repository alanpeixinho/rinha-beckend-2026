#include "knn.h"
#include "server.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

using namespace std;

struct FraudScoreRequest {
    char id[16];
    char merchant_id[12];
    char mcc[8];
    char known_merchants[1024];
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

struct FraudScoreRequest parse_fraud_score_request(const char* body) {
    struct FraudScoreRequest r = {};
    r.has_last_tx = strstr(body, "\"last_transaction\": null") == NULL
                 && strstr(body, "\"last_transaction\":null") == NULL;

    char is_online_str[6] = {0};
    char card_present_str[6] = {0};

    if (r.has_last_tx) {
        sscanf(body,
            "{\"id\": \"%63[^\"]\","
            "\"transaction\": {\"amount\": %f, \"installments\": %d, \"requested_at\": \"%31[^\"]\"},"
            "\"customer\": {\"avg_amount\": %f, \"tx_count_24h\": %d, \"known_merchants\": %1023[^]]]},"
            "\"merchant\": {\"id\": \"%15[^\"]\", \"mcc\": \"%7[^\"]\", \"avg_amount\": %f},"
            "\"terminal\": {\"is_online\": %5[a-z], \"card_present\": %5[a-z], \"km_from_home\": %f},"
            "\"last_transaction\": {\"timestamp\": \"%31[^\"]\", \"km_from_current\": %f}}",
            r.id, &r.amount, &r.installments, r.requested_at,
            &r.cust_avg_amount, &r.tx_count_24h, r.known_merchants,
            r.merchant_id, r.mcc, &r.merchant_avg_amount,
            is_online_str, card_present_str, &r.km_from_home,
            r.last_tx_timestamp, &r.km_from_current
        );
    } else {
        sscanf(body,
            "{\"id\": \"%63[^\"]\","
            "\"transaction\": {\"amount\": %f, \"installments\": %d, \"requested_at\": \"%31[^\"]\"},"
            "\"customer\": {\"avg_amount\": %f, \"tx_count_24h\": %d, \"known_merchants\": %1023[^]]]},"
            "\"merchant\": {\"id\": \"%15[^\"]\", \"mcc\": \"%7[^\"]\", \"avg_amount\": %f},"
            "\"terminal\": {\"is_online\": %5[a-z], \"card_present\": %5[a-z], \"km_from_home\": %f}}",
            r.id, &r.amount, &r.installments, r.requested_at,
            &r.cust_avg_amount, &r.tx_count_24h, r.known_merchants,
            r.merchant_id, r.mcc, &r.merchant_avg_amount,
            is_online_str, card_present_str, &r.km_from_home
        );
    }

    r.is_online = strcmp(is_online_str, "true") == 0 ? 1 : 0;
    r.card_present = strcmp(card_present_str, "true") == 0 ? 1 : 0;

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
        if (strcmp(mcc, "7955") == 0)
            return 0.85;
        if (strcmp(mcc, "4511") == 0)
            return 0.35;
        if (strcmp(mcc, "5311") == 0)
            return 0.25;
        return 0.50;
}

bool is_known_merchant(const char* merchant_id, const char* known_merchants) {
    if (known_merchants[0] == '\0') return false;
    char buf[1024];
    strcpy(buf, known_merchants);
    char* p = strtok(buf + 1, "\",");
    while (p != nullptr) {
        if (strcmp(merchant_id, p) == 0)
            return true;
        p = strtok(nullptr, "\",");
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
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
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

    bool known = is_known_merchant(r.merchant_id, r.known_merchants);
    feats[11] = known ? 0.0f : 1.0f;

    float mcc = mcc_map(r.mcc);
    feats[12] = mcc;
    feats[13] = clamp(r.merchant_avg_amount / max_merchant_avg_amount, 0.0f, 1.0f);
}


size_t serialize_fraud_score_response(float score, char* response) {
    const char* bool_str[2] = {"false", "true"};
    return (size_t)sprintf(response, "{\"approved\": %s, \"fraud_score\": %2.1f}", bool_str[score > 0.6], score);
}

static Dataset dataset;

static int fraud_score_handler(const char* body, char* resp, int resp_sz) {
    const int max_feats = 14;
    float query[max_feats];
    struct FraudScoreRequest r = parse_fraud_score_request(body);
    request_to_vector(r, query);
    const float score = 0.2f;
    return (int)serialize_fraud_score_response(score, resp);
}

static int ready_handler(const char* body, char* resp, int resp_sz) {
    strcpy(resp, "ok");
    return 2;
}

int main(int argc, const char** argv) {
    dataset = load_dataset(argv[1]);
    printf("%d %d\n", dataset.nsamples, dataset.nfeats);
    for (int i = 0; i < dataset.nfeats; ++i) {
        printf("%f ", dataset.samples[i]);
    }
    printf("\n");

    register_route("POST", "/fraud-score", fraud_score_handler);
    register_route("GET", "/ready", ready_handler);

    const char* port_str = getenv("PORT");
    int port = port_str ? atoi(port_str) : 8081;

    run_server_tcp(port);

    destroy_dataset(dataset);

    return 0;
}
