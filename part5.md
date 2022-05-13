# Part5

这章目的是切分叶子节点，使得上一章节我们数据库仅能存储13个元素的尴尬。

首先我们先删除之前超过叶子节点最大元素时的报错:

```c
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
  void* node = get_page(cursor->table->pager, cursor->page_num);

  uint32_t num_cells = *leaf_node_num_cells(node);
  if (num_cells >= LEAF_NODE_MAX_CELLS) {
-    printf("Need to implement splitting a leaf node.\n");
-    exit(EXIT_FAILURE);
+    leaf_node_split_and_insert(cursor, key, value);
+    return;
  }

ExecuteResult execute_insert(Statement* statement, Table* table) {
   void* node = get_page(table->pager, table->root_page_num);
   uint32_t num_cells = (*leaf_node_num_cells(node));
-  if (num_cells >= LEAF_NODE_MAX_CELLS) {
-    return EXECUTE_TABLE_FULL;
-  }
```

接着，叶子节点被切分后，需要一个上层的内部节点关联:

```
internal_node的顺序如下行:
[common_header:6][num_keys:4][right_child_pointer:4][left_chid_pointer0:4][left_child0:4][left_child_pointer1:4][left_child1:4]...
```

![Our internal node format](./images/part5/out_internal_node_format.png)

像上面这样摆放页面数据，因为子节点 pointer / key 占据如此小，可以容纳510 keys 和511 个子节点pointer(+1 right_child)。这表明我们不需要拿着一个key去遍历所有子节点内容!

|**# internal node layers**|**max # leaf nodes**|**Size of all leaf nodes**|
|-|-|-|
|0|511^0 = 1|4 KB|
|1|511^1 = 512|~2 MB|
|2|511^2 = 261,121|~1 GB|
|3|511^3 = 133,432,831|~550 GB|

实际上，我们不可能满足一个子节点把4kb全部填满，肯定有浪费的空间(wasted space). 但是我们仅仅通过4个页面，就把500GB 的数据从内存中读取查找出来。这就是为什么B-Tree 对于数据库是那么重要的数据结构了。

接下来我们增加一些内部节点的数据:

```c
// 内部节点Header Layout
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
+const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
+const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
+const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET =
    INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
+const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                           INTERNAL_NODE_NUM_KEYS_SIZE +
                                           INTERNAL_NODE_RIGHT_CHILD_SIZE;

// 内部节点Body Layout
+const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
+const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
+const uint32_t INTERNAL_NODE_CELL_SIZE =
    INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;

// 下面这些是内部节点便利函数
+uint32_t* internal_node_num_keys(void* node) {
+ return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
+}
+uint32_t* internal_node_right_child(void* node) {
+ return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
+}
+uint32_t* internal_node_cell(void* node, uint32_t cell_num) {
+ return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
+}
/*
  在内部节点node中根据子页面索引获得内存坐标
  不能超过该内存节点最大元素个数
  小于child_num 都是在left_childs中存储
  等于时是right_child
  */
+uint32_t* internal_node_child(void* node, uint32_t child_num) {
+ uint32_t num_keys = *internal_node_num_keys(node);
+ if (child_num > num_keys) {
+   printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
+   exit(EXIT_FAILURE);
+ } else if (child_num == num_keys) {
+   return internal_node_right_child(node);
+ } else {
+   return internal_node_cell(node, child_num);
+ }
+}
+uint32_t* internal_node_key(void* node, uint32_t key_num) {
+ return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
+}
+void initialize_internal_node(void* node) {
+ set_node_type(node, NODE_INTERNAL);
+ set_node_root(node, false);
+ *internal_node_num_keys(node) = 0;
+}

// 通用节点遍历函数
+uint32_t get_node_max_key(void* node) {
+ switch (get_node_type(node)) {
+ case NODE_INTERNAL:
+   return *internal_node_key(node, *internal_node_num_keys(node) - 1);
+ case NODE_LEAF:
+   return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
+ }
+}
+bool is_node_root(void* node) {
+ uint8_t value = *(uint8_t*)(node + IS_ROOT_OFFSET);
+ return (bool)value;
+}
+void set_node_root(void* node, bool is_root) {
+ uint8_t value = is_root;
+ *(uint8_t*)(node + IS_ROOT_OFFSET) = value;
+}
// 补充增加一个获取未使用过的新页面坐标: 每次新更新的pager->num_pages 不就是那个页面坐标么!
+uint32_t get_unused_page_num(Pager* pager) { return pager->num_pages; }
```

叶子节点遍历函数initialize时, set_node_root 为false, 并且`Table*(*db_open)(const char* filename)`发现是首次打开时，root_node设置set_node_root 为true:

```c
// 附加子节点切分LEFT / RIGHT 数量; 其实都是数量7
+const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2;
+const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT

void initialize_leaf_node(void* node) {
  set_node_type(node, NODE_LEAF);
+ set_node_root(node, false);
  *leaf_node_num_cells(node) = 0

Table* db_open(const char* filename) {
  ...
    void* root_node = get_page(pager, 0);
    initialize_leaf_node(root_node);
+   set_node_root(root_node, true);
  }
  return table;
```

准备工作好，接下来实现切分函数`leaf_node_split_and_insert`

