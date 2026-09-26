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
import jakarta.validation.constraints.Min;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.cloud.client.loadbalancer.LoadBalancerInterceptor;
import org.springframework.http.HttpStatus;
import org.springframework.samples.petclinic.customers.model.*;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.client.RestClient;
import org.springframework.web.client.RestClientResponseException;

import java.util.List;
import java.util.Optional;

/**
 *
 * Pet writes validate the owner against owners-service and resolve the pet
 * type against pettypes-service before the pet is persisted locally.
 */
@RestController
@Timed("petclinic.pet")
class PetResource {

    private static final Logger log = LoggerFactory.getLogger(PetResource.class);

    private final PetRepository petRepository;

    private final RestClient ownersClient;

    private final RestClient petTypesClient;

    PetResource(PetRepository petRepository, LoadBalancerInterceptor loadBalancerInterceptor) {
        this.petRepository = petRepository;
        this.ownersClient = RestClient.builder()
            .baseUrl("http://owners-service")
            .requestInterceptor(loadBalancerInterceptor)
            .build();
        this.petTypesClient = RestClient.builder()
            .baseUrl("http://pettypes-service")
            .requestInterceptor(loadBalancerInterceptor)
            .build();
    }

    @GetMapping("/petTypes")
    public List<PetType> getPetTypes() {
        return petRepository.findPetTypes();
    }

    @PostMapping("/owners/{ownerId}/pets")
    @ResponseStatus(HttpStatus.CREATED)
    public Pet processCreationForm(
        @RequestBody PetRequest petRequest,
        @PathVariable("ownerId") @Min(1) int ownerId) {

        // Validate the owner against owners-service
        validateOwner(ownerId);

        final Pet pet = new Pet();
        pet.setOwnerId(ownerId);
        return save(pet, petRequest);
    }

    @PutMapping("/owners/{ownerId}/pets/{petId}")
    @ResponseStatus(HttpStatus.NO_CONTENT)
    public void processUpdateForm(
        @RequestBody PetRequest petRequest,
        @PathVariable("ownerId") int ownerId) {
        int petId = petRequest.id();
        Pet pet = findPetById(petId);
        pet.setOwnerId(ownerId);
        save(pet, petRequest);
    }

    private Pet save(final Pet pet, final PetRequest petRequest) {

        pet.setName(petRequest.name());
        pet.setBirthDate(petRequest.birthDate());

        // Resolve the pet type against pettypes-service
        resolvePetType(petRequest.typeId())
            .ifPresent(pet::setType);

        log.info("Saving pet {}", pet);
        return petRepository.save(pet);
    }

    private void validateOwner(int ownerId) {
        OwnerInfo owner;
        try {
            owner = ownersClient.get()
                .uri("/owners/{ownerId}", ownerId)
                .retrieve()
                .body(OwnerInfo.class);
        }
        catch (RestClientResponseException ex) {
            if (ex.getStatusCode() == HttpStatus.NOT_FOUND) {
                throw new ResourceNotFoundException("Owner " + ownerId + " not found");
            }
            throw ex;
        }
        if (owner == null) {
            throw new ResourceNotFoundException("Owner " + ownerId + " not found");
        }
    }

    private record OwnerInfo(Integer id) {
    }

    private Optional<PetType> resolvePetType(int typeId) {
        try {
            petTypesClient.get()
                .uri("/petTypes/{typeId}", typeId)
                .retrieve()
                .toBodilessEntity();
        }
        catch (RestClientResponseException ex) {
            if (ex.getStatusCode() == HttpStatus.NOT_FOUND) {
                return Optional.empty();
            }
            throw ex;
        }
        return petRepository.findPetTypeById(typeId);
    }

    @GetMapping("owners/*/pets/{petId}")
    public PetDetails findPet(@PathVariable("petId") int petId) {
        Pet pet = findPetById(petId);
        return new PetDetails(pet);
    }

    @GetMapping("owners/{ownerId}/pets")
    public List<PetDetails> findPetsByOwner(@PathVariable("ownerId") int ownerId) {
        return petRepository.findByOwnerId(ownerId).stream()
            .map(PetDetails::new)
            .toList();
    }

    @GetMapping("/pets")
    public List<PetDetails> findAllPets() {
        return petRepository.findAll().stream()
            .map(PetDetails::new)
            .toList();
    }

    private Pet findPetById(int petId) {
        return petRepository.findById(petId)
            .orElseThrow(() -> new ResourceNotFoundException("Pet " + petId + " not found"));
    }

}
