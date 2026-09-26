import random

import monitoring
import os
import sys
import time
import logging
import uuid
import json
import requests
import traceback
from flask import Flask
from flask import Response
from flask import request
from flask import jsonify
from rabbitmq import Publisher
# Prometheus
import prometheus_client
from prometheus_client import Counter, Histogram

app = Flask(__name__)
app.logger.setLevel(logging.INFO)

CART = os.getenv('CART_HOST', 'cart')
USER = os.getenv('USER_HOST', 'user')
DISPATCH = os.getenv('DISPATCH_HOST', 'dispatch')
DISPATCH_SYNC_ENABLED = os.getenv('DISPATCH_SYNC_ENABLED', '0') == '1'
PAYMENT_GATEWAY = os.getenv('PAYMENT_GATEWAY', 'https://paypal.com/')
GATEWAY_ENABLED = os.getenv('PAYMENT_GATEWAY_ENABLED', '1') == '1'
CONNECT_TIMEOUT_SEC = float(os.getenv('PAYMENT_CONNECT_TIMEOUT_SEC', '1.0'))
READ_TIMEOUT_SEC = float(os.getenv('PAYMENT_READ_TIMEOUT_SEC', '2.0'))
REQUEST_TIMEOUT = (CONNECT_TIMEOUT_SEC, READ_TIMEOUT_SEC)
session = requests.Session()

# Prometheus
PromMetrics = {}
PromMetrics['SOLD_COUNTER'] = Counter('sold_count', 'Running count of items sold')
PromMetrics['AUS'] = Histogram('units_sold', 'Avergae Unit Sale', buckets=(1, 2, 5, 10, 100))
PromMetrics['AVS'] = Histogram('cart_value', 'Avergae Value Sale', buckets=(100, 200, 500, 1000, 2000, 5000, 10000))


@app.errorhandler(Exception)
def exception_handler(err):
    app.logger.error(str(err))
    return str(err), 500

@app.route('/health', methods=['GET'])
def health():
    return 'OK'

# Prometheus
@app.route('/metrics', methods=['GET'])
def metrics():
    res = []
    for m in PromMetrics.values():
        res.append(prometheus_client.generate_latest(m))

    return Response(res, mimetype='text/plain')


@app.route('/pay/<id>', methods=['POST'])
def pay(id):
    app.logger.info('payment for {}'.format(id))
    cart = request.get_json()
    app.logger.info(cart)

    anonymous_user = True

    # check user exists
    try:
        req = session.get('http://{user}:8080/check/{id}'.format(user=USER, id=id), timeout=REQUEST_TIMEOUT)
    except requests.exceptions.RequestException as err:
        app.logger.error(err)
        return str(err), 500
    if req.status_code == 200:
        anonymous_user = False

    # check that the cart is valid
    # this will blow up if the cart is not valid
    has_shipping = False
    for item in cart.get('items'):
        if item.get('sku') == 'SHIP':
            has_shipping = True

    if cart.get('total', 0) == 0 or has_shipping == False:
        app.logger.warn('cart not valid')
        return 'cart not valid', 400

    if GATEWAY_ENABLED:
        # dummy call to payment gateway, hope they dont object
        try:
            req = session.get(PAYMENT_GATEWAY, timeout=REQUEST_TIMEOUT)
            app.logger.info('{} returned {}'.format(PAYMENT_GATEWAY, req.status_code))
        except requests.exceptions.RequestException as err:
            app.logger.error(err)
            return str(err), 500
        if req.status_code != 200:
            return 'payment error', req.status_code
    else:
        app.logger.info('payment gateway call skipped (PAYMENT_GATEWAY_ENABLED=0)')

    # Prometheus
    # items purchased
    item_count = countItems(cart.get('items', []))
    PromMetrics['SOLD_COUNTER'].inc(item_count)
    PromMetrics['AUS'].observe(item_count)
    PromMetrics['AVS'].observe(cart.get('total', 0))

    # Generate order id
    orderid = str(uuid.uuid4())
    queueOrder({ 'orderid': orderid, 'user': id, 'cart': cart })

    # add to order history
    if not anonymous_user:
        try:
            req = session.post('http://{user}:8080/order/{id}'.format(user=USER, id=id),
                    data=json.dumps({'orderid': orderid, 'cart': cart}),
                    headers={'Content-Type': 'application/json'},
                    timeout=REQUEST_TIMEOUT)
            app.logger.info('order history returned {}'.format(req.status_code))
        except requests.exceptions.RequestException as err:
            app.logger.error(err)
            return str(err), 500

    # delete cart
    try:
        req = session.delete('http://{cart}:8080/cart/{id}'.format(cart=CART, id=id), timeout=REQUEST_TIMEOUT);
        app.logger.info('cart delete returned {}'.format(req.status_code))
    except requests.exceptions.RequestException as err:
        app.logger.error(err)
        return str(err), 500
    if req.status_code != 200:
        return 'order history update error', req.status_code

    return jsonify({ 'orderid': orderid })


def queueOrder(order):
    app.logger.info('queue order')

    # For screenshot demo requirements optionally add in a bit of delay
    delay = int(os.getenv('PAYMENT_DELAY_MS', 0))
    time.sleep(delay / 1000)

    if DISPATCH_SYNC_ENABLED:
        # Order dispatch
        try:
            req = session.post('http://{dispatch}:8080/dispatch'.format(dispatch=DISPATCH),
                    data=json.dumps(order),
                    headers={'Content-Type': 'application/json'},
                    timeout=REQUEST_TIMEOUT)
            app.logger.info('sync dispatch returned {}'.format(req.status_code))
            if req.status_code != 200:
                raise Exception('dispatch returned {}'.format(req.status_code))
        except requests.exceptions.RequestException as err:
            app.logger.error(err)
            raise
        return

    headers = {}
    publisher.publish(order, headers)


def countItems(items):
    count = 0
    for item in items:
        if item.get('sku') != 'SHIP':
            count += item.get('qty')

    return count


# RabbitMQ
publisher = Publisher(app.logger)

if __name__ == "__main__":
    sh = logging.StreamHandler(sys.stdout)
    sh.setLevel(logging.INFO)
    fmt = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    app.logger.info('Payment gateway {}'.format(PAYMENT_GATEWAY))
    port = int(os.getenv("SHOP_PAYMENT_PORT", "8080"))
    app.logger.info('Starting on port {}'.format(port))
    app.run(host='0.0.0.0', port=port)
