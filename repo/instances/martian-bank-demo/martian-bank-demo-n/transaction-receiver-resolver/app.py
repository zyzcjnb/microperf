# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

"""
overly fine-grained service that only resolves
receiver accounts for transfer/zelle operations.
"""

import os
import logging
from flask import Flask, request, jsonify
from pymongo.mongo_client import MongoClient
from dotenv import load_dotenv

load_dotenv()

logging.basicConfig(level=logging.DEBUG)

db_url = os.getenv("DB_URL")
if db_url is None:
    raise Exception("DB_URL environment variable is not set")

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
        account.pop("_id", None)

    return jsonify({"account": account})


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=50052, debug=True)