```c
+void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
+ void* old_node = get_page(cursor->table->pager, cursor->page_num);
  // 首先先把新的页面做出来，并且当成是right child
+ uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
+ void* new_node = get_page(cursor->table->pager, new_page_num);
+ initialize_leaf_node(new_node);

  // 从大到小将值分到new_node和old_node去，new_node(right child)放大的值
  // 注意: i 是int32_t 类型，不是 uint32_t 哦
+ for (int32_t i = LEAF_NODE_MAX_CELLS; i>= 0; i--) {
+   void* destination_node;
+   if (i >= LEAF_NODE_LEFT_SPLIT_COUNT) {
+     destination_node = new_node;
+   } else {
+     destination_node = old_node;
+   }
    // 计算将要放到指定内存坐标
+   uint32_t index_within_node = i % LEAF_NODE_LEFT_SPLIT_COUNT;
+   void* destination = leaf_node_cell(destination_node, index_within_node);
    // 如果i为新加入的数据，则为其写入
    // 注意: 这里提前修正了原作中的bug，该bug 在chapter12章才修复
+   if (i == cursor->cell_num) {
      // 原来要放的数据被当前数据占用
      // 放的目标是还是上面用于计算得到的 index_within_node
      // 同步记录key
+     serialize_row(leaf_node_value(destination_node, index_within_node), value);
+     *leaf_node_key(destination_node, index_within_node) = key;
+   } else if (i > cursor->cell_num) {
+     memcpy(destination, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE);
+   } else {
      // 这里leaf_node_cell(old_node, i) 不 `-1` 是因为上面的已经算入了`(i == cursor->cell_num)`
+     memcpy(destination, leaf_node_cell(old_node, i), LEAF_NODE_CELL_SIZE);
+   }
+ }
  // 更新两个节点的num_cells信息
+ *leaf_node_num_cells(old_node) = LEAF_NODE_LEFT_SPLIT_COUNT;
+ *leaf_node_num_cells(new_node) = LEAF_NODE_RIGHT_SPLIT_COUNT;
  // 如果是root节点 - 从root 节点分出left child 和root节点
+ if (is_node_root(old_node)) {
+   return create_new_root(cursor->table, new_page_num);
+ } else {
+   printf("Need to implement updating parent after split\n");
+   exit(EXIT_FAILURE);
+ }
+}
void create_new_root(Table* table, uint32_t right_child_page_num) {
  void* root = get_page(table->pager, table->root_page_num);
  void* right_child = get_page(table->pager, right_child_page_num);
  // 新建left_child
  uint32_t left_child_page_num = get_unused_page_num(table->pager);
  void* left_child = get_page(table->pager, left_child_page_num);
  // 将root数据拷贝至left_child
  memcpy(left_child, root, PAGE_SIZE);
  set_node_root(left_child, false);
  // root数据内容将重写为internal格式，并且填充内容
  initialize_internal_node(root);
  set_node_root(root, true);
  *internal_node_num_keys(root) = 1;
  *internal_node_child(root, 0) = left_child_page_num;
  uint32_t left_child_max_key = get_node_max_key(left_child);
  *internal_node_key(root, 0) = left_child_max_key;
  *internal_node_right_child(root) = right_child_page_num;
}
```

以上实现了本章的目标: 切分叶子节点(14 + 1 = x + (15-x)), 先切出来right_child, 然后再切出来left_child, 然后设置root节点后，写入各自数据。

补充打印数据，方便查看:

```c
-void print_leaf_node(void* node) {
-  uint32_t num_cells = *leaf_node_num_cells(node);
-  printf("leaf (size %d)\n", num_cells);
-  for (uint32_t i = 0; i < num_cells; i++) {
-    uint32_t key = *leaf_node_key(node, i);
-    printf("  - %d : %d\n", i, key);
-  }
-}
+void indent(uint32_t level) {
+  for (uint32_t i = 0; i < level; i++) {
+    printf("  ");
+  }
+}
+
+void print_tree(Pager* pager, uint32_t page_num, uint32_t indentation_level) {
+  void* node = get_page(pager, page_num);
+  uint32_t num_keys, child;
+
+  switch (get_node_type(node)) {
+    case NODE_INTERNAL:
+      num_keys = *internal_node_num_keys(node);
+      indent(indentation_level);
+      printf("- internal (size %d)\n", num_keys);
+      for (uint32_t i = 0; i < num_keys; i++) {
+        child = *internal_node_child(node, i);
+        print_tree(pager, child, indentation_level + 1);
+
+        indent(indentation_level + 1);
+        printf("- key %d\n", *internal_node_key(node, i));
+      }
+      child = *internal_node_right_child(node);
+      print_tree(pager, child, indentation_level + 1);
+      break;
+    case NODE_LEAF:
+      num_keys = *leaf_node_num_cells(node);
+      indent(indentation_level);
+      printf("- leaf (size %d)\n", num_keys);
+      for (uint32_t i = 0; i < num_keys; i++) {
+        indent(indentation_level + 1);
+        printf("- %d\n", *leaf_node_key(node, i));
+      }
+      break;
+  }
+}

// 更新调用函数
   } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
     printf("Tree:\n");
-    print_leaf_node(get_page(table->pager, 0));
+    print_tree(table->pager, 0, 0);
     return META_COMMAND_SUCCESS;
```

## 测试

    $gcc part5.c -o test
    $rm aa.db && ./test aa.db
    $> insert 1 1 1 // Executed.
    $> insert 2 2 2 // Executed.
    $> // insert [ 3 ~ 12 ]// Executed.
    $> insert 13 13 13 // Executed.
    $> insert 14 14 14 // Executed.
    $> .btree // Tree:
              // - internal (size 1)
              //   - leaf (size 7)
              //      - 1
              //      - 2
              //      - 3
              //      - 4
              //      - 5
              //      - 6
              //      - 7
              //    - key   - leaf (size 7)
              //      - 8
              //      - 9
              //      - 10
              //      - 11
              //      - 12
              //      - 13
              //      - 14
    $> insert 15 15 15 // Need to implement searching an internal node

## Next

[Part6 - 内部节点查找](./part6.md)