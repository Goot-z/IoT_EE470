<?php
// EE 470 - ESP8266 IoT Assignment
// Insert timestamped sensor measurements using HTTP GET

require_once __DIR__ . "/../private/db_connect.php";

mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);
header("Content-Type: text/plain; charset=utf-8");

// Only accept GET requests
if ($_SERVER["REQUEST_METHOD"] !== "GET") {
    http_response_code(405);
    exit("Error: GET method required.");
}

// Retrieve URL parameters
$node = $_GET["node_name"] ?? "";
$tempInput = $_GET["temperature"] ?? "";
$humidityInput = $_GET["humidity"] ?? "";
$lightInput = $_GET["light_adc"] ?? "";
$time = $_GET["time_received"] ?? "";

// Validate the node name
if (!is_string($node)
    || !preg_match('/^node_[0-9]+$/', $node)
    || strlen($node) > 10) {
    http_response_code(400);
    exit("Error: Invalid node name.");
}

// Validate numeric sensor measurements
if (!is_numeric($tempInput) || !is_numeric($humidityInput)) {
    http_response_code(400);
    exit("Error: Invalid temperature or humidity.");
}

$temp = (float)$tempInput;
$humidity = (float)$humidityInput;

// Check temperature and humidity ranges
if ($temp < -10 || $temp > 100
    || $humidity < 0 || $humidity > 100) {
    http_response_code(400);
    exit("Error: Measurement out of range.");
}

// The photoresistor reading must be an integer from analogRead(A0)
if (!is_string($lightInput) || $lightInput === "") {
    http_response_code(400);
    exit("Error: Missing light_adc value.");
}

$lightAdc = filter_var($lightInput, FILTER_VALIDATE_INT);

if ($lightAdc === false || $lightAdc < 0 || $lightAdc > 1023) {
    http_response_code(400);
    exit("Error: light_adc must be an integer from 0 to 1023.");
}

// A timestamp is required for every measurement
if (!is_string($time) || $time === "") {
    http_response_code(400);
    exit("Error: Missing timestamp.");
}

// Require the exact format YYYY-MM-DD HH:MM:SS
$date = DateTimeImmutable::createFromFormat('!Y-m-d H:i:s', $time);
$dateErrors = DateTimeImmutable::getLastErrors();

if (!$date
    || ($dateErrors !== false
        && ($dateErrors["warning_count"] > 0
            || $dateErrors["error_count"] > 0))
    || $date->format('Y-m-d H:i:s') !== $time) {
    http_response_code(400);
    exit("Error: Invalid timestamp.");
}

$inTransaction = false;

try {
    // Reject nodes that are not registered
    $registerStmt = $conn->prepare(
        "SELECT 1 FROM sensor_register WHERE node_name = ? LIMIT 1"
    );
    $registerStmt->bind_param("s", $node);
    $registerStmt->execute();
    $registerStmt->store_result();

    $isRegistered = ($registerStmt->num_rows > 0);
    $registerStmt->close();

    if (!$isRegistered) {
        http_response_code(400);
        $conn->close();
        exit("Error: Unregistered node.");
    }

    // Keep the measurement and counter update atomic
    $conn->begin_transaction();
    $inTransaction = true;

    // Insert the measurement with its original timestamp
    $insertSql = "INSERT INTO sensor_data
                  (node_name, time_received, temperature, humidity, light_adc)
                  VALUES (?, ?, ?, ?, ?)";

    $insertStmt = $conn->prepare($insertSql);
    $insertStmt->bind_param(
        "ssddi",
        $node,
        $time,
        $temp,
        $humidity,
        $lightAdc
    );
    $insertStmt->execute();
    $insertStmt->close();

    // This runs only after a new sensor_data row was inserted.
    // A duplicate measurement is caught below and will not increment the count.
    $activitySql = "INSERT INTO sensor_activity (node_name, message_count)
                    VALUES (?, 1)
                    ON DUPLICATE KEY UPDATE
                        message_count = message_count + 1";

    $activityStmt = $conn->prepare($activitySql);
    $activityStmt->bind_param("s", $node);
    $activityStmt->execute();
    $activityStmt->close();

    $conn->commit();
    $inTransaction = false;

    // Machine-readable success response for the ESP
    echo "STORED";

} catch (mysqli_sql_exception $e) {
    if ($inTransaction) {
        $conn->rollback();
    }

    // Log database details privately; do not expose them to the requester
    error_log($e->getMessage());

    // The composite key identifies an already-stored node/timestamp pair
    if ((int)$e->getCode() === 1062) {
        http_response_code(200);
        echo "ALREADY_STORED";
    } else {
        http_response_code(400);
        echo "Error: Measurement rejected by database.";
    }
}

$conn->close();
?>