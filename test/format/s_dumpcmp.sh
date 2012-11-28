#! /bin/sh

trap 'exit 1' 1 2

top=../..
bdb=$top/db/build_unix

colflag=0
dump_bdb=0
wt_name="file:wt"
while :
	do case "$1" in
	# -b means we need to dump the BDB database
	-b)
		dump_bdb=1;
		shift ;;
	# -c means it was a column-store.
	-c)
		colflag=1
		shift ;;
	-n)
		shift ;
		wt_name=$1
		shift ;;
	*)
		break ;;
	esac
done

if test $# -ne 0; then
	echo 'usage: s_dumpcmp [-bc]' >&2
	exit 1
fi

ext="\"$top/ext/collators/reverse/.libs/libwiredtiger_reverse_collator.so\""
bzip2_ext="$top/ext/compressors/bzip2/.libs/libwiredtiger_bzip2.so"
if test -e $bzip2_ext ; then
        ext="$ext,\"$bzip2_ext\""
fi
raw_ext=".libs/raw_compress.so"
if test -e $raw_ext ; then
        ext="$ext,\"$raw_ext\""
fi
snappy_ext="$top/ext/compressors/snappy/.libs/libwiredtiger_snappy.so"
if test -e $snappy_ext ; then
        ext="$ext,\"$snappy_ext\""
fi

config='extensions=['$ext']'

$top/wt -h RUNDIR -C "$config" dump $wt_name |
    sed -e '1,/^Data$/d' > RUNDIR/wt_dump

if test $dump_bdb -ne 1; then
	exit 0
fi

if test $colflag -eq 0; then
	$bdb/db_dump -p RUNDIR/bdb |
	    sed -e '1,/HEADER=END/d' \
		-e '/DATA=END/d' \
		-e 's/^ //' > RUNDIR/bdb_dump
else
	# Format stores record numbers in Berkeley DB as string keys,
	# it's simpler that way.  Convert record numbers from strings
	# to numbers.
	$bdb/db_dump -p RUNDIR/bdb |
	    sed -e '1,/HEADER=END/d' \
		-e '/DATA=END/d' \
		-e 's/^ //' |
	    sed -e 's/^0*//' \
		-e 's/\.00$//' \
		-e N > RUNDIR/bdb_dump
fi

cmp RUNDIR/wt_dump RUNDIR/bdb_dump > /dev/null

exit $?
