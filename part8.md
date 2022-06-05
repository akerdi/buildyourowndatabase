# Part8

本章目标是支持数据的不断写入，解决数据只能写入21个元素的问题!

上一章知道，写入数据超过21个时，报错`Need to implement updating parent after split`，原因是当写到21个(从1录入到20，不同录入结果会有不同)，节点分布如: `|root/internal|right|left|` 其中的left 或者right也满了。此时切分了叶子节点，但是缺少了更新父节点描述数据.

首先还是先准备我们要准备的数据:

```c
// 切分internal_node_find方法, 上半部分根据key找到index的单独形成一个方法
+uint32_t internal_node_find_child(void* node, uint32_t key) {
+  uint32_t num_keys = *internal_node_num_keys(node);
+
+  uint32_t min = 0;
+  uint32_t max = num_keys;
+  while (min != max) {
+    uint32_t index = (min + max) / 2;
+    uint32_t indexKey = *internal_node_key(node, index);
+    if (indexKey >= key) {
+      max = index;
+    } else {
+      min = index + 1;
+    }
+  }
+  return min;
+}
Cursor* internal_node_find(Table* table, uint32_t page_num, uint32_t key) {
  void* node = get_page(table->pager, page_num);

+  uint32_t min = internal_node_find_child(node, key);
  // TODO 这里判别key 如果大于min的indexKey，则写到右侧；否则使用min
  uint32_t child_num = *internal_node_child(node, min);
  ...
```

```c
// 新增读写node_parent 的方法
+uint32_t* node_parent(void* node) {
+ return node + PARENT_NODE_OFFSET;
+}
// 更新内部节点key 的内存
+void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key) {
+ uint32_t old_child_index = internal_node_find_child(node, old_key);
+ *internal_node_key(node, old_child_index) = new_key;
+}
```

```c

void create_new_root(Table* table, uint32_t right_child_page_num) {
  void* root = get_page(table->pager, table->root_page_num);
+  void* right_child = get_page(table->pager, right_child_page);
  ...
  *internal_node_key(root, 0) = left_child_max_key;
  *internal_node_right_child(root) = right_child_page_num;
  // left_child 和right_child 的父节点都是root
+  *node_parent(left_child) = table->root_page_num;
+  *node_parent(right_child) = table->root_page_num;
}

void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
  void* old_node = get_page(cursor->table->pager, cursor->page_num);
  // 获取源节点最大的key
+  uint32_t old_max = get_node_max_key(old_node);
  uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
  void* new_node = get_page(cursor->table->pager, new_page_num);
  initialize_leaf_node(new_node);
  // 新的节点的父节点和源节点父节点一致
+  *node_parent(new_node) = *node_parent(old_node);
  ...
  if (is_node_root(old_node)) {
    return create_new_root(cursor->table, new_page_num);
  } else {
-    printf("Need to implement updating parent after split\n");
-    exit(EXIT_FAILURE);
    // 上面已经将一个满的页面切分成两个页面
    // 且得知old_node 不是root节点，所以目标就是更新原来的internal节点中元素
+    uint32_t parent_page_num = *node_parent(old_node);
+    uint32_t new_max = get_node_max_key(old_node); // 上面刚更新过了，所以是new_max
+    void* parent = get_page(cursor->table->pager, parent_page_num);
    // 更新父节点中max大小
    // 如录入1~20后，录入21,
    // 此时替换父节点中原old_max-->new_max；(仅替换，新增的则不管)
+    update_internal_node_key(parent, old_max, new_max);
+    internal_node_insert(cursor->table, parent_page_num, new_page_num);
  }
}
```

上面页面都已准备完毕，只是内部节点没有更新相关信息:

```c
// 这个方法的作用是，为父节点新增子节点的描述
// 并且，如果max(new_child) >
// max(right_child)，需要替换right_child的内存数据为new_child;
// 否则，在internal中添加new_child 的内存页面坐标描述(page_num 和 key)
+void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
+ void* parent = get_page(table->pager, parent_page_num);
+ void* child = get_page(table->pager, child_page_num);
+ uint32_t child_max_key = get_node_max_key(child);
+ uint32_t index = internal_node_find_child(parent, child_max_key);
+
+ uint32_t original_num_keys = *internal_node_num_keys(parent);
+ *internal_node_num_keys(parent) = original_num_keys + 1;
+
+ uint32_t right_child_page_num = *internal_node_right_child(parent);
+ void* right_child = get_page(table->pager, right_child_page_num);
+
+ if (child_max_key > get_node_max_key(right_child)) {
+   // 分解出来的max_key > 右节点max_key
+   // 内部节点描述中: 右节点拼接为新的子节点；child放到right_child 中；
+   *internal_node_child(parent, original_num_keys) = right_child_page_num;
+   *internal_node_key(parent, original_num_keys) =
        get_node_max_key(right_child);
+   *internal_node_right_child(parent) = child_page_num;
+ } else {
+   // 将内部节点的子节点移动，产生出一个空位
+   for (uint32_t i = original_num_keys; i > index; i--) {
+     void* destination = internal_node_cell(parent, i);
+     void* source = internal_node_cell(parent, i -1);
+     memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
+   }
+   // 空位中放入child 信息
+   *internal_node_child(parent, index) = child_page_num;
+   *internal_node_key(parent, index) = child_max_key;
+ }
+}
```

以上数据在执行 `sh part8.sh`后发现.btree 有个key错误:

```
    - 5
    - 6
    - 7
  - key 1
```

这个key肯定是7，不是1!

作者在大量debug后发现，是因为内存计算偏移出错的原因:

```c
uint32_t* internal_node_key(void* node, uint32_t key_num) {
-  return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
+  return (void*)internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}
```

因为`uint32_t*(*internal_node_cell)(void*, uint32_t)`返回的是 uint32_t*。INTERNAL_NODE_CHILD_SIZE = 4, 按照指针算数, 本意是 `+= 4`, 但是指针类型为`uint32_t*` 则表示 `4 * sizeof(uint32_t)` !

## 测试

    $chmod +x part8.sh && ./part8.sh

## END

到这里，数据库的文章结束了。如果数据库要做更多的优化，基本是从当前功能展开。