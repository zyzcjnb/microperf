// tracing removed during sanitization
const tracer = { currentSpan: () => ({ annotate: () => {} }) };

const mysql = require('mysql2');
const request = require('request');
const bodyParser = require('body-parser');
const express = require('express');
const pino = require('pino');
const expPino = require('express-pino-logger');
// Prometheus
const promClient = require('prom-client');
const Registry = promClient.Registry;
const register = new Registry();
const counter = new promClient.Counter({
    name: 'items_added',
    help: 'running count of items added to cart',
    registers: [register]
});


var mysqlConnected = false;

var mysqlHost = process.env.MYSQL_HOST || 'mysql'
var mysqlUser = process.env.MYSQL_USER || 'shipping'
var mysqlPassword = process.env.MYSQL_PASSWORD || 'secret'
var mysqlDatabase = process.env.MYSQL_DATABASE || 'carts'
var catalogueHost = process.env.CATALOGUE_HOST || 'catalogue'
var catalogueTimeoutMs = parseInt(process.env.CATALOGUE_TIMEOUT_MS || '1500', 10)
if(isNaN(catalogueTimeoutMs) || catalogueTimeoutMs <= 0) {
    catalogueTimeoutMs = 1500;
}

var catalogueMaxSockets = parseInt(process.env.CATALOGUE_MAX_SOCKETS || '64', 10)
if(isNaN(catalogueMaxSockets) || catalogueMaxSockets <= 0) {
    catalogueMaxSockets = 64;
}

const catalogueRequest = request.defaults({
    timeout: catalogueTimeoutMs,
    forever: true,
    pool: {
        maxSockets: catalogueMaxSockets
    }
});

const logger = pino({
    level: 'info',
    prettyPrint: false,
    useLevelLabels: true
});
const expLogger = expPino({
    logger: logger
});

const app = express();

app.use(expLogger);

app.use((req, res, next) => {
    res.set('Timing-Allow-Origin', '*');
    res.set('Access-Control-Allow-Origin', '*');
    next();
});

app.use((req, res, next) => {
    let dcs = [
        "asia-northeast2",
        "asia-south1",
        "europe-west3",
        "us-east1",
        "us-west1"
    ];
    let span = tracer.currentSpan();
    span.annotate('custom.sdk.tags.datacenter', dcs[Math.floor(Math.random() * dcs.length)]);

    next();
});

app.use(bodyParser.urlencoded({ extended: true }));
app.use(bodyParser.json());

app.get('/health', (req, res) => {
    var stat = {
        app: 'OK',
        mysql: mysqlConnected
    };
    res.json(stat);
});

// Prometheus
app.get('/metrics', (req, res) => {
    res.header('Content-Type', 'text/plain');
    res.send(register.metrics());
});


// get cart with id
app.get('/cart/:id', (req, res) => {
    getCart(req.params.id, (err, data) => {
        if(err) {
            req.log.error('ERROR', err);
            res.status(500).send(err);
        } else {
            if(data == null) {
                res.status(404).send('cart not found');
            } else {
                res.set('Content-Type', 'application/json');
                res.send(data);
            }
        }
    });
});

// delete cart with id
app.delete('/cart/:id', (req, res) => {
    deleteCart(req.params.id, (err, data) => {
        if(err) {
            req.log.error('ERROR', err);
            res.status(500).send(err);
        } else {
            if(data == 1) {
                res.send('OK');
            } else {
                res.status(404).send('cart not found');
            }
        }
    });
});

// rename cart i.e. at login
app.get('/rename/:from/:to', (req, res) => {
    getCart(req.params.from, (err, data) => {
        if(err) {
            req.log.error('ERROR', err);
            res.status(500).send(err);
        } else {
            if(data == null) {
                res.status(404).send('cart not found');
            } else {
                var cart = JSON.parse(data);
                saveCart(req.params.to, cart).then((data) => {
                    res.json(cart);
                }).catch((err) => {
                    req.log.error(err);
                    res.status(500).send(err);
                });
            }
        }
    });
});

