#!/usr/bin/env xonsh
import json
import os
import tempfile
from pathlib import Path

$RINHA_REPO = "https://github.com/zanfranceschi/rinha-de-backend-2026.git"
$SERVER_URL = os.environ.get("SERVER_URL", "http://localhost:9999")
$TEST_DIR = Path(tempfile.gettempdir()) / "rinha-tests"

if not $TEST_DIR.exists():
    print("Cloning test repo...")
    git clone --depth 1 $RINHA_REPO @($TEST_DIR)

$K6_NO_USAGE_REPORT = "true"

$SMOKE_SCRIPT = str($TEST_DIR / "test" / "smoke.js")
$TEST_SCRIPT = str($TEST_DIR / "test" / "test.js")
$RESULTS_FILE = $TEST_DIR / "test" / "results.json"

print("=== Smoke Test ===")
k6 run --address= $SMOKE_SCRIPT

print("\n=== Full Performance Test ===")
k6 run --address= $TEST_SCRIPT

print("\n=== Results ===")
if $RESULTS_FILE.exists():
    data = json.loads($RESULTS_FILE.read_text())
    print(json.dumps(data, indent=2))
