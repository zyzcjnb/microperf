USE cities;
DROP PROCEDURE IF EXISTS AddCitiesIndex;
DELIMITER $$
CREATE PROCEDURE AddCitiesIndex()
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM information_schema.statistics
        WHERE table_schema = 'cities'
          AND table_name = 'cities'
          AND index_name = 'idx_code_name'
    ) THEN
        ALTER TABLE cities ADD INDEX idx_code_name (country_code, name);
    END IF;
END$$
DELIMITER ;
CALL AddCitiesIndex();
DROP PROCEDURE IF EXISTS AddCitiesIndex;
