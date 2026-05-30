#include "server.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct FraudScoreRequest {
    char id[16];
    char merchant_id[12];
    char mcc[8];
    float amount;
    int installments;
    float cust_avg_amount;
    int tx_count_24h;
    float merchant_avg_amount;
    float km_from_home;
    float km_from_current;
};

struct FraudScoreRequest parse_fraud_score_request(const char* body) {
    struct FraudScoreRequest r = {};
    sscanf(body,
        "{\"id\":\"%63[^\"]\","
        "\"transaction\":{\"amount\":%f,\"installments\":%d,\"requested_at\":%*[^}]},"
        "\"customer\":{\"avg_amount\":%f,\"tx_count_24h\":%d,\"known_merchants\":%*[^]]]},"
        "\"merchant\":{\"id\":\"%15[^\"]\",\"mcc\":\"%7[^\"]\",\"avg_amount\":%f},"
        "\"terminal\":{\"is_online\":%*[^,],\"card_present\":%*[^,],\"km_from_home\":%f},"
        "\"last_transaction\":{\"timestamp\":%*[^,],\"km_from_current\":%f}}",
        r.id, &r.amount, &r.installments,
        &r.cust_avg_amount, &r.tx_count_24h,
        r.merchant_id, r.mcc, &r.merchant_avg_amount,
        &r.km_from_home, &r.km_from_current
    );
    return r;
}

size_t serialize_fraud_score_response(float score, char* response) {
    const char* bool_str[2] = {"false", "true"};
    return (size_t)sprintf(response, "{\"approved\": %s, \"fraud_score\": %4.2f}", bool_str[score > 0.6], score);
}

static int fraud_score_handler(const char* body, char* resp, int resp_sz) {
    (void)resp_sz;
    struct FraudScoreRequest r = parse_fraud_score_request(body);
    (void)r;
    return (int)serialize_fraud_score_response(0.2f, resp);
}

static int ready_handler(const char* body, char* resp, int resp_sz) {
    (void)body;
    (void)resp_sz;
    resp[0] = 'o';
    resp[1] = 'k';
    return 2;
}

int main() {
    register_route("POST", "/fraud-score", fraud_score_handler);
    register_route("GET", "/ready", ready_handler);

    const char* sock_path = getenv("UDS_PATH");
    if (!sock_path) sock_path = "/sockets/api1.sock";

    return run_server(sock_path);
}
