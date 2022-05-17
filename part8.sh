rm part8.db &&
gcc part8.c -o test &&
./test part8.db << EOF
select
insert 18 18 18
insert 7 7 7
insert 10 10 10
insert 29 29 29
insert 23 23 23
insert 4 4 4
insert 14 14 14
insert 30 30 30
insert 15 15 15
insert 26 26 26
insert 22 22 22
insert 19 19 19
insert 2 2 2
insert 1 1 1
insert 21 21 21
insert 11 11 11
insert 6 6 6
insert 20 20 20
insert 5 5 5
insert 8 8 8
insert 9 9 9
insert 3 3 3
insert 12 12 12
insert 27 27 27
insert 17 17 17
insert 16 16 16
insert 13 13 13
insert 24 24 24
insert 25 25 25
insert 28 28 28
select
.btree
EOF