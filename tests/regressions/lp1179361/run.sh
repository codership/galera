#!/bin/bash -ue
##
#
# lp:1179361
# https://bugs.launchpad.net/codership-mysql/+bug/ 1179361
#
# BUG BACKGROUND:
#
#
# TEST SETUP:
#   - N nodes are used in master-slave mode
#   - master gets INSERTs and DELETEs from two concurrent threads
#
# SUCCESS CRITERIA
#
# In case of success all nodes should stay alive
#
# If bug is present, one node will hang
#
declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

echo "##################################################################"
echo "##             regression test for lp:1179361"
echo "##################################################################"
echo "Restarting cluster"
../../scripts/command.sh restart "--slave_threads 16"

MYSQL="mysql --batch --skip-column-names --silent --user=$DBMS_TEST_USER
       --password=$DBMS_TEST_PSWD"

MYSQL0="$MYSQL --host=${NODE_INCOMING_HOST[0]} --port=${NODE_INCOMING_PORT[0]}"

TABLE="test.lp1179361"

cat << EOF | $MYSQL0
DROP TABLE IF EXISTS $TABLE;
CREATE TABLE $TABLE (
  obj_type varchar(16) NOT NULL DEFAULT '' COMMENT 'obj_type',
  obj_id varchar(20) NOT NULL COMMENT 'obj_type編號',
  tag varchar(255) NOT NULL,
  msno bigint(20) NOT NULL DEFAULT '0' COMMENT 'user id',
  terr_id int(11) NOT NULL COMMENT '地區編號',
  PRIMARY KEY (obj_type,obj_id,msno,tag),
  KEY obj_id (obj_id),
  KEY tag (tag),
  KEY msno (msno)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 ROW_FORMAT=COMPRESSED;
EOF

ROUNDS=100000
echo "Rounds: $ROUNDS"

# Checking that all nodes are alive
check_nodes()
{
    for N in $NODE_LIST
    do
        MYSQLN="$MYSQL
                --host=${NODE_INCOMING_HOST[$N]} --port=${NODE_INCOMING_PORT[$N]}"
        if ! $MYSQLN -e 'SELECT COUNT(*) FROM '$TABLE > /dev/null
        then
            echo "Node $N is dead, test failed" >&2
            return 1
        fi
    done
}

thread_insert()
{
    local obj_type=0
    local tag="'中文測試'"
    local terr_id=0

    for i in $(seq 1 $ROUNDS)
    do
        local obj_id=$(( $RANDOM % 100 + 1 ))
        local msno=$(( $RANDOM % 100 + 1 ))

        echo "INSERT INTO $TABLE (obj_type, obj_id, tag, msno, terr_id)" \
             "VALUES ($obj_type, $obj_id, $tag, $msno, $terr_id)" \
             "ON DUPLICATE KEY UPDATE obj_type = obj_type;"

        if [ $(( $i % 1000 )) -eq 0 ]
        then
            echo "Insert thread: inserted $i" >&2
            check_nodes || return 1
        fi
    done
}

thread_delete()
{
    for i in $(seq 1 $ROUNDS)
    do
        local count=`$MYSQL0 -e 'SELECT COUNT(*) FROM '$TABLE`
        if [ $count -gt 2000 ]
        then
            local limit=$(( $count / 2 ))
            $MYSQL0 -e \
            "DELETE FROM $TABLE ORDER BY obj_type, obj_id, tag, msno LIMIT $limit;"
            echo "Delete thread: deleted $limit" >&2
        fi
        sleep 0.5
    done
}

thread_delete &
delete_pid=$!
trap "kill $delete_pid >/dev/null 2>&1 || true" TERM EXIT

thread_insert | $MYSQL0
FAILED=${PIPESTATUS[0]}

echo "Terminating delete thread..."
kill $delete_pid && wait %% > /dev/null 2>&1 || :

[ $FAILED -eq 0 ] && check_nodes || exit 1

check || (sleep 2; check) || (sleep 3; check) || exit 1

exit 0

