import sys
import json
import requests
import tqdm

def main():
    with open(sys.argv[1]) as fp:
        tests = json.load(fp)
        correct = 0
        total = 0
        wrong = []
        for entry in tqdm.tqdm(tests["entries"]):
            # print(entry["request"])
            expected_score = entry["expected_fraud_score"]
            response = requests.post("http://localhost:9999/fraud-score", json=entry["request"]).json()
            # print(response["fraud_score"], expected_score)
            if expected_score == response["fraud_score"]:
                correct += 1
            else:
                wrong.append(entry["request"])
                if len(wrong) > 10:
                    break
                print("differ")
            total += 1
        print(float(correct)/float(total))
    import pdb; pdb.set_trace()

if __name__ == '__main__':
    main()
