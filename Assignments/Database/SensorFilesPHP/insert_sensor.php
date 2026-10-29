
<?php
// EE 470 - Database Assignment Part 2
// Insert sensor measurements using HTTP GET

require_once __DIR__ . "/../private/db_connect.php";

header("Content-Type: text/plain; charset=utf-8");

// Only accept GET requests
if ($_SERVER["REQUEST_METHOD"] !== "GET") {
    http_response_code(405);
    exit("Error: GET method required.");
}

// Retrieve URL parameters
$node = $_GET["node_name"] ?? "";
$temp = $_GET["temperature"] ?? null;
$humidity = $_GET["humidity"] ?? null;
$time = $_GET["time_received"] ?? null;

// Validate sensor name
if (!preg_match('/^node_[0-9]+$/', $node)
    || strlen($node) > 10) {
    http_response_code(400);
    exit("Error: Invalid node name.");
}

// Validate numeric measurements
if (!is_numeric($temp) || !is_numeric($humidity)) {
    http_response_code(400);
    exit("Error: Invalid measurement.");
}

// Check measurement ranges
if ($temp < -10 || $temp > 100 ||
    $humidity < 0 || $humidity > 100) {
    http_response_code(400);
    exit("Error: Measurement out of range.");
}

// Validate an optional timestamp
if ($time !== null) {
    $date = DateTimeImmutable::createFromFormat(
        '!Y-m-d H:i:s', $time
    );

    if (!$date ||
        $date->format('Y-m-d H:i:s') !== $time) {
        http_response_code(400);
        exit("Error: Invalid timestamp.");
    }
}

// Insert the measurement
// Database generates timestamp if omitted
if ($time === null) {
    $sql = "INSERT INTO sensor_data
            (node_name, temperature, humidity)
            VALUES (?, ?, ?)";

    $stmt = $conn->prepare($sql);
    $stmt->bind_param("sdd", $node, $temp, $humidity);
} else {
    $sql = "INSERT INTO sensor_data
            (node_name, time_received,
             temperature, humidity)
            VALUES (?, ?, ?, ?)";

    $stmt = $conn->prepare($sql);
    $stmt->bind_param(
        "ssdd", $node, $time, $temp, $humidity
    );
}

// Execute the SQL statement
try {
    $stmt->execute();
    echo "Measurement inserted successfully!";
} catch (mysqli_sql_exception $e) {
    // Log the detailed database error privately
    error_log($e->getMessage());

    http_response_code(400);
    echo "Error: Measurement rejected by database.";
}

$stmt->close();
$conn->close();
?>
