/*
 * Copyright 2002-2021 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.springframework.samples.petclinic.api.application;

import org.springframework.samples.petclinic.api.dto.OwnerDetails;
import org.springframework.samples.petclinic.api.dto.OwnerInfo;
import org.springframework.samples.petclinic.api.dto.PetDetails;
import org.springframework.stereotype.Component;
import org.springframework.web.reactive.function.client.WebClient;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Mono;

import java.util.List;
import java.util.Map;

/**
 */
@Component
public class CustomersServiceClient {

    private final WebClient.Builder webClientBuilder;

    public CustomersServiceClient(WebClient.Builder webClientBuilder) {
        this.webClientBuilder = webClientBuilder;
    }

    public Mono<OwnerDetails> getOwner(final int ownerId) {
        return webClientBuilder.build().get()
            .uri("http://customers-service/owners/{ownerId}", ownerId)
            .retrieve()
            .bodyToMono(OwnerDetails.class);
    }

    /**
     * Fetch owner info without pets.
     */
    public Mono<OwnerInfo> getOwnerInfo(final int ownerId) {
        return webClientBuilder.build().get()
            .uri("http://customers-service/owners/{ownerId}", ownerId)
            .retrieve()
            .bodyToMono(OwnerInfo.class);
    }

    /**
     * Fetch pets for an owner in a separate call.
     */
    public Mono<List<PetDetails>> getPets(final int ownerId) {
        return webClientBuilder.build().get()
            .uri("http://customers-service/owners/{ownerId}/pets", ownerId)
            .retrieve()
            .bodyToFlux(PetDetails.class)
            .collectList();
    }

    /**
     * Fetch all owners.
     */
    public Flux<OwnerInfo> getOwners() {
        return webClientBuilder.build().get()
            .uri("http://customers-service/owners")
            .retrieve()
            .bodyToFlux(OwnerInfo.class);
    }

    /**
     * Pass-through for creating owners.
     */
    public Mono<OwnerDetails> createOwner(final Map<String, Object> ownerRequest) {
        return webClientBuilder.build().post()
            .uri("http://customers-service/owners")
            .bodyValue(ownerRequest)
            .retrieve()
            .bodyToMono(OwnerDetails.class);
    }
}
