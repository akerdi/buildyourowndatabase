# Part4

上个章节我们发现插入的键是没有被排序过的。这章我们将解决这个问题，并且发现冲突key、报冲突键的功能。

## 读取、设置节点类型

```c
+NodeType get_node_type(void* node) {
+ uint8_t value = *(uint8_t*)(node+NODE_TYPE_OFFSET);
+ return (NodeType)value;
+}
+void set_node_type(void* node, NodeType type) {
+ uint8_t value = type;
+ *(uint8_t*)(node + NODE_TYPE_OFFSET) = value;
+}
// 生成叶子节点时，为其赋值类型
-void initialize_leaf_node(void* node) { *leaf_node_num_cells(node) = 0; }
+void initialize_leaf_node(void* node) {
+ set_node_type(node, NODE_LEAF);
+ *leaf_node_num_cells(node) = 0;
+}
```

之前我们设置了 EXECUTE_DUPLICATE_KEY 我们用其来报错:

```c
    case EXECUTE_SUCCESS:
      printf("Executed.\n");
      break;
+   case EXECUTE_DUPLICATE_KEY:
+     printf("Error: Duplicate key.\n");
+     break;
    ...
```

到现在为止，execute_insert() 方法总是往最后一个位置插入数据。相反的，我们应该查找这个table去寻找正确的位置，然后选择插入到那里。如果key已存在，则报冲突错误。

```c
ExecuteResult execute_insert(Statement* statement, Table* table) {
  void* node = get_page(table->pager, table->root_page_num);
- if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS)) {
+ uint32_t num_cells = *leaf_node_num_cells(node);
+ if (num_cells >= LEAF_NODE_MAX_CELLS) {
    return EXECUTE_TABLE_FULL;
  }
  Row* row_to_insert = &statement->row_to_insert;
- Cursor* cursor = table_end(table);
+ uint32_t key_to_insert = row_to_insert->id;
+ Cursor* cursor = table_find(table, key_to_insert);

  // 如果是插入中间部位，则需要判别是否key相同，否则为其腾出空间插入数值
+ if (cursor->cell_num < num_cells) {
+   uint32_t key_at_index = *leaf_node_key(node, cursor->cell_num);
+   if (key_at_index == key_to_insert) {
+     return EXECUTE_DUPLICATE_KEY;
+   }
+ }

  leaf_node_insert(cursor, row_to_insert->id, row_to_insert);
  ...
```

替换 `Cursor*(*table_end)(Table*)` 为 `Cursor* (*table_find)(Table*,uint32_t)`, 目的是table_end 仅支持插入到最后；但table_find 支持找到对应的page的指定位置:

```c
-Cursor* table_end(Table* table) {
- Cursor* cursor = malloc(sizeof(Cursor));
- cursor->table = table;
- cursor->page_num = table->root_page_num;
- void* root_node = get_page(table->pager, table->root_page_num);
- uint32_t num_cells = *leaf_node_num_cells(root_node);
- cursor->cell_num = num_cells;
- cursor->end_of_table = true;
- return cursor;
-}
+Cursor* table_find(Table* table, uint32_t key) {
+ uint32_t root_page_num = table->root_page_num;
+ void* root_node = get_page(table->pager, root_page_num);
+
+ if (get_node_type(root_node) == NODE_LEAF) {
+   return leaf_node_find(table, root_page_num, key);
+ } else {
+   printf("Need to implement searching an internal node\n");
+   exit(EXIT_FAILURE);
+ }
+}
+Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
+ void* node = get_page(table->pager, page_num);
+ uint32_t num_cells = *leaf_node_num_cells(node);

+ Cursor* cursor = malloc(sizeof(Cursor));
+ cursor* table = table;
+ cursor->page_num = page_num;
  // 采用二分法逐步逼近得到具体位置坐标
+ uint32_t min = 0;
+ uint32_t max = num_cells;
+ while (max != min) {
+   uint32_t index = (min + max) / 2;
+   uint32_t indexKey = *leaf_node_key(node, index);
    // 如果键冲突，直接返回
+   if (key == indexKey) {
+     cursor->cell_num = index;
+     return cursor;
+   }
+   if (key < indexKey) {
+     max = index;
+   } else {
+     min = index + 1;
+   }
+ }
+ cursor->cell_num = min_index;
+ return cursor;
+}
```

## 测试

    $gcc part4.c -o test
    $./test aa.db
    $> insert 3 3 3 // Executed.
    $> insert 1 1 1 // Executed.
    $> insert 2 2 2 // Executed.
    $> .btree // Tree:
              // leaf (size 3)
              // -- 0 : 1
              // -- 1 : 2
              // -- 2 : 3
    $> insert 3 3 3 // Error: Duplicate key.

## Next

[Part5 - 切分叶子节点、引出内部节点](./part5.md)