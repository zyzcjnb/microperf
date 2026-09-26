// tracing removed during sanitization
const tracer = { currentSpan: () => ({ annotate: () => {} }) };

const redis = require('redis');
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


var redisConnected = false;

var redisHost = process.env.REDIS_HOST || 'redis'
var catalogueHost = process.env.CATALOGUE_HOST || 'catalogue'
var splitProductApiEnabled = process.env.SPLIT_PRODUCT_API !== '0'

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
        redis: redisConnected
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
    redisClient.get(req.params.id, (err, data) => {
        if(err) {
            req.log.error('ERROR', err);
            res.status(500).send(err);
        } else {
            if(data == null) {
                res.status(404).send('cart not found');
            } else {
                if(splitProductApiEnabled) {
                    var cartForRead;
                    try {
                        cartForRead = JSON.parse(data);
                    } catch(e) {
                        req.log.warn('unable to parse cart for read validation', e);
                    }

                    if(cartForRead) {
                        runCartValidation(cartForRead, null).then(() => {
                            res.set('Content-Type', 'application/json');
                            res.send(data);
                        });
                        return;
                    }
                }

                res.set('Content-Type', 'application/json');
                res.send(data);
            }
        }
    });
});

// delete cart with id
app.delete('/cart/:id', (req, res) => {
    redisClient.del(req.params.id, (err, data) => {
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
    redisClient.get(req.params.from, (err, data) => {
        if(err) {
            req.log.error('ERROR', err);
            res.status(500).send(err);
        } else {
            if(data == null) {
                res.status(404).send('cart not found');
            } else {
                var cart = JSON.parse(data);
                runCartValidation(cart, null).then(() => {
                    saveCart(req.params.to, cart).then((data) => {
                        res.json(cart);
                    }).catch((err) => {
                        req.log.error(err);
                        res.status(500).send(err);
                    });
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
        redisClient.get(req.params.id, (err, data) => {
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
    redisClient.get(req.params.id, (err, data) => {
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
                    runCartValidation(cart, req.params.sku).then(() => {
                        saveCart(req.params.id, cart).then((data) => {
                            res.json(cart);
                        }).catch((err) => {
                            req.log.error(err);
                            res.status(500).send(err);
                        });
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
        redisClient.get(req.params.id, (err, data) => {
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
                    runCartValidation(cart, null).then(() => {
                        saveCart(req.params.id, cart).then((data) => {
                            res.json(cart);
                        }).catch((err) => {
                            req.log.error(err);
                            res.status(500).send(err);
                        });
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
    if(splitProductApiEnabled) {
        return getProductViaSplitApi(sku);
    }

    return fetchProductOnce(sku);
}

function fetchProductOnce(sku) {
    return new Promise((resolve, reject) => {
        request('http://' + catalogueHost + ':8080/product/' + sku, (err, res, body) => {
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

function getProductViaSplitApi(sku) {
    // decompose the product lookup into fine-grained calls, one per field
    return requestJson('http://' + catalogueHost + ':8080/product/' + sku).then((baseProduct) => {
        return requestJson('http://' + catalogueHost + ':8080/product/' + sku + '/name').then((nameData) => {
            return requestJson('http://' + catalogueHost + ':8080/product/' + sku + '/price').then((priceData) => {
                return requestJson('http://' + catalogueHost + ':8080/product/' + sku + '/stock').then((stockData) => {
                    return requestJson('http://' + catalogueHost + ':8080/product/' + sku + '/categories').then((categoriesData) => {
                        var product = {
                            sku: sku,
                            name: nameData.name,
                            price: priceData.price,
                            instock: stockData.instock,
                            categories: categoriesData.categories
                        };

                        // Preserve functional behavior if a field-level call is inconsistent.
                        if(product.name === undefined) {
                            product.name = baseProduct.name;
                        }
                        if(product.price === undefined) {
                            product.price = baseProduct.price;
                        }
                        if(product.instock === undefined) {
                            product.instock = baseProduct.instock;
                        }
                        if(product.categories === undefined) {
                            product.categories = baseProduct.categories;
                        }

                        return product;
                    });
                });
            });
        });
    }).catch((e) => {
        logger.warn('split product API failed, fallback to /product/:sku', e);
        return fetchProductOnce(sku);
    });
}

function runCartValidation(cart, focusSku) {
    if(!splitProductApiEnabled) {
        return Promise.resolve();
    }

    var skus = [];
    if(focusSku && focusSku !== 'SHIP') {
        skus.push(focusSku);
    }

    if(cart && Array.isArray(cart.items)) {
        cart.items.forEach((item) => {
            if(!item || item.sku === 'SHIP') {
                return;
            }
            if(skus.indexOf(item.sku) === -1) {
                skus.push(item.sku);
            }
        });
    }

    var chain = Promise.resolve();
    skus.forEach((sku) => {
        chain = chain.then(() => {
            return getProductViaSplitApi(sku).catch((e) => {
                logger.warn('cart validation failed for sku ' + sku, e);
                return null;
            });
        });
    });

    return chain;
}

function requestJson(url) {
    return new Promise((resolve, reject) => {
        request(url, (err, res, body) => {
            if(err) {
                reject(err);
            } else if(res.statusCode != 200) {
                resolve(null);
            } else {
                resolve(JSON.parse(body));
            }
        });
    }).then((data) => {
        if(data === null) {
            throw new Error('not found');
        }
        return data;
    });
}

function saveCart(id, cart) {
    logger.info('saving cart', cart);
    return new Promise((resolve, reject) => {
        redisClient.setex(id, 3600, JSON.stringify(cart), (err, data) => {
            if(err) {
                reject(err);
            } else {
                resolve(data);
            }
        });
    });
}

// connect to Redis
var redisClient = redis.createClient({
    host: redisHost
});

redisClient.on('error', (e) => {
    logger.error('Redis ERROR', e);
});
redisClient.on('ready', (r) => {
    logger.info('Redis READY', r);
    redisConnected = true;
});

// fire it up!
const port = process.env.CART_SERVER_PORT || '8080';
app.listen(port, () => {
    logger.info('Started on port', port);
});

