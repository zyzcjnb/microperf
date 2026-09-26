package com.shop.robotshop.shipping;

import java.util.List;

import org.springframework.data.domain.Pageable;
import org.springframework.data.repository.PagingAndSortingRepository;
import org.springframework.data.jpa.repository.Query;

public interface CityRepository extends PagingAndSortingRepository<City, Long> {
    List<City> findByCode(String code, Pageable pageable);

    @Query(
        value = "select c from City c where c.code = ?1 and c.city like ?2%"
    )
    List<City> match(String code, String text);

    City findById(long id);
}
