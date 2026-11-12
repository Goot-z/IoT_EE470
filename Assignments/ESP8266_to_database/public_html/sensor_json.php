<?php
// EE 470 - ESP8266 IoT Assignment
// Return measurements for one registered node as JSON

require_once __DIR__ . "/../private/db_connect.php";

mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);
header("Content-Type: application/json; charset=utf-8");

// Keep the existing chart working by defaulting to node_1.
// The updated webpage can pass ?node_name=node_2, for example.
$node = $_GET["node_name"] ?? "node_1";

// Validate the node name
if (!is_string($node)
    || !preg_match('/^node_[0-9]+$/', $node)
    || strlen($node) > 10) {
    http_response_code(400);
    echo json_encode(["error" => "Invalid node name."]);
    exit;
}

try {
    // Only return data for nodes registered in the database
    $checkStmt = $conn->prepare(
        "SELECT 1 FROM sensor_register WHERE node_name = ? LIMIT 1"
    );
    $checkStmt->bind_param("s", $node);
    $checkStmt->execute();
    $checkStmt->store_result();

    $isRegistered = ($checkStmt->num_rows > 0);
    $checkStmt->close();

    if (!$isRegistered) {
        http_response_code(404);
        echo json_encode(["error" => "Node is not registered."]);
        $conn->close();
        exit;
    }

    // Return measurements in timestamp order
    $sql = "SELECT node_name, time_received, temperature, humidity, light_adc
            FROM sensor_data
            WHERE node_name = ?
            ORDER BY time_received ASC";

    $stmt = $conn->prepare($sql);
    $stmt->bind_param("s", $node);
    $stmt->execute();
    $stmt->bind_result(
        $rowNode,
        $rowTime,
        $rowTemperature,
        $rowHumidity,
        $rowLightAdc
    );

    $measurements = [];

    while ($stmt->fetch()) {
        $measurements[] = [
            "node_name" => $rowNode,
            "time_received" => $rowTime,
            "temperature" => (float)$rowTemperature,
            "humidity" => (float)$rowHumidity,
            // Older rows may have NULL because light_adc was added later.
            "light_adc" => $rowLightAdc === null
                ? null
                : (int)$rowLightAdc
        ];
    }

    $stmt->close();
    echo json_encode($measurements);

} catch (mysqli_sql_exception $e) {
    // Keep database details in the server log
    error_log($e->getMessage());

    http_response_code(500);
    echo json_encode(["error" => "Unable to retrieve measurements."]);
}

$conn->close();
?>