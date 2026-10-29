
<?php
// EE 470 - Database Assignment Part 2
// Retrieve temperature measurements as JSON

require_once __DIR__ . "/../private/db_connect.php";

header("Content-Type: application/json; charset=utf-8");

// Retrieve measurements for node_1
$sql = "SELECT node_name, time_received,
               temperature, humidity
        FROM sensor_data
        WHERE node_name = ?
        ORDER BY time_received";

$stmt = $conn->prepare($sql);
$node = "node_1";
$stmt->bind_param("s", $node);
$stmt->execute();

$result = $stmt->get_result();

// Store measurements in an array
$data = [];

while ($row = $result->fetch_assoc()) {
    $data[] = [
        "node_name" => $row["node_name"],
        "time_received" => $row["time_received"],
        "temperature" => (float)$row["temperature"],
        "humidity" => (float)$row["humidity"]
    ];
}

// Convert PHP array to JSON
echo json_encode($data, JSON_PRETTY_PRINT);

$stmt->close();
$conn->close();
?>
