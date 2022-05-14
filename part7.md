# Part7

上一章节后，我们发现select 时，数据是 (2 1 1).

原因是select 仅找了root节点，2 1 1对应之前留下的的数据。所以这章就是解决select，并且是多个节点间select。

首先修复table_start，他的作用是通过`Cursor*(*table_start)(Table* table, uint32_t key)`根据键找对正确内存坐标:

```c
-Cursor* table_start(Table* table) {
-  Cursor* cursor = malloc(sizeof(Cursor));
-  cursor->table = table;
-  cursor->page_num = table->root_page_num;
-  cursor->cell_num = 0;
-
-  void* root_node = get_page(table->pager, table->root_page_num);
-  uint32_t num_cells = *leaf_node_num_cells(root_node);
-  cursor->end_of_table = num_cells == 0;
-
-  return cursor;
-}
+Cursor* table_start(Table* table) {
+  Cursor* cursor = table_find(table, 0);
+
+  void* node = get_page(table->pager, cursor->page_num);
+  uint32_t num_cells = *leaf_node_num_cells(node);
+  cursor->end_of_table = (num_cells == 0);
+
+  return cursor;
+}
```

以上，如果有15个元素，但是仅打印了7个，因为在`void(*cursor_advance)(Cursor* cursor)`方法中直接判别为`cursor->end_of_table = true;` 了.

这时我们为节点内容添加`Next`属性，来表达下一页的页面(节点)索引。并且由于新加入的属性，之前的数据文件内容将不适用，需要删除测试的数据文件aa.db!

首先为LEAF_NODE_HEADER_SIZE 中加入NEXT内存存储:

```c
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET = LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
+const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE;

// 存取方便函数
uint32_t* leaf_node_next_leaf(void* node) {
  return node + LEAF_NODE_NEXT_LEAF_OFFSET;
}
// init 叶子节点时，赋值为0(root 刚启动时也是叶子节点，所以也为0)
void initialize_leaf_node(void* node) {
  set_node_type(node, NODE_LEAF);
  set_node_root(node, false);
  *leaf_node_num_cells(node) = 0;
+ *leaf_node_next_leaf(node) = 0;
}
```

下一步，每当生成新的叶子节点时，把相连的叶子节点设置下:

```c
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
  void* old_node = get_page(cursor->table->pager, cursor->page_num);

  uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
  void* new_node = get_page(cursor->table->pager, new_page_num);
  initialize_leaf_node(new_node);
+ *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
+ *leaf_node_next_leaf(old_node) = new_page_num;
```

## 测试

    $gcc part7.c -o test
    $rm aa.db && ./test aa.db
    $> insert 1 1 1 // Executed.
    $> insert 2 2 2 // Executed.
    $> // insert [ 3 ~ 12 ]// Executed.
    $> insert 13 13 13 // Executed.
    $> insert 14 14 14 // Executed.
    $> select // 展示所有的正确数据

## Next

[Part8 - ?](./part8.md)