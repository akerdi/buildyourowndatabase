# Part7

上一章节后，我们发现 select 时，数据是 (2 1 1).

原因是 select 仅找了 root 节点，2 1 1 对应之前留下的的数据。所以这章目标是解决多个节点间 select。

首先修复 table_start，他的作用是通过`Cursor*(*table_start)(Table* table, uint32_t key)`根据键找对正确内存坐标:

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

这步修改完执行`chmod +x part7.sh && ./part7.sh`, 输入 15 个元素，但是仅打印了 7 个。因为在`void(*cursor_advance)(Cursor* cursor)`方法中直接判别为`cursor->end_of_table = true;`, 打印了 7 个后即停止了。

为了解决不能跳节点问题，我们为节点内容添加`Next`属性，来表达下一页的页面(节点)索引。并且由于新加入的属性，之前的数据文件内容将不适用，需要删除测试的数据文件!

首先为 LEAF_NODE_HEADER_SIZE 中加入 NEXT 内存存储:

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

最后，由于 select 会调用`void(*cursor_advance)(Cursor*)`获取下一个元素，并且判别是否到了末尾，所以此方法内我们做更新 cursor->page_num 和判别 end_of_table:

```c
void cursor_advance(Cursor* cursor) {
  uint32_t page_num = cursor->page_num;
  void* node = get_page(cursor->table->pager, page_num);
-  if (cursor->cell_num >= *leaf_node_num_cells(node)) {
-    cursor->end_of_table = true;
-  }
+  if (cursor->cell_num >= *leaf_node_num_cells(node)) {
+    // 拿出下一页坐标，next_page_num == 0 则表示没有下一页了; 否则进入新的一页
+    uint32_t next_page_num = *leaf_node_next_leaf(node);
+    if (next_page_num == 0) {
+      cursor->end_of_table = true;
+    } else {
+      cursor->page_num = next_page_num;
+      cursor->cell_num = 0;
+    }
+  }
}
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

## 下一章

[Part8 - 更新内部节点](./part8.md)
