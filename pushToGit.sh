#!/usr/local/env bash

set -e;

sudo git add *

sudo git commit -m "bashed update"

sudo git remote rm origin

sudo git remote add origin "https://github.com/MrSoir/d1miniirrigation.git"

sudo git push -u origin master