// update/create cart
app.get('/add/:id/:sku/:qty', (req, res) => {
    // check quantity
    var qty = parseInt(req.params.qty);
    if(isNaN(qty)) {
        req.log.warn('quantity not a number');
        res.status(400).send('quantity must be a number');
        return;
    } else if(qty < 1) {
        req.log.warn('quantity less than one');
        res.status(400).send('quantity has to be greater than zero');
        return;
    }

    // look up product details
    getProduct(req.params.sku).then((product) => {
        req.log.info('got product', product);
        if(!product) {
            res.status(404).send('product not found');
            return;
        }
        // is the product in stock?
        if(product.instock == 0) {
            res.status(404).send('out of stock');
            return;
        }
        // does the cart already exist?
        getCart(req.params.id, (err, data) => {
            if(err) {
                req.log.error('ERROR', err);
                res.status(500).send(err);
            } else {
                var cart;
                if(data == null) {
                    // create new cart
                    cart = {
                        total: 0,
                        tax: 0,
                        items: []
                    };
                } else {
                    cart = JSON.parse(data);
                }
                req.log.info('got cart', cart);
                // add sku to cart
                var item = {
                    qty: qty,
                    sku: req.params.sku,
                    name: product.name,
                    price: product.price,
                    subtotal: qty * product.price
                };
                var list = mergeList(cart.items, item, qty);
                cart.items = list;
                cart.total = calcTotal(cart.items);
                // work out tax
                cart.tax = calcTax(cart.total);

                // save the new cart
                saveCart(req.params.id, cart).then((data) => {
                    counter.inc(qty);
                    res.json(cart);
                }).catch((err) => {
                    req.log.error(err);
                    res.status(500).send(err);
                });
            }
        });
    }).catch((err) => {
        req.log.error(err);
        res.status(500).send(err);
    });
});

// update quantity - remove item when qty == 0
app.get('/update/:id/:sku/:qty', (req, res) => {
    // check quantity
    var qty = parseInt(req.params.qty);
    if(isNaN(qty)) {
        req.log.warn('quanity not a number');
        res.status(400).send('quantity must be a number');
        return;
    } else if(qty < 0) {
        req.log.warn('quantity less than zero');
        res.status(400).send('negative quantity not allowed');
        return;
    }

    // get the cart
    getCart(req.params.id, (err, data) => {
        if(err) {
            req.log.error('ERROR', err);
            res.status(500).send(err);
        } else {
            if(data == null) {
                res.status(404).send('cart not found');
            } else {
                var cart = JSON.parse(data);
                var idx;
                var len = cart.items.length;
                for(idx = 0; idx < len; idx++) {
                    if(cart.items[idx].sku == req.params.sku) {
                        break;
                    }
                }
                if(idx == len) {
                    // not in list
                    res.status(404).send('not in cart');
                } else {
                    if(qty == 0) {
                        cart.items.splice(idx, 1);
                    } else {
                        cart.items[idx].qty = qty;
                        cart.items[idx].subtotal = cart.items[idx].price * qty;
                    }
                    cart.total = calcTotal(cart.items);
                    // work out tax
                    cart.tax = calcTax(cart.total);
                    saveCart(req.params.id, cart).then((data) => {
                        res.json(cart);
                    }).catch((err) => {
                        req.log.error(err);
                        res.status(500).send(err);
                    });
                }
            }
        }
    });
});

