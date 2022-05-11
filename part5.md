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
uint32_t* internal_node_num_keys(void* node) {
  return node + INTERNAL_NODE_NUM_KEYS_OFFSET;
}
uint32_t* internal_node_right_child(void* node) {
  return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
}
uint32_t* internal_node_cell(void* node, uint32_t cell_num) {
  return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}
/*
  在内部节点node中根据子页面索引获得内存坐标
  不能超过该内存节点最大元素个数
  小于child_num 都是在left_childs中存储
  等于时是right_child
  */
uint32_t* internal_node_child(void* node, uint32_t child_num) {
  uint32_t num_keys = *internal_node_num_keys(node);
  if (child_num > num_keys) {
    printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
    exit(EXIT_FAILURE);
  } else if (child_num == num_keys) {
    return internal_node_right_child(node);
  } else {
    return internal_node_cell(node, child_num);
  }
}
uint32_t* internal_node_key(void* node, uint32_t key_num) {
  return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}
void initialize_internal_node(void* node) {
  set_node_type(node, NODE_INTERNAL);
  set_node_root(node, false);
  *internal_node_num_keys(node) = 0;
}

// 通用节点遍历函数
uint32_t get_node_max_key(void* node) {
  switch (get_node_type(node)) {
  case NODE_INTERNAL:
    return *internal_node_key(node, *internal_node_num_keys(node) - 1);
  case NODE_LEAF:
    return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
  }
}
bool is_node_root(void* node) {
  uint8_t value = *(uint8_t*)(node + IS_ROOT_OFFSET);
  return (bool)value;
}
void set_node_root(void* node, bool is_root) {
  uint8_t value = is_root;
  *(uint8_t*)(node + IS_ROOT_OFFSET) = value;
}
// 补充增加一个获取未使用过的新页面坐标: 每次新更新的pager->num_pages 不就是那个页面坐标么!
uint32_t get_unused_page_num(Pager* pager) { return pager->num_pages; }
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
```