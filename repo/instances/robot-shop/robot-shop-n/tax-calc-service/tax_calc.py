import os
import sys
import logging
import json

import requests
from flask import Flask
from flask import request
from flask import jsonify

app = Flask(__name__)
app.logger.setLevel(logging.INFO)

RATE_HOST = os.getenv('RATE_HOST', 'tax-rate-service')
RATE_PORT = os.getenv('RATE_PORT', '8080')

session = requests.Session()


def get_rate():
    req = session.get('http://{}:{}/rate'.format(RATE_HOST, RATE_PORT), timeout=(1.0, 2.0))
    req.raise_for_status()
    return float(req.json()['rate'])


@app.route('/health', methods=['GET'])
def health():
    return 'OK'


@app.route('/tax', methods=['GET'])
def tax():
    try:
        amount = float(request.args.get('amount', '0'))
    except ValueError:
        return 'invalid amount', 400

    rate = get_rate()
    # same VAT-inclusive formula the cart used inline
    return jsonify({'tax': amount - (amount / (1.0 + rate))})


if __name__ == "__main__":
    sh = logging.StreamHandler(sys.stdout)
    sh.setLevel(logging.INFO)
    app.logger.addHandler(sh)
    port = int(os.getenv('SERVER_PORT', '8080'))
    app.logger.info('tax-calc starting on port {}'.format(port))
    app.run(host='0.0.0.0', port=port, threaded=True)
