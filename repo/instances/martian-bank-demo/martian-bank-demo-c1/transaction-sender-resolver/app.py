# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

"""
this microservice resolves the sender account
and then forwards the request to the next service (receiver
resolver) instead of returning to an orchestrator.
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

RECEIVER_RESOLVER_HOST = os.getenv("RECEIVER_RESOLVER_HOST", "transaction-receiver-resolver")
RECEIVER_RESOLVER_URL = f"http://{RECEIVER_RESOLVER_HOST}:50052"

client = MongoClient(db_url)
db = client["bank"]
collection_accounts = db["accounts"]

app = Flask(__name__)


def _resolve_account_by_number(account_number):
    return collection_accounts.find_one({"account_number": account_number})


def _resolve_account_by_email(email):
    account = collection_accounts.find_one(
        {"email_id": email, "account_type": "Checking"}
    )
    if account:
        return account
    return collection_accounts.find_one(
        {"email_id": email, "account_type": "Savings"}
    )


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
        account = _serialize_account(account)

    return jsonify({"account": account})


@app.route("/forward-transfer", methods=["POST"])
def forward_transfer():
    """resolve sender and forward to receiver-resolver."""
    data = request.json
    logging.debug(f"forward-transfer called in sender-resolver with: {data}")

    account_number = data.get("sender_account_number")
    email = data.get("sender_email")

    if account_number:
        account = _resolve_account_by_number(account_number)
    elif email:
        account = _resolve_account_by_email(email)
    else:
        return jsonify({"approved": False, "message": "Sender Account Not Found."})

    account = _serialize_account(account)

    if account is None:
        return jsonify({"approved": False, "message": "Sender Account Not Found."})

    # Forward to the next service.
    payload = dict(data)
    payload["sender"] = account
    response = requests.post(
        f"{RECEIVER_RESOLVER_URL}/forward-transfer",
        json=payload,
    )
    return jsonify(response.json())


@app.route("/forward-zelle", methods=["POST"])
def forward_zelle():
    """resolve sender and forward to receiver-resolver."""
    data = request.json
    logging.debug(f"forward-zelle called in sender-resolver with: {data}")

    email = data.get("sender_email")
    account = _resolve_account_by_email(email) if email else None

    account = _serialize_account(account)

    if account is None:
        return jsonify({"approved": False, "message": "Sender Account Not Found."})

    payload = dict(data)
    payload["sender"] = account
    response = requests.post(
        f"{RECEIVER_RESOLVER_URL}/forward-zelle",
        json=payload,
    )
    return jsonify(response.json())


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=50052, debug=True)
