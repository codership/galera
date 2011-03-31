#
# Miscellaneous functions
#

# Sleeps variable amount of seconds (by default 1-10)
pause() #min_sleep #var_sleep
{
    local min_sleep=${1:-"1"}
    local var_sleep=${2:-"10"}
    local p=$(( $RANDOM % var_sleep + min_sleep ))

    echo "Sleeping for $p sec."
    sleep $p
}

# Pauses given processes (load) to perform consistency check
consistency_check() #pids
{
    local ret=0
    local pids="$@"

    kill -STOP $pids
    sleep 1
    check || (sleep 2; check) || (sleep 3; check) || ret=$?
    kill -CONT $pids # processes will receive SIGHUP in case of script exit
    return $ret
}
