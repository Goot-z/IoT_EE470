
<?php
// EE 470 - Database Connection

$host = "localhost";
$username = "SAMPLENAME";
$password = "SAMPLEPASSWORD";
$database = "SAMPLEDATABASE";

// Connect to MariaDB
$conn = new mysqli(
    $host,
    $username,
    $password,
    $database
);

// Check connection
if ($conn->connect_error) {
    error_log($conn->connect_error);
    http_response_code(500);
    exit("Database connection failed.");
}
?>
