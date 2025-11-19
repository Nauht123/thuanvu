CREATE database watering_system;
use watering_system;

CREATE TABLE plant (
    plantID INT AUTO_INCREMENT PRIMARY KEY,
    Name VARCHAR(100) NOT NULL,
    info TEXT,
    lowerThreshold FLOAT(5,2),
    upperThreshold FLOAT(5,2)
);

CREATE TABLE history (
    timeID INT AUTO_INCREMENT PRIMARY KEY,
    plantID INT NOT NULL,
    soilMoisture FLOAT(5,2) NOT NULL,
    temperature FLOAT(5,2) NOT NULL,
    recorded DATETIME,
    FOREIGN KEY (plantID) REFERENCES plant(plantID)
);
ALTER TABLE history ADD airMoisture FLOAT(5,2) NOT NULL;

SELECT * FROM history;
SELECT * FROM plant;

INSERT INTO history (plantID, soilMoisture, temperature, airMoisture, recorded) VALUES
(1, 35.50, 22.30, 60.50, '2025-05-07 00:00:00'),
(1, 45.20, 23.10, 62.30, '2025-05-07 01:00:00'),
(1, 60.10, 24.50, 65.10, '2025-05-07 02:00:00'),
(1, 28.90, 21.80, 58.90, '2025-05-07 03:00:00'),
(2, 50.30, 25.60, 70.20, '2025-05-07 04:00:00'),
(2, 65.40, 26.20, 72.40, '2025-05-07 05:00:00'),
(2, 40.70, 24.90, 68.70, '2025-05-07 06:00:00'),
(3, 55.60, 23.70, 75.60, '2025-05-07 07:00:00'),
(3, 30.20, 22.90, 60.20, '2025-05-07 08:00:00'),
(3, 70.80, 27.10, 78.80, '2025-05-07 09:00:00');

INSERT INTO history (plantID, soilMoisture, temperature, airMoisture, recorded) VALUES
(2, 40.80, 30.00, 70.70, '2025-05-08 17:00:00');
Select * from history where plantID = 2;

INSERT INTO history (plantID, soilMoisture, temperature, airMoisture, recorded) VALUES
(2, 40.80, 40.00, 80.70, '2025-05-08 18:00:00');

INSERT INTO history (plantID, soilMoisture, temperature, airMoisture, recorded) VALUES
(2, 50.80, 30.00, 70.70, '2025-05-08 19:00:00');

INSERT INTO history (plantID, soilMoisture, temperature, airMoisture, recorded) VALUES
(1, 50.80, 30.00, 70.70, '2025-05-08 18:00:00');


INSERT INTO history (plantID, soilMoisture, temperature, airMoisture, recorded) VALUES
(4, 35.50, 22.30, 60.50, '2025-05-15 00:00:00'),
(4, 45.20, 23.10, 62.30, '2025-05-15 01:00:00'),
(4, 60.10, 24.50, 65.10, '2025-05-15 02:00:00'),
(4, 28.90, 21.80, 58.90, '2025-05-15 03:00:00'),
(4, 50.30, 25.60, 70.20, '2025-05-15 04:00:00'),
(4, 65.40, 26.20, 72.40, '2025-05-15 05:00:00'),
(4, 40.70, 24.90, 68.70, '2025-05-15 06:00:00'),
(4, 55.60, 23.70, 75.60, '2025-05-15 07:00:00'),
(4, 30.20, 22.90, 60.20, '2025-05-15 08:00:00'),
(4, 70.80, 27.10, 78.80, '2025-05-15 09:00:00');