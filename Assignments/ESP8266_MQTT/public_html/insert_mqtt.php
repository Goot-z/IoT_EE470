<?php
require_once __DIR__ . "/../private/db_connect.php";
require_once __DIR__ . "/../private/mqtt_config.php";

mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);
header("Content-Type: application/json; charset=utf-8");

if ($_SERVER["REQUEST_METHOD"] !== "POST") {
    http_response_code(405);
    exit(json_encode(["error" => "POST required."]));
}

$providedKey = $_SERVER["HTTP_X_API_KEY"] ?? "";
if (!hash_equals(MQTT_INGEST_API_KEY, $providedKey)) {
    http_response_code(401);
    exit(json_encode(["error" => "Unauthorized."]));
}

$data = json_decode(file_get_contents("php://input"), true);

if (!is_array($data)) {
    http_response_code(400);
    exit(json_encode(["error" => "Invalid JSON."]));
}

$node = $data["node_name"] ?? "";
$tempInput = $data["temperature"] ?? null;
$humidityInput = $data["humidity"] ?? null;
$lightInput = $data["light_adc"] ?? null;

if (!is_string($node) || !preg_match('/^node_[0-9]+$/', $node)) {
    http_response_code(400);
    exit(json_encode(["error" => "Invalid node_name."]));
}

if (!is_numeric($tempInput) || !is_numeric($humidityInput)) {
    http_response_code(400);
    exit(json_encode(["error" => "Temperature and humidity must be numeric."]));
}

$temp = (float)$tempInput;
$humidity = (float)$humidityInput;

if ($temp < -10 || $temp > 100 || $humidity < 0 || $humidity > 100) {
    http_response_code(400);
    exit(json_encode(["error" => "Measurement out of range."]));
}

if (!is_int($lightInput) || $lightInput < 0 || $lightInput > 1023) {
    http_response_code(400);
    exit(json_encode(["error" => "light_adc must be an integer from 0 to 1023."]));
}

$inTransaction = false;

try {
    // Only accept nodes already registered in the database.
    $check = $conn->prepare(
        "SELECT 1 FROM sensor_register WHERE node_name = ? LIMIT 1"
    );
    $check->bind_param("s", $node);
    $check->execute();
    $check->store_result();
    $registered = $check->num_rows > 0;
    $check->close();

    if (!$registered) {
        http_response_code(400);
        exit(json_encode(["error" => "Unregistered node."]));
    }

    $conn->begin_transaction();
    $inTransaction = true;

    // Timestamp is created by the database when the relay's reading arrives.
    $insert = $conn->prepare(
        "INSERT INTO sensor_data
         (node_name, time_received, temperature, humidity, light_adc)
         VALUES (?, CURRENT_TIMESTAMP, ?, ?, ?)"
    );
    $insert->bind_param("sddi", $node, $temp, $humidity, $lightInput);
    $insert->execute();
    $insert->close();

    $activity = $conn->prepare(
        "INSERT INTO sensor_activity (node_name, message_count)
         VALUES (?, 1)
         ON DUPLICATE KEY UPDATE message_count = message_count + 1"
    );
    $activity->bind_param("s", $node);
    $activity->execute();
    $activity->close();

    $conn->commit();
    $inTransaction = false;

    echo json_encode(["status" => "STORED"]);

} catch (mysqli_sql_exception $e) {
    if ($inTransaction) {
        $conn->rollback();
    }

    error_log($e->getMessage());

    if ((int)$e->getCode() === 1062) {
        echo json_encode(["status" => "ALREADY_STORED"]);
    } else {
        http_response_code(500);
        echo json_encode(["error" => "Database insert failed."]);
    }
}

$conn->close();
?>