# Part6

[Part5 - 切分叶子节点、引出内部节点](./part5.md)中留有bug: insert 15 15 15 时报错: `Need to implement searching an internal node\n`

因为当前数据库结构已经变了: 之前第1页是root、类型为LEAF、只有一页；当第一页满了之后分解成root/right_child/left_child. 这时root类型就是INTERNAL了。

这章目标是能从root节点往下搜索插入数据内存坐标.

先替换报错打印:

```c
  if (get_node_type(root_node) == NODE_LEAF) {
    return leaf_node_find(table, root_page_num, key);
  } else {
-   printf("Need to implement searching an internal node\n");
-   exit(EXIT_FAILURE);
+   return internal_node_find(table, root_page_num, key);
```

实现内部节点查找插入数据内存坐标, 通过二分法逼近，直到min == max :

```c
Cursor* internal_node_find(Table* table, uint32_t page_num, uint32_t key) {
  void* node = get_page(table->pager, page_num);
  uint32_t num_keys = *internal_node_num_keys(node);

  uint32_t min = 0;
  uint32_t max = num_keys;
  while (max != min) {
    uint32_t index = (min + max) / 2;
    uint32_t indexKey = *internal_node_key(node, index);
    if (indexKey >= key) {
      max = index;
    } else {
      min = index + 1;
    }
  }
  uint32_t child_num = *internal_node_child(node, min);
  void* child = get_page(table->pager, child_num);
  // 如果找到是INTERNAL，则继续往下找
  // 直到找到LEAF 中的内存坐标
  switch (get_node_type(child)) {
  case NODE_INTERNAL:
    return internal_node_find(table, child_num, key);
  case NODE_LEAF:
    return leaf_node_find(table, child_num, key);
  }
}
```

## 测试

    $gcc part6.c -o test
    $./test aa.db // 之前已经有 1 ~ 14 数据
    $> insert 15 15 15 // Executed.
    $> insert 16 16 16 // Executed.
    $> insert 17 17 17 // Executed.
    $> insert 18 18 18 // Executed.
    $> insert 19 19 19 // Executed.
    $> insert 20 20 20 // Executed.
    $> insert 21 21 21 // Executed.
    $> insert 22 22 22 // Executed.
    $> insert 23 23 23 // Need to implement updating parent after

## Next

[Part7 - ?](./part7.md)