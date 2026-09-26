# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

"""
this microservice resolves the receiver account
and then forwards the request to the next service (executor)
instead of returning to an orchestrator.
"""

import os
import logging
import datetime
import requests
from flask import Flask, request, jsonify
from pymongo.mongo_client import MongoClient
from dotenv import load_dotenv

load_dotenv()

logging.basicConfig(level=logging.DEBUG)

db_url = os.getenv("DB_URL")
if db_url is None:
    raise Exception("DB_URL environment variable is not set")

EXECUTOR_HOST = os.getenv("EXECUTOR_HOST", "transaction-executor")
EXECUTOR_URL = f"http://{EXECUTOR_HOST}:50052"

# resolve accounts through the accounts service
ACCOUNTS_HOST = os.getenv("ACCOUNT_HOST", "accounts")
ACCOUNTS_PORT = os.getenv("ACCOUNT_PORT", "50051")
ACCOUNTS_BASE_URL = f"http://{ACCOUNTS_HOST}:{ACCOUNTS_PORT}"

client = MongoClient(db_url)
db = client["bank"]
collection_accounts = db["accounts"]

app = Flask(__name__)


def _get_account(account_number):
    resp = requests.post(
        f"{ACCOUNTS_BASE_URL}/account-detail",
        json={"account_number": account_number},
    )
    if resp.status_code == 200:
        data = resp.json()
        if data:
            return data
    return None


def _get_account_with_email(email):
    resp = requests.post(
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


def _resolve_account_by_number(account_number):
    # resolve via accounts service.
    return _get_account(account_number)


def _resolve_account_by_email(email):
    # resolve via accounts service.
    return _get_account_with_email(email)


def _serialize_account(account):
    """Make a MongoDB account document JSON-serializable for forwarding."""
    if account is None:
        return None
    serialized = {}
    for key, value in account.items():
        if key == "_id":
            continue
        if isinstance(value, datetime.datetime):
            serialized[key] = value.isoformat()
        else:
            serialized[key] = value
    return serialized


@app.route("/resolve-receiver", methods=["POST"])
def resolve_receiver():
    """Resolve receiver account by account_number or email."""
    data = request.json
    logging.debug(f"resolve-receiver called with: {data}")

    account_number = data.get("receiver_account_number")
    email = data.get("receiver_email")

    if account_number:
        account = _resolve_account_by_number(account_number)
    elif email:
        account = _resolve_account_by_email(email)
    else:
        return jsonify({"account": None, "error": "No receiver identifier provided"})

    if account:
        account = _serialize_account(account)

    return jsonify({"account": account})


@app.route("/forward-transfer", methods=["POST"])
def forward_transfer():
    """resolve receiver and forward to executor."""
    data = request.json
    logging.debug(f"forward-transfer called in receiver-resolver with: {data}")

    sender = data.get("sender")
    if sender is None:
        return jsonify({"approved": False, "message": "Sender Account Not Found."})

    account_number = data.get("receiver_account_number")
    email = data.get("receiver_email")

    if account_number:
        account = _resolve_account_by_number(account_number)
    elif email:
        account = _resolve_account_by_email(email)
    else:
        account = None

    account = _serialize_account(account)

    if account is None:
        return jsonify({"approved": False, "message": "Receiver Account Not Found."})

    # Forward to the next service.
    response = requests.post(
        f"{EXECUTOR_URL}/forward-execute",
        json={
            "sender": sender,
            "receiver": account,
            "amount": data.get("amount"),
            "reason": data.get("reason", ""),
        },
    )
    return jsonify(response.json())


@app.route("/forward-zelle", methods=["POST"])
def forward_zelle():
    """resolve receiver and forward to executor."""
    data = request.json
    logging.debug(f"forward-zelle called in receiver-resolver with: {data}")

    sender = data.get("sender")
    if sender is None:
        return jsonify({"approved": False, "message": "Sender Account Not Found."})

    email = data.get("receiver_email")
    account = _resolve_account_by_email(email) if email else None

    account = _serialize_account(account)

    if account is None:
        return jsonify({"approved": False, "message": "Receiver Account Not Found."})

    response = requests.post(
        f"{EXECUTOR_URL}/forward-execute",
        json={
            "sender": sender,
            "receiver": account,
            "amount": data.get("amount"),
            "reason": data.get("reason", ""),
        },
    )
    return jsonify(response.json())


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=50052, debug=True)
