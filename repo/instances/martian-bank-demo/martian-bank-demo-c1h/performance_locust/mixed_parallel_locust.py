# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

"""
Mixed parallel load test.

Load-test notes:
  - TransactionUser and LoanUser carry a larger share of the aggregate load.
  - All accounts/users come from a pre-seeded pool (seed_data.py) instead
    of on_start creation. Run `python seed_data.py` before starting locust.
"""

import itertools
import json
import os
import random

from api_urls import ApiUrls
from faker import Faker
from locust import HttpUser, SequentialTaskSet, between, task

fake = Faker()

FORM_HEADERS = {"Content-Type": "application/x-www-form-urlencoded"}

# ---------------------------------------------------------------------------
# Load pre-seeded data pool.
# ---------------------------------------------------------------------------
SEED_FILE = os.getenv("SEED_DATA_FILE", "seed_data.json")
with open(SEED_FILE, "r", encoding="utf-8") as _f:
    SEED = json.load(_f)

AUTH_USERS = SEED["auth_users"]
TRANSFER_PAIRS = SEED["transfer_pairs"]
LOAN_ACCOUNTS = SEED["loan_accounts"]
VIEW_EMAILS = SEED["view_emails"]

# Round-robin pair assignment so concurrent users do not drain one account.
_pair_counter = itertools.count()


def _next_pair():
    return TRANSFER_PAIRS[next(_pair_counter) % len(TRANSFER_PAIRS)]


# ---------------------------------------------------------------------------
# Auth
# ---------------------------------------------------------------------------
class AuthTasks(SequentialTaskSet):
    wait_time = between(2, 3)

    def on_start(self):
        u = random.choice(AUTH_USERS)
        self.user_data = {"email": u["email"], "password": u["password"]}

    @task
    def login(self):
        self.client.post(
            "/auth",
            json={
                "email": self.user_data["email"],
                "password": self.user_data["password"],
            },
        )

    @task
    def get_profile(self):
        self.client.post("/profile", json={"email": self.user_data["email"]})

    @task
    def update_profile(self):
        new_password = fake.unique.password()
        self.client.put(
            "/profile",
            json={"email": self.user_data["email"], "password": new_password},
        )
        self.user_data["password"] = new_password

    @task
    def logout(self):
        self.client.post("/logout", json={"email": self.user_data["email"]})


class AuthUser(HttpUser):
    host = ApiUrls["VITE_USERS_URL"]
    tasks = [AuthTasks]
    weight = 1


# ---------------------------------------------------------------------------
# ATM (data seeded on service boot)
# ---------------------------------------------------------------------------
class AtmTasks(SequentialTaskSet):
    wait_time = between(2, 3)

    @task
    def get_all_atms(self):
        response = self.client.post("/")
        self.atm_data = response.json() if response.content else []

    @task
    def get_atm_details(self):
        for atm in getattr(self, "atm_data", []):
            self.client.get(f"/{atm['_id']}")


class AtmUser(HttpUser):
    host = ApiUrls["VITE_ATM_URL"]
    tasks = [AtmTasks]
    weight = 1


# ---------------------------------------------------------------------------
# Accounts
# ---------------------------------------------------------------------------
class AccountTasks(SequentialTaskSet):
    wait_time = between(2, 3)

    def on_start(self):
        self.email_id = random.choice(VIEW_EMAILS)

    @task
    def get_all_accounts(self):
        self.client.post(
            "/allaccounts",
            data={"email_id": self.email_id},
            headers=FORM_HEADERS,
        )

    @task
    def get_particular_account(self):
        self.client.get(
            "/detail",
            data={"email": self.email_id},
            headers=FORM_HEADERS,
        )


class AccountUser(HttpUser):
    host = ApiUrls["VITE_ACCOUNTS_URL"]
    tasks = [AccountTasks]
    weight = 1


# ---------------------------------------------------------------------------
# Transactions
# ---------------------------------------------------------------------------
class TransactionTasks(SequentialTaskSet):
    wait_time = between(2, 3)

    def on_start(self):
        pair = _next_pair()
        self.sender_account_number = pair["sender_account_number"]
        self.receiver_account_number = pair["receiver_account_number"]
        self.sender_email = pair["sender_email"]
        self.receiver_email = pair["receiver_email"]

    @task
    def internal_transfer(self):
        self.client.post(
            "/",
            data={
                "sender_account_number": self.sender_account_number,
                "receiver_account_number": self.receiver_account_number,
                "amount": fake.random_int(min=1, max=3),
                "reason": "Internal Transfer",
            },
            headers=FORM_HEADERS,
        )

    @task
    def external_transfer(self):
        self.client.post(
            "/zelle/",
            data={
                "sender_email": self.sender_email,
                "receiver_email": self.receiver_email,
                "amount": fake.random_int(min=1, max=3),
                "reason": "External Transfer",
            },
            headers=FORM_HEADERS,
        )

    @task
    def transaction_history(self):
        self.client.post(
            "/history",
            data={"account_number": self.sender_account_number},
            headers=FORM_HEADERS,
        )


class TransactionUser(HttpUser):
    host = ApiUrls["VITE_TRANSFER_URL"]
    tasks = [TransactionTasks]
    weight = 4


# ---------------------------------------------------------------------------
# Loan
# ---------------------------------------------------------------------------
class LoanTasks(SequentialTaskSet):
    wait_time = between(2, 3)

    def on_start(self):
        acc = random.choice(LOAN_ACCOUNTS)
        self.account_number = acc["account_number"]
        self.email_id = acc["email_id"]
        self.user_data = {
            "name": fake.name(),
            "email_id": self.email_id,
            "account_type": "Checking",
            "government_id_type": random.choice(
                ["Driver's License", "Passport", "SSN"]
            ),
            "govt_id_number": fake.unique.ssn(),
            "address": fake.address(),
        }

    @task
    def apply(self):
        req = dict(self.user_data)
        req["email"] = req["email_id"]
        req["govt_id_type"] = req["government_id_type"]
        req["account_number"] = self.account_number
        req["interest_rate"] = random.randint(1, 10)
        req["time_period"] = random.randint(1, 10)
        req["loan_amount"] = random.randint(1000, 10000)
        req["loan_type"] = random.choice(
            ["Base Camp", "Rover", "Potato Farming", "Ice Home", "Rocker"]
        )

        self.client.post("/", data=req, headers=FORM_HEADERS)

    @task
    def history(self):
        self.client.post(
            "/history",
            data={"email": self.email_id},
            headers=FORM_HEADERS,
        )


class LoanUser(HttpUser):
    host = ApiUrls["VITE_LOAN_URL"]
    tasks = [LoanTasks]
    weight = 2
