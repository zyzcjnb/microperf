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
package org.springframework.samples.petclinic.customers.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

import java.util.Date;
import java.util.Objects;

/**
 * DTO mirroring the {@code Pet} serialization produced by the original
 * monolithic customers-service. Pets live in pets-service; owners-service
 * fetches and re-emits them so the public API contract is preserved.
 *
 * <p>The {@code ownerId} field that pets-service adds on top is ignored on
 * the wire by {@link JsonIgnoreProperties} so the rendered JSON is identical
 * to the original schema ({@code id, name, birthDate, type}).</p>
 */
@JsonIgnoreProperties(ignoreUnknown = true)
public class Pet {

    private Integer id;
    private String name;
    private Date birthDate;
    private PetType type;

    public Pet() {
    }

    public Pet(Integer id, String name, Date birthDate, PetType type) {
        this.id = id;
        this.name = name;
        this.birthDate = birthDate;
        this.type = type;
    }

    public Integer getId() {
        return id;
    }

    public void setId(Integer id) {
        this.id = id;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public Date getBirthDate() {
        return birthDate;
    }

    public void setBirthDate(Date birthDate) {
        this.birthDate = birthDate;
    }

    public PetType getType() {
        return type;
    }

    public void setType(PetType type) {
        this.type = type;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        Pet pet = (Pet) o;
        return Objects.equals(id, pet.id)
            && Objects.equals(name, pet.name)
            && Objects.equals(birthDate, pet.birthDate)
            && Objects.equals(type, pet.type);
    }

    @Override
    public int hashCode() {
        return Objects.hash(id, name, birthDate, type);
    }
}