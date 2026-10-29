
<?php
// EE 470 - Database Assignment Part 2
// Insert sensor data using Base64-encoded parameters

require_once __DIR__ . "/../private/db_connect.php";

header("Content-Type: text/plain; charset=utf-8");

// Accept GET requests only
if ($_SERVER["REQUEST_METHOD"] !== "GET") {
    http_response_code(405);
    exit("Error: GET method required.");
}

// Retrieve the Base64 string from the URL
$encoded = $_SERVER["QUERY_STRING"] ?? "";

// Decode the Base64 string
$decoded = base64_decode($encoded, true);

if ($decoded === false) {
    http_response_code(400);
    exit("Error: Invalid Base64 encoding.");
}

// Extract individual parameters
parse_str($decoded, $parameters);

$node = $parameters["node_name"] ?? "";
$temp = $parameters["temperature"] ?? null;
$humidity = $parameters["humidity"] ?? null;
$time = $parameters["time_received"] ?? null;

// Display decoded values for demonstration
echo "Decoded Parameters:\n";
echo "Node: $node\n";
echo "Temperature: $temp\n";
echo "Humidity: $humidity\n";
echo "Time: $time\n\n";

// Validate node name
if (!preg_match('/^node_[0-9]+$/', $node)
    || strlen($node) > 10) {
    http_response_code(400);
    exit("Error: Invalid node name.");
}

// Validate measurements
if (!is_numeric($temp) || !is_numeric($humidity)) {
    http_response_code(400);
    exit("Error: Invalid measurements.");
}

// Validate measurement ranges
if ($temp < -10 || $temp > 100 ||
    $humidity < 0 || $humidity > 100) {
    http_response_code(400);
    exit("Error: Measurement out of range.");
}

// Time is required for this section
if ($time === null || $time === "") {
    http_response_code(400);
    exit("Error: Timestamp required.");
}

// Validate timestamp format
$date = DateTimeImmutable::createFromFormat(
    '!Y-m-d H:i:s', $time
);

if (!$date ||
    $date->format('Y-m-d H:i:s') !== $time) {
    http_response_code(400);
    exit("Error: Invalid timestamp.");
}

// Insert measurement using prepared statement
$sql = "INSERT INTO sensor_data
        (node_name, time_received,
         temperature, humidity)
        VALUES (?, ?, ?, ?)";

try {
    $stmt = $conn->prepare($sql);

    $stmt->bind_param(
        "ssdd", $node, $time, $temp, $humidity
    );

    $stmt->execute();

    echo "Measurement inserted successfully!";

    $stmt->close();
} catch (mysqli_sql_exception $e) {
    error_log($e->getMessage());

    http_response_code(400);
    echo "Error: Measurement rejected by database.";
}

$conn->close();
?>
