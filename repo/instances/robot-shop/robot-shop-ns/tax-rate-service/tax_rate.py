import os
import sys
import logging

import pymysql
from flask import Flask
from flask import jsonify

app = Flask(__name__)
app.logger.setLevel(logging.INFO)

DB_HOST = os.getenv('DB_HOST', 'mysql')
DB_USER = os.getenv('DB_USER', 'shipping')
DB_PASSWORD = os.getenv('DB_PASSWORD', 'secret')
DB_NAME = os.getenv('DB_NAME', 'tax')


def get_rate():
    conn = pymysql.connect(
        host=DB_HOST,
        user=DB_USER,
        password=DB_PASSWORD,
        database=DB_NAME,
        cursorclass=pymysql.cursors.DictCursor
    )
    try:
        with conn.cursor() as cur:
            cur.execute('SELECT rate FROM tax_rates WHERE id = 1')
            row = cur.fetchone()
            return row['rate'] if row else 0.2
    finally:
        conn.close()


@app.route('/health', methods=['GET'])
def health():
    return 'OK'


@app.route('/rate', methods=['GET'])
def rate():
    # Each tax lookup hits the shared MySQL instance, creating noisy-neighbor
    # contention with shipping cities queries and ratings writes.
    return jsonify({'rate': float(get_rate())})


if __name__ == "__main__":
    sh = logging.StreamHandler(sys.stdout)
    sh.setLevel(logging.INFO)
    app.logger.addHandler(sh)
    port = int(os.getenv('SERVER_PORT', '8080'))
    app.logger.info('tax-rate starting on port {}'.format(port))
    app.run(host='0.0.0.0', port=port, threaded=True)
