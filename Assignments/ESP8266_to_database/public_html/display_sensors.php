<?php
// EE 470 - ESP8266 IoT Assignment
// Display registered nodes, measurements, statistics, and charts

require_once __DIR__ . "/../private/db_connect.php";

mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);

function h($value)
{
    return htmlspecialchars((string)$value, ENT_QUOTES, "UTF-8");
}

function metric($value, $unit = "")
{
    if ($value === null) {
        return "N/A";
    }

    return number_format((float)$value, 2) . $unit;
}

try {
    // Load registered nodes for the dropdown and registration table
    $registerResult = $conn->query(
        "SELECT node_name, manufacturer, longitude, latitude
         FROM sensor_register
         ORDER BY node_name"
    );

    $registeredSensors = [];

    while ($row = $registerResult->fetch_assoc()) {
        $registeredSensors[] = $row;
    }

    if (count($registeredSensors) === 0) {
        http_response_code(500);
        exit("No registered sensors were found.");
    }

    $registeredNodes = array_column($registeredSensors, "node_name");

    // Keep node_1 as the initial selection when it is registered
    $defaultNode = in_array("node_1", $registeredNodes, true)
        ? "node_1"
        : $registeredNodes[0];

    $requestedNode = $_GET["node_name"] ?? "";

    // Accept only nodes that appear in sensor_register
    $selectedNode = (
        is_string($requestedNode)
        && in_array($requestedNode, $registeredNodes, true)
    ) ? $requestedNode : $defaultNode;

    // Load all measurements for the history table
    $dataResult = $conn->query(
        "SELECT node_name, time_received, temperature, humidity, light_adc
         FROM sensor_data
         ORDER BY node_name, time_received"
    );

    $measurements = [];

    while ($row = $dataResult->fetch_assoc()) {
        $measurements[] = $row;
    }

    // Get the selected node's activity count and statistics
    $statsSql = "
        SELECT
            COALESCE(sa.message_count, 0) AS message_count,
            AVG(sd.temperature) AS avg_temperature,
            MIN(sd.temperature) AS min_temperature,
            MAX(sd.temperature) AS max_temperature,
            AVG(sd.humidity) AS avg_humidity,
            MIN(sd.humidity) AS min_humidity,
            MAX(sd.humidity) AS max_humidity,
            AVG(sd.light_adc) AS avg_light_adc,
            MIN(sd.light_adc) AS min_light_adc,
            MAX(sd.light_adc) AS max_light_adc
        FROM sensor_register AS sr
        LEFT JOIN sensor_activity AS sa
            ON sa.node_name = sr.node_name
        LEFT JOIN sensor_data AS sd
            ON sd.node_name = sr.node_name
        WHERE sr.node_name = ?
        GROUP BY sr.node_name, sa.message_count
    ";

    $statsStmt = $conn->prepare($statsSql);
    $statsStmt->bind_param("s", $selectedNode);
    $statsStmt->execute();
    $statsStmt->bind_result(
        $messageCount,
        $avgTemperature,
        $minTemperature,
        $maxTemperature,
        $avgHumidity,
        $minHumidity,
        $maxHumidity,
        $avgLightAdc,
        $minLightAdc,
        $maxLightAdc
    );
    $statsStmt->fetch();
    $statsStmt->close();

    // Load activity counts for every registered node
    $countResult = $conn->query(
        "SELECT
             sr.node_name,
             COALESCE(sa.message_count, 0) AS message_count
         FROM sensor_register AS sr
         LEFT JOIN sensor_activity AS sa
             ON sa.node_name = sr.node_name
         ORDER BY sr.node_name"
    );

    $nodeCounts = [];

    while ($row = $countResult->fetch_assoc()) {
        $nodeCounts[] = $row;
    }

    $conn->close();

} catch (mysqli_sql_exception $e) {
    error_log($e->getMessage());
    http_response_code(500);
    exit("Unable to retrieve the sensor dashboard.");
}
?>

<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
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

        .node-picker {
            margin: 16px 0 24px;
        }

        .node-picker label {
            font-weight: bold;
            margin-right: 8px;
        }

        .node-picker select {
            padding: 8px;
            font-size: 1rem;
        }

        .chart-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
            margin: 20px 0 36px;
        }

        .chart-card {
            background: white;
            border: 1px solid #ccc;
            border-radius: 6px;
            padding: 16px;
            min-height: 330px;
        }

        .chart-card canvas {
            width: 100% !important;
            height: 290px !important;
        }
    </style>
</head>

<body>

<h1>EE 470 - Sensor Database</h1>

<h2>Registered Sensors</h2>

<table>
    <thead>
        <tr>
            <th>Node Name</th>
            <th>Manufacturer</th>
            <th>Longitude</th>
            <th>Latitude</th>
        </tr>
    </thead>
    <tbody>
        <?php foreach ($registeredSensors as $row): ?>
        <tr>
            <td><?= h($row["node_name"]) ?></td>
            <td><?= h($row["manufacturer"]) ?></td>
            <td><?= h($row["longitude"]) ?></td>
            <td><?= h($row["latitude"]) ?></td>
        </tr>
        <?php endforeach; ?>
    </tbody>
</table>

<h2>Unique Messages by Node</h2>

