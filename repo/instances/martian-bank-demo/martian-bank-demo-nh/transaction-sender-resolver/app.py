# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

"""
overly fine-grained service that only resolves
sender accounts for transfer/zelle operations.
"""

import os
import logging
from flask import Flask, request, jsonify
from pymongo.mongo_client import MongoClient
from dotenv import load_dotenv

import requests as http_requests

load_dotenv()

logging.basicConfig(level=logging.DEBUG)

db_url = os.getenv("DB_URL")
if db_url is None:
    raise Exception("DB_URL environment variable is not set")

client = MongoClient(db_url)
db = client["bank"]
collection_accounts = db["accounts"]

# resolve accounts through the accounts service
ACCOUNTS_HOST = os.getenv("ACCOUNT_HOST", "accounts")
ACCOUNTS_PORT = os.getenv("ACCOUNT_PORT", "50051")
ACCOUNTS_BASE_URL = f"http://{ACCOUNTS_HOST}:{ACCOUNTS_PORT}"


def _get_account(account_number):
    resp = http_requests.post(
        f"{ACCOUNTS_BASE_URL}/account-detail",
        json={"account_number": account_number},
    )
    if resp.status_code == 200:
        data = resp.json()
        if data:
            return data
    return None


def _get_account_with_email(email):
    resp = http_requests.post(
        f"{ACCOUNTS_BASE_URL}/get-all-accounts",
        json={"email_id": email},
    )
    if resp.status_code == 200:
        accounts = resp.json()
        checking = [a for a in accounts if a.get("account_type") == "Checking"]
        if checking:
            return checking[0]
        savings = [a for a in accounts if a.get("account_type") == "Savings"]
        if savings:
            return savings[0]
    return None


app = Flask(__name__)


def _resolve_account_by_number(account_number):
    # resolve via accounts service.
    return _get_account(account_number)


def _resolve_account_by_email(email):
    # resolve via accounts service.
    return _get_account_with_email(email)


@app.route("/resolve-sender", methods=["POST"])
def resolve_sender():
    """Resolve sender account by account_number or email."""
    data = request.json
    logging.debug(f"resolve-sender called with: {data}")

    account_number = data.get("sender_account_number")
    email = data.get("sender_email")

    if account_number:
        account = _resolve_account_by_number(account_number)
    elif email:
        account = _resolve_account_by_email(email)
    else:
        return jsonify({"account": None, "error": "No sender identifier provided"})

    if account:
        # Strip MongoDB internal _id to keep response JSON serializable
        account.pop("_id", None)

    return jsonify({"account": account})


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=50052, debug=True)
