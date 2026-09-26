package com.shop.robotshop.shipping;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.IOException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.apache.http.client.methods.CloseableHttpResponse;
import org.apache.http.client.methods.HttpPost;
import org.apache.http.entity.StringEntity;
import org.apache.http.impl.client.HttpClients;
import org.apache.http.impl.client.CloseableHttpClient;

public class CartHelper {
    private static final Logger logger = LoggerFactory.getLogger(CartHelper.class);

    // shared client (default pool settings) so connections are reused
    // across requests instead of opening a new one per call
    private static final CloseableHttpClient httpClient = HttpClients.createDefault();

    private String baseUrl;

    public CartHelper(String baseUrl) {
        this.baseUrl = baseUrl;
    }

    public String addToCart(String id, String data) {
        logger.info("add shipping to cart {}", id);
        StringBuilder buffer = new StringBuilder();

        try {
            HttpPost postRequest = new HttpPost(baseUrl + id);
            StringEntity payload = new StringEntity(data);
            payload.setContentType("application/json");
            postRequest.setEntity(payload);
            CloseableHttpResponse res = httpClient.execute(postRequest);

            if (res.getStatusLine().getStatusCode() == 200) {
                BufferedReader in = new BufferedReader(new InputStreamReader(res.getEntity().getContent()));
                String line;
                while ((line = in.readLine()) != null) {
                    buffer.append(line);
                }
            } else {
                logger.warn("Failed with code {}", res.getStatusLine().getStatusCode());
            }
            try {
                res.close();
            } catch(IOException e) {
                logger.warn("httpresponse", e);
            }
        } catch(Exception e) {
            logger.warn("http client exception", e);
        }

        // this will be empty on error
        return buffer.toString();
    }
}
