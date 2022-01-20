<?php

//
// Get latest replies to an HN user
//
// Example:
//   https://hnreplies.ggerganov.com/get.php?u=dang&print=pretty
//

$nmax = 100; // max number of replies

$user_org = $_GET['u'];
$pretty = $_GET['print'];

if ($pretty === "pretty") {
    $pretty = true;
} else {
    $pretty = false;
}

$len = strlen($user_org);
if ($len < 2 || $len > 15) {
    header('Content-Type: application/json');
    echo "null";
    return;
}

$user = preg_replace("/[^\p{L}0-9_-]+/", "", $user_org);

if ($user != $user_org) {
    header('Content-Type: application/json');
    echo "null";
    return;
}

$path="/var/www/html/hnreplies/data/$user";
$cmd = "if [ -d $path ] ; then cat `find $path -type f | sort -r | head -n $nmax` ; fi";

$descriptorspec = array(
    1 => array("pipe", "w")
);

$process = proc_open($cmd, $descriptorspec, $pipes);

if (is_resource($process)) {
    $result = stream_get_contents($pipes[1]);
    fclose($pipes[1]);

    $return_value = proc_close($process);

    header('Content-Type: application/json');
    $result = rtrim(preg_replace('/\n+/', ',', $result), ',');
    $result = "{\"replies\": [$result]}";

    if ($pretty) {
        $result = json_encode(json_decode($result), JSON_PRETTY_PRINT);
    }

    echo $result;
}

?>