<table>
    <thead>
        <tr>
            <th>Node Name</th>
            <th>Accepted Unique Messages</th>
        </tr>
    </thead>
    <tbody>
        <?php foreach ($nodeCounts as $row): ?>
        <tr>
            <td><?= h($row["node_name"]) ?></td>
            <td><?= number_format((int)$row["message_count"]) ?></td>
        </tr>
        <?php endforeach; ?>
    </tbody>
</table>

<h2>Node Measurements and Statistics</h2>

<form class="node-picker" method="get">
    <label for="node_name">Select a registered node:</label>
    <select
        id="node_name"
        name="node_name"
        onchange="this.form.submit()"
    >
        <?php foreach ($registeredNodes as $node): ?>
        <option
            value="<?= h($node) ?>"
            <?= $node === $selectedNode ? "selected" : "" ?>
        >
            <?= h($node) ?>
        </option>
        <?php endforeach; ?>
    </select>
    <noscript><button type="submit">Show Node</button></noscript>
</form>

<p>
    <strong>Unique messages received from <?= h($selectedNode) ?>:</strong>
    <?= number_format((int)$messageCount) ?>
</p>

<table>
    <thead>
        <tr>
            <th>Measurement</th>
            <th>Average</th>
            <th>Minimum</th>
            <th>Maximum</th>
        </tr>
    </thead>
    <tbody>
        <tr>
            <td>Temperature (&deg;C)</td>
            <td><?= metric($avgTemperature, " &deg;C") ?></td>
            <td><?= metric($minTemperature, " &deg;C") ?></td>
            <td><?= metric($maxTemperature, " &deg;C") ?></td>
        </tr>
        <tr>
            <td>Humidity (%)</td>
            <td><?= metric($avgHumidity, " %") ?></td>
            <td><?= metric($minHumidity, " %") ?></td>
            <td><?= metric($maxHumidity, " %") ?></td>
        </tr>
        <tr>
            <td>Photoresistor ADC (0&ndash;1023)</td>
            <td><?= metric($avgLightAdc) ?></td>
            <td><?= metric($minLightAdc) ?></td>
            <td><?= metric($maxLightAdc) ?></td>
        </tr>
    </tbody>
</table>

<h2>Measurements for <?= h($selectedNode) ?></h2>

<div class="chart-grid">
    <div class="chart-card">
        <canvas id="temperatureChart"></canvas>
    </div>
    <div class="chart-card">
        <canvas id="humidityChart"></canvas>
    </div>
    <div class="chart-card">
        <canvas id="lightChart"></canvas>
    </div>
</div>

<h2>All Sensor Measurements</h2>

<table>
    <thead>
        <tr>
            <th>Node Name</th>
            <th>Time Received</th>
            <th>Temperature (&deg;C)</th>
            <th>Humidity (%)</th>
            <th>Photoresistor ADC</th>
        </tr>
    </thead>
    <tbody>
        <?php foreach ($measurements as $row): ?>
        <tr>
            <td><?= h($row["node_name"]) ?></td>
            <td><?= h($row["time_received"]) ?></td>
            <td><?= h($row["temperature"]) ?></td>
            <td><?= h($row["humidity"]) ?></td>
            <td>
                <?= $row["light_adc"] === null
                    ? "N/A"
                    : h($row["light_adc"]) ?>
            </td>
        </tr>
        <?php endforeach; ?>
    </tbody>
</table>

<script>
const selectedNode = <?= json_encode($selectedNode) ?>;

// Get this node's records; sensor_json.php returns a JSON array.
fetch("sensor_json.php?node_name=" + encodeURIComponent(selectedNode))
    .then(async response => {
        const payload = await response.json();

        if (!response.ok || !Array.isArray(payload)) {
            throw new Error(payload.error || "Unable to retrieve node data.");
        }

        return payload;
    })
    .then(rows => {
        const timestamps = rows.map(row => row.time_received);

        const temperatures = rows.map(row =>
            row.temperature === null ? null : Number(row.temperature)
        );

        const humidities = rows.map(row =>
            row.humidity === null ? null : Number(row.humidity)
        );

        // Existing records may have NULL light_adc values.
        const lightValues = rows.map(row =>
            row.light_adc === null ? null : Number(row.light_adc)
        );

        function makeLineChart(canvasId, label, values, color, axisTitle) {
            new Chart(document.getElementById(canvasId), {
                type: "line",
                data: {
                    labels: timestamps,
                    datasets: [{
                        label: label,
                        data: values,
                        borderColor: color,
                        backgroundColor: color,
                        borderWidth: 2,
                        tension: 0.15,
                        spanGaps: false
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    plugins: {
                        title: {
                            display: true,
                            text: selectedNode + " - " + label
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
                                text: axisTitle
                            }
                        }
                    }
                }
            });
        }

        makeLineChart(
            "temperatureChart",
            "Temperature",
            temperatures,
            "rgba(0, 170, 0, 1)",
            "Temperature (°C)"
        );

        makeLineChart(
            "humidityChart",
            "Humidity",
            humidities,
            "rgba(30, 110, 200, 1)",
            "Humidity (%)"
        );

        makeLineChart(
            "lightChart",
            "Photoresistor",
            lightValues,
            "rgba(220, 130, 0, 1)",
            "ADC reading (0–1023)"
        );
    })
    .catch(error => {
        console.error("Chart error:", error);
    });
</script>

</body>
</html>