/real/ {
    if (match($0, /[0-9]+\.[0-9]+s/)) {
        time = substr($0, RSTART, RLENGTH - 1)  # remove the trailing 's'
        print time > ENVIRON["TIMEFILE"]
        next
    }
    print "INVALID INPUT: " $0 > ENVIRON["TIMEFILE"]
}
/user|sys/ { next }
{ print }
