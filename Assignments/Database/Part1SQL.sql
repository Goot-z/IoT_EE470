-- EE 470 - Intro to Internet of Things
-- Database Assignment - Part 1
-- Student: Steve Gutierrez
-- Run setup script in empty database
-- Do not rerun against populated Hostinger database.

-- SECTION 1.1B: Create the Book table.
CREATE TABLE Book (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(50) NOT NULL
);

INSERT INTO Book (name) VALUES
('MariaDB Book'),
('SQLDB Book');

SELECT * FROM Book;

-- SECTION 1.2: Create the Price table, linked by book ID.
CREATE TABLE Price (
    id INT PRIMARY KEY,
    price DECIMAL(6,2) NOT NULL,
    FOREIGN KEY (id) REFERENCES Book(id)
);

INSERT INTO Price (id, price) VALUES
(1, 200.00),
(2, 150.00);

SELECT * FROM Price;

-- Combine book titles and their prices.
SELECT Book.name, Price.price
FROM Book
INNER JOIN Price ON Book.id = Price.id;

-- SECTION 1.3: Register five sensor nodes.
CREATE TABLE sensor_register (
    node_name VARCHAR(10) PRIMARY KEY,
    manufacturer VARCHAR(10) NOT NULL,
    longitude DECIMAL(15,10),
    latitude DECIMAL(15,10)
);

INSERT INTO sensor_register
(node_name, manufacturer, longitude, latitude) VALUES
('node_1', 'Bosch', -122.6765, 45.5231),
('node_2', 'Siemens', -118.2437, 34.0522),
('node_3', 'Honeywell', -74.0060, 40.7128),
('node_4', 'Sensirion', -87.6298, 41.8781),
('node_5', 'DHT', -122.4194, 37.7749);

-- Measurements use a composite primary key so each node can
-- report repeatedly, but cannot duplicate its own timestamp.
-- The foreign key rejects readings from unregistered nodes.
-- CHECK constraints reject out-of-range measurements.
CREATE TABLE sensor_data (
    node_name VARCHAR(10) NOT NULL,
    time_received DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    temperature DECIMAL(6,2) NOT NULL,
    humidity DECIMAL(6,2) NOT NULL,
    PRIMARY KEY (node_name, time_received),
    FOREIGN KEY (node_name) REFERENCES sensor_register(node_name),
    CHECK (temperature BETWEEN -10 AND 100),
    CHECK (humidity BETWEEN 0 AND 100)
);

-- Four readings per sensor, spaced 30 minutes apart.
INSERT INTO sensor_data
(node_name, time_received, temperature, humidity) VALUES
('node_1', '2022-10-01 11:00:00', 22.5, 45.0),
('node_1', '2022-10-01 11:30:00', 23.1, 46.0),
('node_1', '2022-10-01 12:00:00', 24.0, 47.5),
('node_1', '2022-10-01 12:30:00', 23.8, 48.0),
('node_2', '2022-10-01 11:00:00', 28.0, 35.0),
('node_2', '2022-10-01 11:30:00', 28.5, 36.0),
('node_2', '2022-10-01 12:00:00', 29.1, 37.0),
('node_2', '2022-10-01 12:30:00', 29.5, 38.0),
('node_3', '2022-10-01 11:00:00', 18.5, 55.0),
('node_3', '2022-10-01 11:30:00', 19.0, 54.0),
('node_3', '2022-10-01 12:00:00', 19.8, 53.0),
('node_3', '2022-10-01 12:30:00', 20.2, 52.0),
('node_4', '2022-10-01 11:00:00', 16.0, 60.0),
('node_4', '2022-10-01 11:30:00', 16.5, 61.0),
('node_4', '2022-10-01 12:00:00', 17.2, 62.0),
('node_4', '2022-10-01 12:30:00', 17.8, 63.0),
('node_5', '2022-10-01 11:00:00', 21.0, 50.0),
('node_5', '2022-10-01 11:30:00', 21.5, 51.0),
('node_5', '2022-10-01 12:00:00', 22.2, 52.0),
('node_5', '2022-10-01 12:30:00', 22.8, 53.0);

-- Verify all readings, ordered by sensor and timestamp.
SELECT * FROM sensor_data ORDER BY node_name, time_received;
SELECT node_name, COUNT(*) AS measurement_count
FROM sensor_data GROUP BY node_name;

-- Combine registration metadata and readings in a virtual table.
CREATE VIEW sensor_combined AS
SELECT
    sensor_data.node_name,
    sensor_register.manufacturer,
    sensor_register.longitude,
    sensor_register.latitude,
    sensor_data.time_received,
    sensor_data.temperature,
    sensor_data.humidity
FROM sensor_data
INNER JOIN sensor_register
    ON sensor_data.node_name = sensor_register.node_name;

SELECT * FROM sensor_combined ORDER BY node_name, time_received;

-- NEGATIVE TESTS (run individually only when demonstrating constraints):
-- These statements are intentionally commented out because each
-- should produce an error when the constraints are working.

-- Unregistered node: expected foreign key error #1452.
-- INSERT INTO sensor_data (node_name, time_received, temperature, humidity)
-- VALUES ('node_6', '2022-10-01 13:00:00', 25.0, 50.0);

-- Temperature outside allowed range: expected CHECK error.
-- INSERT INTO sensor_data (node_name, time_received, temperature, humidity)
-- VALUES ('node_1', '2022-10-01 13:00:00', 150.0, 50.0);

-- Humidity outside allowed range: expected CHECK error.
-- INSERT INTO sensor_data (node_name, time_received, temperature, humidity)
-- VALUES ('node_1', '2022-10-01 13:00:00', 25.0, 120.0);

-- Confirm unregistered node has no saved measurements.
SELECT * FROM sensor_data WHERE node_name = 'node_6';

