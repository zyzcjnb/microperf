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
package org.springframework.samples.petclinic.api.boundary.web;

import org.springframework.cloud.client.circuitbreaker.ReactiveCircuitBreaker;
import org.springframework.cloud.client.circuitbreaker.ReactiveCircuitBreakerFactory;
import org.springframework.samples.petclinic.api.application.CustomersServiceClient;
import org.springframework.samples.petclinic.api.application.VisitsServiceClient;
import org.springframework.http.HttpStatus;
import org.springframework.samples.petclinic.api.dto.OwnerDetails;
import org.springframework.samples.petclinic.api.dto.OwnerInfo;
import org.springframework.samples.petclinic.api.dto.PetDetails;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.ResponseStatus;
import org.springframework.web.bind.annotation.RestController;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Mono;

import java.util.List;
import java.util.Map;

/**
 * Gateway aggregation endpoints for owner data.
 *
 */
@RestController
@RequestMapping("/api")
public class ApiGatewayController {

    private final CustomersServiceClient customersServiceClient;

    private final VisitsServiceClient visitsServiceClient;

    private final ReactiveCircuitBreakerFactory cbFactory;

    public ApiGatewayController(CustomersServiceClient customersServiceClient,
                                VisitsServiceClient visitsServiceClient,
                                ReactiveCircuitBreakerFactory cbFactory) {
        this.customersServiceClient = customersServiceClient;
        this.visitsServiceClient = visitsServiceClient;
        this.cbFactory = cbFactory;
    }

    /**
     * Owner details: owner info, then pets, then visits per pet.
     */
    @GetMapping(value = "gateway/owners/{ownerId}")
    public Mono<OwnerDetails> getOwnerDetails(final @PathVariable int ownerId) {
        return customersServiceClient.getOwnerInfo(ownerId)
            .flatMap(owner -> customersServiceClient.getPets(ownerId)
                .flatMap(pets -> Flux.fromIterable(pets)
                    .concatMap(pet -> visitsServiceClient.getVisitsForPet(pet.id())
                        .transform(it -> {
                            ReactiveCircuitBreaker cb = cbFactory.create("getOwnerDetails");
                            return cb.run(it, throwable -> Mono.just(List.of()));
                        })
                        .map(visits -> {
                            pet.visits().addAll(visits);
                            return pet;
                        }))
                    .collectList()
                    .map(petList -> toOwnerDetails(owner, petList))));
    }

    /**
     * Owner list: all owners with their pets and visits.
     */
    @GetMapping(value = "customer/owners")
    public Mono<List<OwnerDetails>> getOwners() {
        return customersServiceClient.getOwners()
            .concatMap(owner -> customersServiceClient.getPets(owner.id())
                .flatMap(pets -> Flux.fromIterable(pets)
                    .concatMap(pet -> visitsServiceClient.getVisitsForPet(pet.id())
                        .transform(it -> {
                            ReactiveCircuitBreaker cb = cbFactory.create("getOwners");
                            return cb.run(it, throwable -> Mono.just(List.of()));
                        })
                        .map(visits -> {
                            pet.visits().addAll(visits);
                            return pet;
                        }))
                    .collectList()
                    .map(petList -> toOwnerDetails(owner, petList))))
            .collectList();
    }

    /**
     * Pass-through for POST /api/customer/owners so the test can create owners.
     */
    @PostMapping(value = "customer/owners")
    @ResponseStatus(HttpStatus.CREATED)
    public Mono<OwnerDetails> createOwner(@RequestBody Map<String, Object> ownerRequest) {
        return customersServiceClient.createOwner(ownerRequest);
    }

    private OwnerDetails toOwnerDetails(OwnerInfo owner, List<PetDetails> pets) {
        return new OwnerDetails(
            owner.id(),
            owner.firstName(),
            owner.lastName(),
            owner.address(),
            owner.city(),
            owner.telephone(),
            pets);
    }
}
