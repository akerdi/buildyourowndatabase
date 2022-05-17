rm part1.db &&
gcc part1.c -o test &&
./test part1.db << EOF
insert 1 1 1
select
.exit
EOF
./test part1.db << EOF
select
.exit
EOF