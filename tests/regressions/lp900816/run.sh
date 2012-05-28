#!/bin/sh -uex

PORT=3306
HOST=127.0.0.1
USER=root
PSWD=rootpass
DB=test
TABLE=nopk
TRIES=1000

MYSQL="mysql -u$USER -p$PSWD -h$HOST -P$PORT $DB"

CREATE="
DROP TABLE IF EXISTS $DB.$TABLE;
CREATE TABLE $DB.$TABLE (i INT, j INT);"

INSERT="INSERT INTO $DB.$TABLE VALUES (1, 0),(2,0);"

UPDATE="UPDATE $DB.$TABLE SET j=j+1;"

DELETE="DELETE FROM $DB.$TABLE;"

echo $CREATE | $MYSQL

for i in $(seq 1 $TRIES)
do
    echo $INSERT | $MYSQL
    echo $UPDATE | $MYSQL >> update.log 2>&1 &
    echo $DELETE | $MYSQL
done

echo "$i tries passed"
