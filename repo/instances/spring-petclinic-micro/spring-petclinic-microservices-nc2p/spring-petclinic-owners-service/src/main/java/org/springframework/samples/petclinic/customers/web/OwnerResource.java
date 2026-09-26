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
package org.springframework.samples.petclinic.customers.web;

import io.micrometer.core.annotation.Timed;
import jakarta.validation.Valid;
import jakarta.validation.constraints.Min;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.cloud.client.loadbalancer.LoadBalancerInterceptor;
import org.springframework.http.HttpStatus;
import org.springframework.samples.petclinic.customers.web.mapper.OwnerEntityMapper;
import org.springframework.samples.petclinic.customers.model.Owner;
import org.springframework.samples.petclinic.customers.model.OwnerRepository;
import org.springframework.samples.petclinic.customers.model.Pet;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.client.RestClient;
import org.springframework.web.client.RestClientResponseException;

import java.util.List;
import java.util.Optional;

/**
 */
@RequestMapping("/owners")
@RestController
@Timed("petclinic.owner")
class OwnerResource {

    private static final Logger log = LoggerFactory.getLogger(OwnerResource.class);

    private final OwnerRepository ownerRepository;
    private final OwnerEntityMapper ownerEntityMapper;
    private final RestClient petsClient;

    OwnerResource(OwnerRepository ownerRepository,
                  OwnerEntityMapper ownerEntityMapper,
                  LoadBalancerInterceptor loadBalancerInterceptor) {
        this.ownerRepository = ownerRepository;
        this.ownerEntityMapper = ownerEntityMapper;
        this.petsClient = RestClient.builder()
            .baseUrl("http://pets-service")
            .requestInterceptor(loadBalancerInterceptor)
            .build();
    }

    /**
     * Create Owner
     */
    @PostMapping
    @ResponseStatus(HttpStatus.CREATED)
    public Owner createOwner(@Valid @RequestBody OwnerRequest ownerRequest) {
        Owner owner = ownerEntityMapper.map(new Owner(), ownerRequest);
        return ownerRepository.save(owner);
    }

    /**
     * Read single Owner
     */
    @GetMapping(value = "/{ownerId}")
    public Optional<Owner> findOwner(@PathVariable("ownerId") @Min(1) int ownerId) {
        Optional<Owner> owner = ownerRepository.findById(ownerId);
        owner.ifPresent(this::attachPets);
        return owner;
    }

    /**
     * Read List of Owners
     */
    @GetMapping
    public List<Owner> findAll() {
        List<Owner> owners = ownerRepository.findAll();
        owners.forEach(this::attachPets);
        return owners;
    }

    /**
     * Update Owner
     */
    @PutMapping(value = "/{ownerId}")
    @ResponseStatus(HttpStatus.NO_CONTENT)
    public void updateOwner(@PathVariable("ownerId") @Min(1) int ownerId, @Valid @RequestBody OwnerRequest ownerRequest) {
        final Owner ownerModel = ownerRepository.findById(ownerId).orElseThrow(() -> new ResourceNotFoundException("Owner " + ownerId + " not found"));

        ownerEntityMapper.map(ownerModel, ownerRequest);
        log.info("Saving owner {}", ownerModel);
        ownerRepository.save(ownerModel);
    }

    /**
     * Fetch the owner's pets from pets-service and stash them on the entity
     * so the JSON response matches the original monolithic customers-service
     * shape (Owner { ... pets: [ Pet { id, name, birthDate, type } ] }).
     */
    private void attachPets(Owner owner) {
        if (owner == null || owner.getId() == null) {
            return;
        }
        try {
            Pet[] pets = petsClient.get()
                .uri("/owners/{ownerId}/pets", owner.getId())
                .retrieve()
                .body(Pet[].class);
            owner.setPets(pets == null ? List.of() : List.of(pets));
        }
        catch (RestClientResponseException ex) {
            log.warn("Failed to fetch pets for owner {}: HTTP {}", owner.getId(), ex.getStatusCode());
            owner.setPets(List.of());
        }
        catch (Exception ex) {
            log.warn("Unexpected error fetching pets for owner {}: {}", owner.getId(), ex.toString());
            owner.setPets(List.of());
        }
    }
}