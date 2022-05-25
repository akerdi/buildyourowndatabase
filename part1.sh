rm part1.db &&
gcc part1.c -o test &&

# 第一步
./test part1.db << EOF
insert 1 1 1
select
.exit
EOF

# 第二步
./test part1.db << EOF
select
.exit
EOF