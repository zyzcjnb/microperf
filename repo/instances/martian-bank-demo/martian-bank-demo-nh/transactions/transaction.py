# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

from concurrent import futures
import datetime
from bson.objectid import ObjectId
import os
import grpc
from flask import Flask, request, jsonify

from dotmap import DotMap

# Configure the logging settings
import logging

logging.basicConfig(
    level=logging.DEBUG, format="%(asctime)s - %(levelname)s - %(message)s"
)
from transaction_pb2 import *
import transaction_pb2_grpc
from flask import Flask, request, jsonify

from google.protobuf.json_format import MessageToDict

from dotenv import load_dotenv
load_dotenv()

import logging

# set logging to debug
logging.basicConfig(level=logging.DEBUG)

import requests

from pymongo.mongo_client import MongoClient


# db_host = os.getenv("DATABASE_HOST", "localhost")

db_url = os.getenv("DB_URL")
if db_url is None:
    raise Exception("DB_URL environment variable is not set")                   

uri = db_url


# protocol = os.getenv('SERVICE_PROTOCOL')
protocol = os.getenv('SERVICE_PROTOCOL', 'http')
if protocol is None:
    raise Exception("SERVICE_PROTOCOL environment variable is not set")

protocol = protocol.lower()
logging.debug(f"microservice protocol: {protocol}")


# Internal transaction sub-service hosts.
SENDER_RESOLVER_HOST = os.getenv("TRANSACTION_SENDER_RESOLVER_HOST", "localhost")
RECEIVER_RESOLVER_HOST = os.getenv("TRANSACTION_RECEIVER_RESOLVER_HOST", "localhost")
EXECUTOR_HOST = os.getenv("TRANSACTION_EXECUTOR_HOST", "localhost")
SERVICE_PORT = 50052

SENDER_RESOLVER_URL = f"http://{SENDER_RESOLVER_HOST}:{SERVICE_PORT}"
RECEIVER_RESOLVER_URL = f"http://{RECEIVER_RESOLVER_HOST}:{SERVICE_PORT}"
EXECUTOR_URL = f"http://{EXECUTOR_HOST}:{SERVICE_PORT}"


client = MongoClient(uri)
db = client["bank"]
collection_accounts = db["accounts"]
collection_transactions = db["transactions"]


class TransactionGeneric:
    def SendMoney(self, request):
        # delegate sender resolution to a dedicated microservice.
        sender_response = requests.post(
            f"{SENDER_RESOLVER_URL}/resolve-sender",
            json={"sender_account_number": request.sender_account_number},
        )
        sender_account = sender_response.json().get("account")

        # delegate receiver resolution to a dedicated microservice.
        receiver_response = requests.post(
            f"{RECEIVER_RESOLVER_URL}/resolve-receiver",
            json={"receiver_account_number": request.receiver_account_number},
        )
        receiver_account = receiver_response.json().get("account")

        return self.__transfer(sender_account, receiver_account, float(request.amount), request.reason)

    def GetTransactionByID(self, request):
        # read transaction data from the executor microservice.
        response = requests.post(
            f"{EXECUTOR_URL}/transaction-with-id",
            json={"transaction_id": request.transaction_id},
        )
        return response.json()

    def GetTransactionsHistory(self, request):
        # read transaction history from the executor microservice.
        response = requests.post(
            f"{EXECUTOR_URL}/transaction-history",
            json={"account_number": request.account_number},
        )
        return response.json()

    def Zelle(self, request):
        sender_email = request.sender_email
        receiver_email = request.receiver_email
        amount = float(request.amount)
        reason = request.reason

        # resolve sender and receiver via separate microservices.
        sender_response = requests.post(
            f"{SENDER_RESOLVER_URL}/resolve-sender",
            json={"sender_email": sender_email},
        )
        sender_account = sender_response.json().get("account")

        receiver_response = requests.post(
            f"{RECEIVER_RESOLVER_URL}/resolve-receiver",
            json={"receiver_email": receiver_email},
        )
        receiver_account = receiver_response.json().get("account")

        return self.__transfer(sender_account, receiver_account, amount, reason)

    def __transfer(self, sender_account, receiver_account, amount, reason):
        if sender_account is None:
            return {"approved": False, "message": "Sender Account Not Found."}

        if receiver_account is None:
            return {"approved": False, "message": "Receiver Account Not Found."}

        # execute the actual transfer in yet another microservice.
        endpoint = f"{EXECUTOR_URL}/execute-transfer"
        response = requests.post(
            endpoint,
            json={
                "sender": sender_account,
                "receiver": receiver_account,
                "amount": amount,
                "reason": reason,
            },
        )
        return response.json()

    def __getAccountwithEmail(self, email):
        logging.debug(f"Email: {email}")
        # Prefer Checking account, fallback to Savings account.
        document = collection_accounts.find_one(
            {"email_id": email, "account_type": "Checking"}
        )
        if document:
            logging.debug(f"Checking Account: {document}")
            return document
        document = collection_accounts.find_one(
            {"email_id": email, "account_type": "Savings"}
        )
        if document:
            logging.debug(f"Savings Account: {document}")
            return document
        logging.debug("No Account Found")
        return None

    def __getAccount(self, account_num):
        return collection_accounts.find_one({"account_number": account_num})


