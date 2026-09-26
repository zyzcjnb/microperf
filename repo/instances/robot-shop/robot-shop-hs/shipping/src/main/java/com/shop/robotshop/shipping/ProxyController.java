package com.shop.robotshop.shipping;

import java.io.IOException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.apache.http.client.config.RequestConfig;
import org.apache.http.client.methods.CloseableHttpResponse;
import org.apache.http.client.methods.HttpGet;
import org.apache.http.impl.client.CloseableHttpClient;
import org.apache.http.impl.client.HttpClients;
import org.apache.http.impl.conn.PoolingHttpClientConnectionManager;
import org.apache.http.util.EntityUtils;

import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RestController;

/**
 * Proxy controller: forwards catalogue, ratings, cart and user read traffic
 * through the shipping service.
 */
@RestController
public class ProxyController {
    private static final Logger logger = LoggerFactory.getLogger(ProxyController.class);

    private final String catalogueUrl;
    private final String ratingsUrl;
    private final String cartUrl;
    private final String userUrl;
    private final CloseableHttpClient httpClient;

    public ProxyController() {
        String catalogueHost = System.getenv("CATALOGUE_HOST");
        catalogueHost = catalogueHost == null ? "catalogue" : catalogueHost;
        String ratingsHost = System.getenv("RATINGS_HOST");
        ratingsHost = ratingsHost == null ? "ratings" : ratingsHost;
        String cartHost = System.getenv("CART_HOST");
        cartHost = cartHost == null ? "cart" : cartHost;
        String userHost = System.getenv("USER_HOST");
        userHost = userHost == null ? "user" : userHost;
        this.catalogueUrl = "http://" + catalogueHost + ":8080";
        this.ratingsUrl = "http://" + ratingsHost + ":80";
        this.cartUrl = "http://" + cartHost + ":8080";
        this.userUrl = "http://" + userHost + ":8080";

        PoolingHttpClientConnectionManager cm = new PoolingHttpClientConnectionManager();
        cm.setMaxTotal(1);
        cm.setDefaultMaxPerRoute(1);
        RequestConfig requestConfig = RequestConfig.custom()
                .setConnectTimeout(2000)
                .setConnectionRequestTimeout(30000)
                .setSocketTimeout(30000)
                .build();
        this.httpClient = HttpClients.custom()
                .setConnectionManager(cm)
                .setDefaultRequestConfig(requestConfig)
                .build();
    }

    @GetMapping("/catalogue/categories")
    public ResponseEntity<byte[]> categories() {
        return proxy(catalogueUrl + "/categories");
    }

    @GetMapping("/catalogue/products")
    public ResponseEntity<byte[]> products() {
        return proxy(catalogueUrl + "/products");
    }

    @GetMapping("/catalogue/product/{sku}")
    public ResponseEntity<byte[]> product(@PathVariable String sku) {
        return proxy(catalogueUrl + "/product/" + sku);
    }

    @GetMapping("/ratings/fetch/{sku}")
    public ResponseEntity<byte[]> fetch(@PathVariable String sku) {
        return proxy(ratingsUrl + "/api/fetch/" + sku);
    }

    @GetMapping("/cart/add/{user}/{sku}/{qty}")
    public ResponseEntity<byte[]> cartAdd(
            @PathVariable String user,
            @PathVariable String sku,
            @PathVariable int qty) {
        return proxy(cartUrl + "/add/" + user + "/" + sku + "/" + qty);
    }

    @GetMapping("/cart/update/{user}/{sku}/{qty}")
    public ResponseEntity<byte[]> cartUpdate(
            @PathVariable String user,
            @PathVariable String sku,
            @PathVariable int qty) {
        return proxy(cartUrl + "/update/" + user + "/" + sku + "/" + qty);
    }

    @GetMapping("/cart/cart/{user}")
    public ResponseEntity<byte[]> cartGet(@PathVariable String user) {
        return proxy(cartUrl + "/cart/" + user);
    }

    @GetMapping("/user/uniqueid")
    public ResponseEntity<byte[]> uniqueid() {
        return proxy(userUrl + "/uniqueid");
    }

    private ResponseEntity<byte[]> proxy(String url) {
        HttpGet get = new HttpGet(url);
        try (CloseableHttpResponse res = httpClient.execute(get)) {
            int status = res.getStatusLine().getStatusCode();
            byte[] body = EntityUtils.toByteArray(res.getEntity());
            HttpHeaders headers = new HttpHeaders();
            headers.setContentType(MediaType.APPLICATION_JSON);
            return ResponseEntity.status(status).headers(headers).body(body);
        } catch (IOException e) {
            logger.warn("proxy error for {}: {}", url, e.getMessage());
            return ResponseEntity.status(502).body("bad gateway".getBytes());
        }
    }
}
