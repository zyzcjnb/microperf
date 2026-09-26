import os
import sys
import logging

import pymysql
from flask import Flask
from flask import jsonify
from flask import Response

app = Flask(__name__)
app.logger.setLevel(logging.INFO)

DB_HOST = os.getenv('DB_HOST', 'mysql')
DB_USER = os.getenv('DB_USER', 'shipping')
DB_PASSWORD = os.getenv('DB_PASSWORD', 'secret')
DB_NAME = os.getenv('DB_NAME', 'products')


def query_price(sku):
    conn = pymysql.connect(
        host=DB_HOST,
        user=DB_USER,
        password=DB_PASSWORD,
        database=DB_NAME,
        cursorclass=pymysql.cursors.DictCursor
    )
    try:
        with conn.cursor() as cur:
            cur.execute('SELECT sku, price FROM product_price WHERE sku = %s', (sku,))
            return cur.fetchone()
    finally:
        conn.close()


@app.route('/health', methods=['GET'])
def health():
    return 'OK'


@app.route('/price/<sku>', methods=['GET'])
def price(sku):
    row = query_price(sku)
    if not row:
        return Response('not found', status=404)
    return jsonify({'sku': row['sku'], 'price': float(row['price'])})


if __name__ == "__main__":
    sh = logging.StreamHandler(sys.stdout)
    sh.setLevel(logging.INFO)
    app.logger.addHandler(sh)
    port = int(os.getenv('SERVER_PORT', '8080'))
    app.logger.info('product-price starting on port {}'.format(port))
    app.run(host='0.0.0.0', port=port, threaded=True)
