// tracing removed during sanitization
const tracer = { currentSpan: () => ({ annotate: () => {} }) };

const mongoClient = require('mongodb').MongoClient;
const mongoObjectID = require('mongodb').ObjectID;
const http = require('http');
const bodyParser = require('body-parser');
const express = require('express');
const pino = require('pino');
const expPino = require('express-pino-logger');

const logger = pino({
    level: 'info',
    prettyPrint: false,
    useLevelLabels: true
});
const expLogger = expPino({
    logger: logger
});

// MongoDB
var db;
var collection;
var mongoConnected = false;

// Ratings service composition
var ratingsHost = process.env.RATINGS_HOST || 'ratings'
var ratingsTimeoutMs = parseInt(process.env.RATINGS_TIMEOUT_MS || '1500', 10)
if(isNaN(ratingsTimeoutMs) || ratingsTimeoutMs <= 0) {
    ratingsTimeoutMs = 1500;
}
const ratingsAgent = new http.Agent({
    keepAlive: true,
    maxSockets: 64
});

// include the product rating in the product response
function fetchRating(sku) {
    return new Promise((resolve) => {
        const req = http.request({
            host: ratingsHost,
            port: 80,
            path: '/api/fetch/' + sku,
            method: 'GET',
            timeout: ratingsTimeoutMs,
            agent: ratingsAgent
        }, (resp) => {
            var data = '';
            resp.on('data', (chunk) => { data += chunk; });
            resp.on('end', () => {
                if(resp.statusCode !== 200) {
                    resolve(null);
                    return;
                }
                try {
                    resolve(JSON.parse(data));
                } catch(e) {
                    resolve(null);
                }
            });
        });

        req.on('timeout', () => {
            req.destroy();
            resolve(null);
        });

        req.on('error', () => resolve(null));
        req.end();
    });
}

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
        mongo: mongoConnected
    };
    res.json(stat);
});

// all products
app.get('/products', (req, res) => {
    if(mongoConnected) {
        collection.find({}).toArray().then((products) => {
            res.json(products);
        }).catch((e) => {
            req.log.error('ERROR', e);
            res.status(500).send(e);
        });
    } else {
        req.log.error('database not available');
        res.status(500).send('database not avaiable');
    }
});

// product by SKU
app.get('/product/:sku', (req, res) => {
    if(mongoConnected) {
        // optionally slow this down
        const delay = process.env.GO_SLOW || 0;
        setTimeout(() => {
        collection.findOne({sku: req.params.sku}).then((product) => {
            req.log.info('product', product);
            if(product) {
                // compose the product with its rating from the ratings service
                fetchRating(req.params.sku).then((rating) => {
                    if(rating) {
                        product.rating = rating;
                    }
                    res.json(product);
                });
            } else {
                res.status(404).send('SKU not found');
            }
        }).catch((e) => {
            req.log.error('ERROR', e);
            res.status(500).send(e);
        });
        }, delay);
    } else {
        req.log.error('database not available');
        res.status(500).send('database not available');
    }
});

// products in a category
app.get('/products/:cat', (req, res) => {
    if(mongoConnected) {
        collection.find({ categories: req.params.cat }).sort({ name: 1 }).toArray().then((products) => {
            if(products) {
                res.json(products);
            } else {
                res.status(404).send('No products for ' + req.params.cat);
            }
        }).catch((e) => {
            req.log.error('ERROR', e);
            res.status(500).send(e);
        });
    } else {
        req.log.error('database not available');
        res.status(500).send('database not avaiable');
    }
});

// all categories
app.get('/categories', (req, res) => {
    if(mongoConnected) {
        collection.distinct('categories').then((categories) => {
            res.json(categories);
        }).catch((e) => {
            req.log.error('ERROR', e);
            res.status(500).send(e);
        });
    } else {
        req.log.error('database not available');
        res.status(500).send('database not available');
    }
});

// search name and description
app.get('/search/:text', (req, res) => {
    if(mongoConnected) {
        collection.find({ '$text': { '$search': req.params.text }}).toArray().then((hits) => {
            res.json(hits);
        }).catch((e) => {
            req.log.error('ERROR', e);
            res.status(500).send(e);
        });
    } else {
        req.log.error('database not available');
        res.status(500).send('database not available');
    }
});

// set up Mongo
function mongoConnect() {
    return new Promise((resolve, reject) => {
        var mongoURL = process.env.MONGO_URL || 'mongodb://mongodb:27017/catalogue';
        mongoClient.connect(mongoURL, (error, client) => {
            if(error) {
                reject(error);
            } else {
                db = client.db('catalogue');
                collection = db.collection('products');
                resolve('connected');
            }
        });
    });
}

// mongodb connection retry loop
function mongoLoop() {
    mongoConnect().then((r) => {
        mongoConnected = true;
        logger.info('MongoDB connected');
    }).catch((e) => {
        logger.error('ERROR', e);
        setTimeout(mongoLoop, 2000);
    });
}

mongoLoop();

// fire it up!
const port = process.env.CATALOGUE_SERVER_PORT || '8080';
app.listen(port, () => {
    logger.info('Started on port', port);
});