class TransactionService(transaction_pb2_grpc.TransactionServiceServicer):
    def __init__(self):
        self.transaction = TransactionGeneric()

    def sendMoney(self, request, context):
        t = TransactionResponse()
        result = self.transaction.SendMoney(request)
        t.approved = result["approved"]
        t.message = result["message"]
        return t

    def Zelle(self, request, context):
        result = self.transaction.Zelle(request)
        t = TransactionResponse(approved=result["approved"], message=result["message"])
        return t

    def getTransactionByID(self, request, context):
        result = self.transaction.GetTransactionByID(request)
        if len(result) == 0:
            return Transaction()
        else:
            return Transaction(
                account_number=result["account_number"],
                amount=result["amount"],
                reason=result["reason"],
                time_stamp=result["time_stamp"],
                type="credit",
                transaction_id=result["transaction_id"],
            )

    def getTransactionsHistory(self, request, context):
        results = self.transaction.GetTransactionsHistory(request)
        transactions_list = []
        for t in results:
            temp_t = Transaction(
                account_number=t["account_number"],
                amount=t["amount"],
                reason=t["reason"],
                time_stamp=t["time_stamp"],
                type=t["type"],
                transaction_id=t["transaction_id"],
            )
            transactions_list.append(temp_t)

        return GetALLTransactionsResponse(transactions=transactions_list)





app = Flask(__name__)
transaction_generic = TransactionGeneric()

@app.route("/transfer", methods=["POST"])
def sendMoney():
    data = request.json
    data = DotMap(data)
    result = transaction_generic.SendMoney(data)
    return jsonify(result)

@app.route("/zelle", methods=["POST"])
def zelle():
    logging.debug(" Zelle API called")
    data = request.json
    data = DotMap(data)
    result = transaction_generic.Zelle(data)
    return jsonify(result)

@app.route("/transaction-with-id", methods=["POST"])
def getTransactionByID():
    logging.debug(" Get Transaction By ID API called")
    data = request.json
    data = DotMap(data)
    result = transaction_generic.GetTransactionByID(data)
    return jsonify(result)

@app.route("/transaction-history", methods=["POST"])
def getTransactionsHistory():
    data = request.json
    data = DotMap(data)
    result = transaction_generic.GetTransactionsHistory(data)
    return jsonify(result)



def serverFlask(port):
    logging.debug(f"Starting Flask server on port {port}")
    app.run(host='0.0.0.0' ,port=port, debug=True)


def serverGRPC(port):
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    transaction_pb2_grpc.add_TransactionServiceServicer_to_server(
        TransactionService(), server
    )
    server.add_insecure_port(f"[::]:{port}")
    logging.debug(f"Starting server. Listening on port {port}.")
    server.start()
    server.wait_for_termination()

if __name__ == "__main__":
    port  = 50052
    # serverGRPC(port)
    # serverFlask(port)

    if protocol == "grpc":
        serverGRPC(port)
    else:
        serverFlask(port)
