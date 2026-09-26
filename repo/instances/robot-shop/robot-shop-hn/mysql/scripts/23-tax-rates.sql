CREATE DATABASE IF NOT EXISTS tax;
USE tax;

CREATE TABLE IF NOT EXISTS tax_rates (
    id INT PRIMARY KEY,
    rate DECIMAL(5,4) NOT NULL
) ENGINE=InnoDB;

-- 20% VAT inclusive (same rate the cart used inline)
INSERT INTO tax_rates (id, rate) VALUES (1, 0.2)
ON DUPLICATE KEY UPDATE rate = VALUES(rate);

GRANT ALL ON tax.* TO 'shipping'@'%';
FLUSH PRIVILEGES;
