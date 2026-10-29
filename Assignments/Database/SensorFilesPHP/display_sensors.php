
<?php
// EE 470 - Database Assignment Part 2
// Display sensor registration and measurement data

require_once __DIR__ . "/../private/db_connect.php";

// Retrieve registered sensors
$register_sql = "SELECT * FROM sensor_register
                 ORDER BY node_name";

$register_result = $conn->query($register_sql);

// Retrieve sensor measurements
$data_sql = "SELECT * FROM sensor_data
             ORDER BY node_name, time_received";

$data_result = $conn->query($data_sql);

if (!$register_result || !$data_result) {
    error_log($conn->error);
    http_response_code(500);
    exit("Unable to retrieve sensor data.");
}

// Calculate average measurements for node_1
$average_sql = "
    SELECT
        AVG(temperature) AS avg_temperature,
        AVG(humidity) AS avg_humidity
    FROM sensor_data
    WHERE node_name = 'node_1'
";

$average_result = $conn->query($average_sql);

if (!$average_result) {
    error_log($conn->error);
    http_response_code(500);
    exit("Unable to calculate averages.");
}

$average = $average_result->fetch_assoc();
?>





<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>EE 470 Sensor Database</title>
    
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.8/dist/chart.umd.min.js"></script>

    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 30px;
            background-color: #f4f6f8;
        }

        h1, h2 {
            color: #17365d;
        }

        table {
            border-collapse: collapse;
            width: 100%;
            margin-bottom: 30px;
            background-color: white;
        }

        th, td {
            border: 1px solid #ccc;
            padding: 10px;
            text-align: center;
        }

        th {
            background-color: #17365d;
            color: white;
        }

        tr:nth-child(even) {
            background-color: #eef2f7;
        }
    </style>
</head>

<body>

<h1>EE 470 - Sensor Database</h1>

<h2>Registered Sensors</h2>

<table>
    <tr>
        <th>Node Name</th>
        <th>Manufacturer</th>
        <th>Longitude</th>
        <th>Latitude</th>
    </tr>

    <?php while ($row = $register_result->fetch_assoc()): ?>
    <tr>
        <td><?= htmlspecialchars($row['node_name']) ?></td>
        <td><?= htmlspecialchars($row['manufacturer']) ?></td>
        <td><?= htmlspecialchars($row['longitude']) ?></td>
        <td><?= htmlspecialchars($row['latitude']) ?></td>
    </tr>
    <?php endwhile; ?>
</table>

<h2>Sensor Measurements</h2>

<table>
    <tr>
        <th>Node Name</th>
        <th>Time Received</th>
        <th>Temperature (C)</th>
        <th>Humidity (%)</th>
    </tr>

    <?php while ($row = $data_result->fetch_assoc()): ?>
    <tr>
        <td><?= htmlspecialchars($row['node_name']) ?></td>
        <td><?= htmlspecialchars($row['time_received']) ?></td>
        <td><?= htmlspecialchars($row['temperature']) ?></td>
        <td><?= htmlspecialchars($row['humidity']) ?></td>
    </tr>
    <?php endwhile; ?>
</table>


<h2>Average Measurements - node_1</h2>

<p>
    Average Temperature:
    <?= number_format($average['avg_temperature'], 2) ?> °C
</p>

<p>
    Average Humidity:
    <?= number_format($average['avg_humidity'], 2) ?> %
</p>


<h2>Sensor Node 1</h2>

<div style="max-width: 900px; margin: 30px auto;">
    <canvas id="temperatureChart"></canvas>
</div>

<script>
// Retrieve sensor measurements from PHP
fetch("sensor_json.php")
    .then(response => {
        if (!response.ok) {
            throw new Error("Unable to retrieve sensor data");
        }
        return response.json();
    })
    .then(data => {

        // Extract timestamps for the x-axis
        const timestamps = data.map(
            row => row.time_received
        );

        // Extract temperatures for the y-axis
        const temperatures = data.map(
            row => row.temperature
        );

        // Find the canvas element
        const ctx = document.getElementById(
            "temperatureChart"
        );

        // Create the chart
        new Chart(ctx, {
            type: "line",

            data: {
                labels: timestamps,
                datasets: [{
                    label: "Temperature (°C)",
                    data: temperatures,
                    backgroundColor: "rgba(0, 200, 0, 0.6)",
                    borderColor: "rgba(0, 200, 0, 1)",
                    borderWidth: 1
                }]
            },

            options: {
                responsive: true,

                plugins: {
                    title: {
                        display: true,
                        text: "Sensor Node 1"
                    }
                },

                scales: {
                    x: {
                        title: {
                            display: true,
                            text: "Time Received"
                        }
                    },

                    y: {
                        title: {
                            display: true,
                            text: "Temperature (°C)"
                        }
                    }
                }
            }
        });
    })
    .catch(error => {
        console.error("Chart error:", error);
    });
</script>


</body>
</html>

<?php
$conn->close();
?>
