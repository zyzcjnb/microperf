# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

"""
service that only executes transfers after
sender/receiver resolution has been done by other microservices.
"""

import os
import datetime
import logging
import requests
from bson.objectid import ObjectId
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
collection_transactions = db["transactions"]

# route all account balance updates through the
# accounts service instead of writing to the accounts collection directly.
ACCOUNTS_HOST = os.getenv("ACCOUNT_HOST", "accounts")
ACCOUNTS_PORT = os.getenv("ACCOUNT_PORT", "50051")
ACCOUNTS_BASE_URL = f"http://{ACCOUNTS_HOST}:{ACCOUNTS_PORT}"


def _update_balance(account_number, balance):
    resp = requests.post(
        f"{ACCOUNTS_BASE_URL}/update-balance",
        json={"account_number": account_number, "balance": balance},
    )
    return resp.status_code == 200


app = Flask(__name__)


@app.route("/execute-transfer", methods=["POST"])
def execute_transfer():
    """Execute a transfer using pre-resolved sender/receiver accounts."""
    data = request.json
    sender = data.get("sender")
    receiver = data.get("receiver")
    amount = float(data.get("amount", 0))
    reason = data.get("reason", "")

    if sender is None:
        return jsonify({"approved": False, "message": "Sender Account Not Found."})
    if receiver is None:
        return jsonify({"approved": False, "message": "Receiver Account Not Found."})

    if sender["balance"] < amount:
        return jsonify({"approved": False, "message": "Insufficient Balance"})

    sender["balance"] -= amount
    receiver["balance"] += amount

    _update_balance(sender["account_number"], sender["balance"])
    _update_balance(receiver["account_number"], receiver["balance"])
    collection_transactions.insert_one(
        {
            "sender": sender["account_number"],
            "receiver": receiver["account_number"],
            "amount": amount,
            "reason": reason,
            "time_stamp": datetime.datetime.now(),
        }
    )

    return jsonify({"approved": True, "message": "Transaction is Successful."})


@app.route("/forward-execute", methods=["POST"])
def forward_execute():
    """Execute transfer with pre-resolved accounts."""
    data = request.json
    sender = data.get("sender")
    receiver = data.get("receiver")
    amount = float(data.get("amount", 0))
    reason = data.get("reason", "")

    if sender is None:
        return jsonify({"approved": False, "message": "Sender Account Not Found."})
    if receiver is None:
        return jsonify({"approved": False, "message": "Receiver Account Not Found."})

    if sender["balance"] < amount:
        return jsonify({"approved": False, "message": "Insufficient Balance"})

    sender["balance"] -= amount
    receiver["balance"] += amount

    _update_balance(sender["account_number"], sender["balance"])
    _update_balance(receiver["account_number"], receiver["balance"])
    collection_transactions.insert_one(
        {
            "sender": sender["account_number"],
            "receiver": receiver["account_number"],
            "amount": amount,
            "reason": reason,
            "time_stamp": datetime.datetime.now(),
        }
    )

    return jsonify({"approved": True, "message": "Transaction is Successful."})


@app.route("/execute-zelle", methods=["POST"])
def execute_zelle():
    """Execute a zelle transfer using pre-resolved sender/receiver accounts."""
    data = request.json
    sender = data.get("sender")
    receiver = data.get("receiver")
    amount = float(data.get("amount", 0))
    reason = data.get("reason", "")

    if sender is None:
        return jsonify({"approved": False, "message": "Sender Account Not Found."})
    if receiver is None:
        return jsonify({"approved": False, "message": "Receiver Account Not Found."})

    if sender["balance"] < amount:
        return jsonify({"approved": False, "message": "Insufficient Balance"})

    sender["balance"] -= amount
    receiver["balance"] += amount

    _update_balance(sender["account_number"], sender["balance"])
    _update_balance(receiver["account_number"], receiver["balance"])
    collection_transactions.insert_one(
        {
            "sender": sender["account_number"],
            "receiver": receiver["account_number"],
            "amount": amount,
            "reason": reason,
            "time_stamp": datetime.datetime.now(),
        }
    )

    return jsonify({"approved": True, "message": "Transaction is Successful."})


@app.route("/transaction-history", methods=["POST"])
def get_transactions_history():
    data = request.json
    account_number = data.get("account_number")

    transactions_credit = collection_transactions.find({"sender": account_number})
    transactions_debit = collection_transactions.find({"receiver": account_number})

    transactions_list = []
    for t in transactions_credit:
        transactions_list.append(
            {
                "account_number": t["receiver"],
                "amount": t["amount"],
                "reason": t["reason"],
                "time_stamp": f"{t['time_stamp']}",
                "type": "credit",
                "transaction_id": str(t["_id"]),
            }
        )
    for t in transactions_debit:
        transactions_list.append(
            {
                "account_number": t["receiver"],
                "amount": t["amount"],
                "reason": t["reason"],
                "time_stamp": f"{t['time_stamp']}",
                "type": "credit",
                "transaction_id": str(t["_id"]),
            }
        )

    return jsonify(transactions_list)


@app.route("/transaction-with-id", methods=["POST"])
def get_transaction_by_id():
    data = request.json
    transaction_id = data.get("transaction_id")
    logging.debug(f"Transaction ID: {transaction_id}")

    count = collection_transactions.count_documents({"_id": ObjectId(transaction_id)})
    if count == 0:
        return jsonify({})

    transaction = collection_transactions.find_one({"_id": ObjectId(transaction_id)})
    return jsonify(
        {
            "account_number": transaction["receiver"],
            "amount": transaction["amount"],
            "reason": transaction["reason"],
            "time_stamp": f"{transaction['time_stamp']}",
            "type": "credit",
            "transaction_id": str(transaction["_id"]),
        }
    )


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=50052, debug=True)
