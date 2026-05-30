#include <cassert>
#include <cstddef>
#include <cstdio>
//#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <string>

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

//{"id":"tx-smoke-001","transaction":{"amount":384.88,"installments":3,"requested_at":"2026-03-11T20:23:35Z"},"customer":{"avg_amount":769.76,"tx_count_24h":3,"known_merchants":["MERC-009","MERC-001","MERC-001"]},"merchant":{"id":"MERC-001","mcc":"5912","avg_amount":298.95},"terminal":{"is_online":false,"card_present":true,"km_from_home":13.7090520965},"last_transaction":{"timestamp":"2026-03-11T14:58:35Z","km_from_current":18.8626479774}}

struct FraudScoreRequest parse_fraud_score_request(const std::string& body) {
    struct FraudScoreRequest r = {};
    sscanf(body.c_str(),
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
    const size_t nbytes = sprintf(response, "{\"approved\": %s, \"fraud_score\": %4.2f}", bool_str[score > 0.6], score);
    return nbytes;
}

void fraud_score_endpoint(const httplib::Request& req, httplib::Response& res) {
    const struct FraudScoreRequest r = parse_fraud_score_request(req.body);

    char response[64];
    const size_t nbytes = serialize_fraud_score_response(0.2, response);
    assert(nbytes <= 64);
    res.set_content(response, nbytes, "application/json");
}

void ready_endpoint(const httplib::Request& req, httplib::Response& res) {
    res.set_content("ok", "text/plain");
}

int main() {
    const int http_port = 9999;
    printf("%ld\n", sizeof(FraudScoreRequest));
    httplib::Server server;
    server.set_keep_alive_max_count(1);
    server.Post("/fraud-score", fraud_score_endpoint);
    server.Get("/ready", ready_endpoint);

    printf("Server listening on port %d\n", http_port);
    server.listen("0.0.0.0", http_port);

    return 0;
}
