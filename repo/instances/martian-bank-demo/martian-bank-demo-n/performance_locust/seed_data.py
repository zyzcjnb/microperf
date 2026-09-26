# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

"""
Pre-seed a deterministic data pool so the Locust
script no longer performs account/user creation in on_start.

Run this inside the locust container *before* each locust run:
    python seed_data.py

It writes seed_data.json which mixed_parallel_locust.py loads at import time.
"""

import datetime
import json
import os
import random
import time

import requests
from faker import Faker
from pymongo import MongoClient

fake = Faker()

MONGO_URL = os.getenv(
    "SEED_MONGO_URL", "mongodb://root:example@mongo:27017/?authSource=admin"
)
AUTH_URL = os.getenv("SEED_AUTH_URL", "http://customer-auth:8000/api/users")
OUT = os.getenv("SEED_DATA_FILE", "seed_data.json")

N_PAIRS = 150      # sender/receiver account pairs for transactions
N_LOAN = 80        # single accounts for loan applications
N_VIEW = 80        # emails with 2 accounts each for account queries
N_AUTH = 40        # pre-registered auth users
BALANCE = 100000   # high balance to avoid depletion during the run

# Unique run tag so auth e-mails never collide across repeated seeds.
RUN_TAG = int(time.time())


def iban():
    return f"IBAN{random.randint(1000000000000000, 9999999999999999)}"


def make_account(email, atype):
    return {
        "email_id": email,
        "account_type": atype,
        "address": fake.address(),
        "govt_id_number": fake.unique.ssn(),
        "government_id_type": random.choice(
            ["Driver's License", "Passport", "SSN"]
        ),
        "name": fake.name(),
        "balance": BALANCE,
        "currency": "USD",
        "account_number": iban(),
        "created_at": datetime.datetime.now(),
        "seeded": True,
    }


def main():
    client = MongoClient(MONGO_URL)
    db = client["bank"]
    accounts = db["accounts"]

    # Remove previous seed so the accounts collection stays small.
    accounts.delete_many({"seeded": True})

    docs = []
    transfer_pairs = []
    for i in range(N_PAIRS):
        se = f"seed_sender_{i}@example.com"
        re_ = f"seed_receiver_{i}@example.com"
        s = make_account(se, "Checking")
        r = make_account(re_, "Checking")
        docs += [s, r]
        transfer_pairs.append(
            {
                "sender_account_number": s["account_number"],
                "receiver_account_number": r["account_number"],
                "sender_email": se,
                "receiver_email": re_,
            }
        )

    loan_accounts = []
    for i in range(N_LOAN):
        e = f"seed_loan_{i}@example.com"
        a = make_account(e, "Checking")
        docs.append(a)
        loan_accounts.append({"email_id": e, "account_number": a["account_number"]})

    view_emails = []
    for i in range(N_VIEW):
        e = f"seed_view_{i}@example.com"
        docs.append(make_account(e, "Checking"))
        docs.append(make_account(e, "Savings"))
        view_emails.append(e)

    accounts.insert_many(docs)

    # Pre-register auth users via HTTP so the service hashes passwords.
    auth_users = []
    for i in range(N_AUTH):
        email = f"seed_auth_{RUN_TAG}_{i}@example.com"
        password = f"SeedPass{i}!"
        try:
            requests.post(
                f"{AUTH_URL}/",
                json={"name": fake.name(), "email": email, "password": password},
                timeout=10,
            )
        except Exception as exc:  # pragma: no cover - best effort
            print(f"auth register failed for {email}: {exc}")
        auth_users.append({"email": email, "password": password})

    payload = {
        "auth_users": auth_users,
        "transfer_pairs": transfer_pairs,
        "loan_accounts": loan_accounts,
        "view_emails": view_emails,
    }
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(payload, f)

    print(
        f"Seeded {len(docs)} accounts, {len(transfer_pairs)} pairs, "
        f"{len(loan_accounts)} loan accounts, {len(view_emails)} view emails, "
        f"{len(auth_users)} auth users -> {OUT}"
    )


if __name__ == "__main__":
    main()
