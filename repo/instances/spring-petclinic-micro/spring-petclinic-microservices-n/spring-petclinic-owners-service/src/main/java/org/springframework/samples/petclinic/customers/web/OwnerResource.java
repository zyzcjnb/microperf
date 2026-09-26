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

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
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
        owner.ifPresent(o -> attachPets(List.of(o)));
        return owner;
    }

    /**
     * Read List of Owners
     */
    @GetMapping
    public List<Owner> findAll() {
        List<Owner> owners = ownerRepository.findAll();
        attachPets(owners);
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
     * Fetch pets for the given owners in a single batched call to
     * pets-service (one HTTP round trip regardless of N) and stash them on
     * each owner entity. This isolates the "service split adds one network
     * hop" cost from any chatty N+1 effect.
     */
    private void attachPets(List<Owner> owners) {
        if (owners == null || owners.isEmpty()) {
            return;
        }
        Pet[] pets;
        try {
            pets = petsClient.get()
                .uri("/pets")
                .retrieve()
                .body(Pet[].class);
        }
        catch (RestClientResponseException ex) {
            log.warn("Failed to fetch pets batch: HTTP {}", ex.getStatusCode());
            owners.forEach(o -> o.setPets(List.of()));
            return;
        }
        catch (Exception ex) {
            log.warn("Unexpected error fetching pets batch: {}", ex.toString());
            owners.forEach(o -> o.setPets(List.of()));
            return;
        }
        Map<Integer, List<Pet>> byOwner = new HashMap<>();
        if (pets != null) {
            for (Pet p : pets) {
                // PetDetails carries ownerId on the wire; we need to map it
                // back to an owner. Inject ownerId into a transient field.
                Integer oid = p.getOwnerIdWire();
                if (oid != null) {
                    byOwner.computeIfAbsent(oid, k -> new ArrayList<>()).add(p);
                }
            }
        }
        for (Owner o : owners) {
            o.setPets(byOwner.getOrDefault(o.getId(), List.of()));
        }
    }
}