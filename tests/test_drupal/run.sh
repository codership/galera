#!/bin/bash

#
# DESCRIPTION:
#
# This scripts installs drupal site and runs drupal test suite against
# installed site. Drupal package will be fetched automatically if it is
# not found from working directory.
#
# SETUP:
#
# Simplest way to get test running is to create symbolic link
# /var/www/drupal_test which will point to ./workdir under this directory.
#
# PARAMETERS:
#
# CONCURRENCY    - number of test cases run concurrently (default 1)
# WORKDIR        - directory where drupal site is extracted and installed,
#                  should reside under www root so that site can be accessed
#                  via web server (default ./workdir)
# DRUPAL_VERSION - drupal version to be tested
# URL            - url pointing to installed site, should be specified only
#                  if default does not work
# TESTS          - drupal test suites to be run or --all to run all tests
# VERBOSE        - to run test suite in verbose mode set this to --verbose
#
# NOTES:
#
# WARNINGS:
# * Drops database drupal before site is installed
#


set -e

BASE_DIR=$(cd $(dirname $0); pwd -P)

# Parameters
CONCURRENCY=${CONCURRENCY:-"1"}
WORKDIR=${WORKDIR:-"$BASE_DIR/workdir"}
DRUPAL_VERSION=${DRUPAL_VERSION:-"7.12"}
TESTS=${TESTS:-"--all"}
VERBOSE=${VERBOSE:-""}

# Drupal pkg/dirname
DRUPAL_PKG="drupal-${DRUPAL_VERSION}.tar.gz"
DRUPAL_DIR="$(basename $DRUPAL_PKG .tar.gz)"

# Construct url (note, should be done after setting DRUPAL_DIR
# to get it right)
URL=${URL:-"http://localhost/drupal_test/$DRUPAL_DIR"}

PHP=$(which php)
DRUSH=$(which drush)

if ! test -x $DRUSH
then
    echo "Program 'drush' is required to run this test"
    exit 1
fi

if ! test -x $PHP
then
    echo "PHP is required to run this test"
    exit 1
fi

# Helpers to manage the cluster
declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"

# MySQL management
MYSQL="mysql -u${DBMS_ROOT_USER} -p${DBMS_ROOT_PSWD}
             -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]}"


# Create working directory if it does not exist yet
if ! test -d $WORKDIR
then
    mkdir -p $WORKDIR
fi

cd $WORKDIR

# Download and extract drupal package
if ! test -f $DRUPAL_PKG
then
    echo "-- Downloading drupal package"
    wget http://ftp.drupal.org/files/projects/${DRUPAL_PKG}
fi

echo "-- Extracting drupal package"
tar zxf $DRUPAL_PKG
cd $DRUPAL_DIR

# Relax permissions for installer
chmod -R a+rw sites/default
cp sites/default/default.settings.php sites/default/settings.php
chmod a+rw sites/default/settings.php

# Restart cluster
echo "-- Restarting cluster"
$SCRIPTS/command.sh restart

# Drop drupal database, set globals
$MYSQL -e "DROP DATABASE IF EXISTS drupal;
           SET GLOBAL auto_increment_increment=1;
           SET GLOBAL wsrep_drupal_282555_workaround=0;"

# Install site
echo "-- Installing drupal"
$DRUSH si \
    --db-url="mysql://${DBMS_TEST_USER}:${DBMS_TEST_PSWD}@${NODE_INCOMING_HOST[0]}:${NODE_INCOMING_PORT[0]}/drupal" \
    --account-name=drupal --account-pass=drupass --clean-url=0 -y
# Relax permissions so that dir can be cleaned up or overwritten
# automatically.
chmod -R a+rw .

# Append base_url in settings.php
echo "\$base_url = '"$URL"';" >> sites/default/settings.php

# Enable modules
MODULES=$(ls -1 modules | grep -v README.txt)
$DRUSH en $MODULES -y

# Run tests
cd scripts
echo "-- Running tests $TESTS"
$PHP run-tests.sh --concurrency $CONCURRENCY --php $PHP \
    --url $URL $VERBOSE $TESTS >& $WORKDIR/drupal-tests.log

tests_failed=$(grep -v '0 fails' $WORKDIR/drupal-tests.log | grep 'passes' | wc -l)
if test $tests_failed != 0
then
    echo "Some tests failed:"
    grep -v '0 fails' $WORKDIR/drupal-tests.log | grep 'passes'
    exit 1
else
    echo "Success"
    exit 0
fi

