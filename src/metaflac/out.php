<?php
/* example */
$f=join('',file('ar.json'));
$x=json_decode($f,true);

print_r($x);
