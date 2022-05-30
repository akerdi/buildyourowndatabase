gcc part5.c -o test &&
rm part5.db | echo &&
./test part5.db << EOF
insert 1 1 1
insert 2 2 2
insert 3 3 3
insert 4 4 4
insert 5 5 5
insert 6 6 6
insert 7 7 7
insert 8 8 8
insert 9 9 9
insert 10 10 10
insert 11 11 11
insert 12 12 12
insert 13 13 13
insert 14 14 14
.btree
insert 15 15 15
EOF