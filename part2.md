# Part 2

这章代入Cursor。

增加Cursor struct，用于在select、insert时记录上下文。

以及翻译B+ tree。

## Cursor

增加struct Cursor, 作用于记录上下文table、要操作的row 位置row_num、以及是否是末尾end_of_table

```c
typedef struct {
  Table* table;
  uint32_t row_num;
  bool end_of_table;
} Cursor;

// select 从0开始的便捷函数
Cursor* table_start(Table* table) {
  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->row_num = 0;
  cursor->end_of_table = table->row_num == 0;

  return cursor;
}
// insert 肯定是最后插入
Cursor* table_end(Table* table) {
  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->row_num = table->row_nums;
  cursor->end_of_table = true;

  return cursor;
}
// select后主动cursor->row_num += 1; 并且判别是否偏移至末尾
void cursor_advance(Cursor* cursor) {
  cursor->row_num += 1;
  if (cursor->row_num == cursor->table->row_nums) {
    cursor->end_of_table = true;
  }
}
```

修改 `void* row_slot(Table*, uint32_t)` 为 `void* cursor_value(Cursor*)`

```
- void* row_slot(Table* table, uint32_t row_num) {
+ void* cursor_value(Cursor* cursor) {
  Pager* pager = table->pager;
+ uint32_t row_num = cursor->row_num;

  uint32_t page_num = row_num / ROW_PER_PAGES;
```

在execute_insert/execute_select中使用Cursor:

```c
ExecuteResult execute_insert(Statement* statement, Table* table) {
  Row* row_to_insert = &statement->row_to_insert;
+ Cursor* cursor = table_end(table);
- void* page = row_slot(table, table->row_nums);
+ void* page = cursor_value(cursor);
  serialize_row(page, row_to_insert);
  table->row_nums++;

+ free(cursor);
  return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement* statement, Table* table) {
  Row row;
+ Cursor* cursor = table_start(table);
  // 简单处理，select时打印全部
- FORLESS(table->row_nums) {
+ while (cursor->end_of_table) {
    // 找到i在哪个page的offset 偏移内存点
-   void* page = row_slot(table, i);
+   void* page = cursor_value(cursor);
    deserialize_row(&row, page);
    print_row(&row);
    cursor_advance(cursor);
  }
+ free(cursor);

  return EXECUTE_SUCCESS;
}
```

## 测试

当前能实现Part1 的测试

## B+ Tree

