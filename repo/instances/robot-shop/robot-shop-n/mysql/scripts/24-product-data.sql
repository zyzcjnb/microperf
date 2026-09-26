CREATE DATABASE IF NOT EXISTS products;
USE products;

CREATE TABLE IF NOT EXISTS product_info (
    sku VARCHAR(255) PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    description TEXT,
    categories JSON
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS product_price (
    sku VARCHAR(255) PRIMARY KEY,
    price DECIMAL(10, 2) NOT NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS product_stock (
    sku VARCHAR(255) PRIMARY KEY,
    instock INT NOT NULL
) ENGINE=InnoDB;

INSERT INTO product_info (sku, name, description, categories) VALUES
('Watson', 'Watson', 'Probably the smartest AI on the planet', '["Artificial Intelligence"]'),
('Ewooid', 'Ewooid', 'Fully sentient assistant', '["Artificial Intelligence"]'),
('HPTD', 'High-Powered Travel Droid', 'Traveling to the far reaches of the Galaxy? You need this for protection. Comes in handy when you are lost in space', '["Robot"]'),
('UHJ', 'Ultimate Harvesting Juggernaut', 'Extraterrestrial vegetation harvester', '["Robot"]'),
('EPE', 'Extreme Probe Emulator', 'Versatile interface adapter for hacking into systems', '["Robot"]'),
('EMM', 'Exceptional Medical Machine', 'Fully automatic surgery droid with exceptional bedside manner', '["Robot"]'),
('SHCE', 'Strategic Human Control Emulator', 'Diplomatic protocol assistant', '["Robot"]'),
('RED', 'Responsive Enforcer Droid', 'Security detail, will gaurd anything', '["Robot"]'),
('RMC', 'Robotic Mining Cyborg', 'Excellent tunneling capability to get those rare minerals', '["Robot"]'),
('STAN-1', 'Stan', 'Observability guru', '["Robot", "Artificial Intelligence"]'),
('CNA', 'Cybernated Neutralization Android', 'Is your spaceship a bit whiffy? This little fellow will bring a breath of fresh air', '["Robot"]')
ON DUPLICATE KEY UPDATE name = VALUES(name), description = VALUES(description), categories = VALUES(categories);

INSERT INTO product_price (sku, price) VALUES
('Watson', 2001), ('Ewooid', 200), ('HPTD', 1200), ('UHJ', 5000), ('EPE', 953),
('EMM', 1024), ('SHCE', 300), ('RED', 700), ('RMC', 42), ('STAN-1', 67), ('CNA', 1000)
ON DUPLICATE KEY UPDATE price = VALUES(price);

INSERT INTO product_stock (sku, instock) VALUES
('Watson', 2), ('Ewooid', 0), ('HPTD', 12), ('UHJ', 10), ('EPE', 1),
('EMM', 1), ('SHCE', 12), ('RED', 5), ('RMC', 48), ('STAN-1', 1000), ('CNA', 0)
ON DUPLICATE KEY UPDATE instock = VALUES(instock);

GRANT ALL ON products.* TO 'shipping'@'%';
FLUSH PRIVILEGES;