// add shipping
app.post('/shipping/:id', (req, res) => {
    var shipping = req.body;
    if(shipping.distance === undefined || shipping.cost === undefined || shipping.location == undefined) {
        req.log.warn('shipping data missing', shipping);
        res.status(400).send('shipping data missing');
    } else {
        // get the cart
        getCart(req.params.id, (err, data) => {
            if(err) {
                req.log.error('ERROR', err);
                res.status(500).send(err);
            } else {
                if(data == null) {
                    req.log.info('no cart for', req.params.id);
                    res.status(404).send('cart not found');
                } else {
                    var cart = JSON.parse(data);
                    var item = {
                        qty: 1,
                        sku: 'SHIP',
                        name: 'shipping to ' + shipping.location,
                        price: shipping.cost,
                        subtotal: shipping.cost
                    };
                    // check shipping already in the cart
                    var idx;
                    var len = cart.items.length;
                    for(idx = 0; idx < len; idx++) {
                        if(cart.items[idx].sku == item.sku) {
                            break;
                        }
                    }
                    if(idx == len) {
                        // not already in cart
                        cart.items.push(item);
                    } else {
                        cart.items[idx] = item;
                    }
                    cart.total = calcTotal(cart.items);
                    // work out tax
                    cart.tax = calcTax(cart.total);

                    // save the updated cart
                    saveCart(req.params.id, cart).then((data) => {
                        res.json(cart);
                    }).catch((err) => {
                        req.log.error(err);
                        res.status(500).send(err);
                    });
                }
            }
        });
    }
});

function mergeList(list, product, qty) {
    var inlist = false;
    // loop through looking for sku
    var idx;
    var len = list.length;
    for(idx = 0; idx < len; idx++) {
        if(list[idx].sku == product.sku) {
            inlist = true;
            break;
        }
    }

    if(inlist) {
        list[idx].qty += qty;
        list[idx].subtotal = list[idx].price * list[idx].qty;
    } else {
        list.push(product);
    }

    return list;
}

function calcTotal(list) {
    var total = 0;
    for(var idx = 0, len = list.length; idx < len; idx++) {
        total += list[idx].subtotal;
    }

    return total;
}

function calcTax(total) {
    // tax @ 20%
    return (total - (total / 1.2));
}

function getProduct(sku) {
    return new Promise((resolve, reject) => {
        catalogueRequest('http://' + catalogueHost + ':8080/product/' + sku, (err, res, body) => {
            if(err) {
                reject(err);
            } else if(res.statusCode != 200) {
                resolve(null);
            } else {
                // return object - body is a string
                // TODO - catch parse error
                resolve(JSON.parse(body));
            }
        });
    });
}

function getCart(id, cb) {
    pool.query('SELECT cart FROM carts WHERE id = ?', [id], (err, results) => {
        if(err) {
            return cb(err, null);
        }
        if(results.length === 0) {
            return cb(null, null);
        }
        // mysql JSON column returns a parsed object; downstream expects a JSON string
        var cartData = results[0].cart;
        if(typeof cartData === 'object') {
            cartData = JSON.stringify(cartData);
        }
        cb(null, cartData);
    });
}

function deleteCart(id, cb) {
    pool.query('DELETE FROM carts WHERE id = ?', [id], (err, results) => {
        if(err) {
            return cb(err, null);
        }
        cb(null, results.affectedRows);
    });
}

function saveCart(id, cart) {
    logger.info('saving cart', cart);
    return new Promise((resolve, reject) => {
        var sql = 'INSERT INTO carts (id, cart) VALUES (?, ?) ON DUPLICATE KEY UPDATE cart = VALUES(cart)';
        pool.query(sql, [id, JSON.stringify(cart)], (err, data) => {
            if(err) {
                reject(err);
            } else {
                resolve(data);
            }
        });
    });
}

// connect to the shared MySQL instance
var pool = mysql.createPool({
    host: mysqlHost,
    user: mysqlUser,
    password: mysqlPassword,
    database: mysqlDatabase,
    connectionLimit: 64,
    waitForConnections: true,
    queueLimit: 0
});

pool.on('error', (e) => {
    logger.error('MySQL ERROR', e);
});

pool.query('SELECT 1', (e) => {
    if(e) {
        logger.error('MySQL connection failed', e);
    } else {
        logger.info('Shared MySQL connected');
        mysqlConnected = true;
    }
});

// fire it up!
const port = process.env.CART_SERVER_PORT || '8080';
app.listen(port, () => {
    logger.info('Started on port', port);
});

