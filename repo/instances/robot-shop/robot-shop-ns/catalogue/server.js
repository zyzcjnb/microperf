// tracing removed during sanitization
const tracer = { currentSpan: () => ({ annotate: () => {} }) };

const mongoClient = require('mongodb').MongoClient;
const mongoObjectID = require('mongodb').ObjectID;
const bodyParser = require('body-parser');
const express = require('express');
const http = require('http');
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

// product by SKU - aggregate from product services
const productInfoHost = process.env.PRODUCT_INFO_HOST || 'product-info-service';
const productPriceHost = process.env.PRODUCT_PRICE_HOST || 'product-price-service';
const productStockHost = process.env.PRODUCT_STOCK_HOST || 'product-stock-service';

function serviceRequest(host, path) {
    return new Promise((resolve, reject) => {
        const options = {
            hostname: host,
            port: 8080,
            path: path,
            method: 'GET',
            timeout: 5000
        };
        const request = http.request(options, (response) => {
            let data = '';
            response.on('data', (chunk) => {
                data += chunk;
            });
            response.on('end', () => {
                if(response.statusCode >= 200 && response.statusCode < 300) {
                    try {
                        resolve(JSON.parse(data));
                    } catch(e) {
                        reject(e);
                    }
                } else {
                    reject(new Error('Product service returned ' + response.statusCode));
                }
            });
        });
        request.on('error', (e) => reject(e));
        request.on('timeout', () => {
            request.destroy();
            reject(new Error('Product service timeout'));
        });
        request.end();
    });
}

app.get('/product/:sku', (req, res) => {
    const sku = req.params.sku;
    const delay = process.env.GO_SLOW || 0;
    setTimeout(() => {
        serviceRequest(productInfoHost, '/info/' + sku)
            .then((info) => {
                return serviceRequest(productPriceHost, '/price/' + sku)
                    .then((price) => {
                        return serviceRequest(productStockHost, '/stock/' + sku)
                            .then((stock) => {
                                const product = {
                                    sku: info.sku,
                                    name: info.name,
                                    description: info.description,
                                    categories: info.categories,
                                    price: price.price,
                                    instock: stock.instock
                                };
                                req.log.info('product', product);
                                res.json(product);
                            });
                    });
            }).catch((e) => {
                req.log.error('ERROR', e);
                res.status(500).send(e);
            });
    }, delay);
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

