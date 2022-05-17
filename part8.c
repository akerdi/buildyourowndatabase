#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// open
#include <fcntl.h>
#include <unistd.h>

// 对第一个字符为 '.' 原始输入命令的解析
typedef enum {
  // 对.exit .help .btree 等命令识别，成功则LOOP
  META_COMMAND_SUCCESS,
  // 不识别报错
  META_COMMAND_UNRECOGNIZED
} MetaCommandResult;

// 识别InputBuffer 类型，成功则转成Statement对象
typedef enum {
  PREPARE_SUCCESS,
  PREPARE_SYNTAX_ERROR,
  PREPARE_STRING_TOO_LONG,
  PREPARE_NEGATIVE_ID,
  PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

// 操作类型
typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;

// 执行结果状态码
typedef enum {
  EXECUTE_SUCCESS,
  EXECUTE_DUPLICATE_KEY,
  EXECUTE_FULL_TABLE
} ExecuteResult;

typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;

// 输入原文的结构
typedef struct {
  char* buffer;
  uint32_t buffer_length;
  uint32_t str_length;
} InputBuffer;

InputBuffer* new_input_buffer() {
  InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->str_length = 0;
  input_buffer->buffer_length = 0;
  return input_buffer;
}
void del_input_buffer(InputBuffer* input_buffer) {
  free(input_buffer->buffer);
  free(input_buffer);
}

// 指明username 大小为32字节
const uint32_t COLUMN_USERNAME = 32;
// 指明email 大小为255字节
const uint32_t COLUMN_EMAIL = 255;

// Row 代表写入的表类型结构 |id(4)|username(33)|email(256)|
typedef struct {
  uint32_t id;
  // +1 是为了给'\0'保留一位，下同
  char username[COLUMN_USERNAME + 1];
  char email[COLUMN_EMAIL + 1];
} Row;

// Statement 对象为操作对象
typedef struct {
  StatementType type;
  Row row_to_insert;
} Statement;

// 便捷宏
#define FORLESS(less) for (int i = 0; i < less; i++)
// 查看属性大小
#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

// 以下描述字段大小，ROW_SIZE指真实数据大小
const uint32_t ID_OFFSET = 0;
const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;            // 4
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username); // 33
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;   // 37 = 4 + 33
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);       // 256
const uint32_t ROW_SIZE =
    ID_SIZE + USERNAME_SIZE + EMAIL_SIZE; // 293 = 37 + 256

// 缓存按整块读取大小4
// kilobytes(极大多数系统架构的虚拟内存的page大小都为4kb)，如果每次都读整块，那读写效率是最大的
const uint32_t PAGE_SIZE = 4096;
const uint32_t TABLE_MAX_PAGES = 100; // 模拟有100页

// 页面属性
typedef struct {
  void* pages[TABLE_MAX_PAGES];
  int file_descriptor;
  int file_length;
  // 记录页面数量
  uint32_t num_pages;
} Pager;

// Table属性
typedef struct {
  // 保存页面数据，方便上下文获取
  Pager* pager;
  // 记录root页面坐标
  uint32_t root_page_num;
} Table;

typedef struct {
  Table* table;
  uint32_t page_num;
  uint32_t cell_num;
  bool end_of_table;
} Cursor;

// 通用节点Header Layout
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint8_t COMMON_NODE_HEADER_SIZE =
    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

// 叶子节点的Header Layout
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET =
    LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                       LEAF_NODE_NUM_CELLS_SIZE +
                                       LEAF_NODE_NEXT_LEAF_SIZE;

// 叶子节点Body Layout
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET =
    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS =
    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;
const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2;
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT =
    (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT;

// 内部节点Header Layout
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET =
    INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                           INTERNAL_NODE_NUM_KEYS_SIZE +
                                           INTERNAL_NODE_RIGHT_CHILD_SIZE;

// 内部节点Body Layout
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CELL_SIZE =
    INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;

NodeType get_node_type(void* node) {
  uint8_t value = *(uint8_t*)(node + NODE_TYPE_OFFSET);
  return (NodeType)value;
}
void set_node_type(void* node, NodeType type) {
  uint8_t value = type;
  *(uint8_t*)(node + NODE_TYPE_OFFSET) = value;
}
bool is_node_root(void* node) {
  uint8_t value = *(uint8_t*)(node + IS_ROOT_OFFSET);
  return (bool)value;
}
void set_node_root(void* node, bool is_root) {
  uint8_t value = is_root;
  *(uint8_t*)(node + IS_ROOT_OFFSET) = value;
}
uint32_t* node_parent(void* node) { return node + PARENT_POINTER_OFFSET; }

uint32_t* leaf_node_num_cells(void* node) {
  return node + LEAF_NODE_NUM_CELLS_OFFSET;
}
uint32_t* leaf_node_next_leaf(void* node) {
  return node + LEAF_NODE_NEXT_LEAF_OFFSET;
}
void* leaf_node_cell(void* node, uint32_t cell_num) {
  return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}
uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num);
}
void* leaf_node_value(void* node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}
void initialize_leaf_node(void* node) {
  set_node_type(node, NODE_LEAF);
  set_node_root(node, false);
  *leaf_node_num_cells(node) = 0;
  *leaf_node_next_leaf(node) = 0;
}

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
  // internal_node_cell 返回uint32_t*
  // 所以在指针偏移时，就会变成 4(uint32_t*) 指针偏移4(INTERNAL_NODE_CHILD_SIZE)
  // 实际就是 4 * 4；而不是0 + 4
  // 指针偏移不要搞错
  return (void*)internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}
void initialize_internal_node(void* node) {
  set_node_type(node, NODE_INTERNAL);
  set_node_root(node, false);
  *internal_node_num_keys(node) = 0;
}

uint32_t get_node_max_key(void* node) {
  switch (get_node_type(node)) {
  case NODE_INTERNAL:
    return *internal_node_key(node, *internal_node_num_keys(node) - 1);
  case NODE_LEAF:
    return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
  }
}
uint32_t get_unused_page_num(Pager* pager) { return pager->num_pages; }

///////////////
void* get_page(Pager* pager, uint32_t page_num);
Cursor* table_find(Table* table, uint32_t key);
///////////////

void print_constants() {
  printf("ROW_SIZE: %d\n", ROW_SIZE);
  printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
  printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
  printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
  printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
  printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
  printf("LEAF_NODE_LEFT_SPLIT_COUNT: %d\n", LEAF_NODE_LEFT_SPLIT_COUNT);
  printf("LEAF_NODE_RIGHT_SPLIT_COUNT: %d\n", LEAF_NODE_RIGHT_SPLIT_COUNT);
}
void indent(uint32_t level) {
  FORLESS(level) { printf("  "); }
}
void print_tree(Pager* pager, uint32_t page_num, uint32_t indentation_level) {
  void* node = get_page(pager, page_num);
  uint32_t num_keys, child;

  switch (get_node_type(node)) {
  case NODE_INTERNAL:
    num_keys = *internal_node_num_keys(node);
    indent(indentation_level);
    printf("- internal (size %d)\n", num_keys);
    FORLESS(num_keys) {
      child = *internal_node_child(node, i);
      print_tree(pager, child, indentation_level + 1);

      indent(indentation_level + 1);
      printf("- key %d\n", *internal_node_key(node, i));
    }
    child = *internal_node_right_child(node);
    print_tree(pager, child, indentation_level + 1);
    break;
  case NODE_LEAF:
    num_keys = *leaf_node_num_cells(node);
    indent(indentation_level);
    printf("- leaf (size %d)\n", num_keys);
    FORLESS(num_keys) {
      indent(indentation_level + 1);
      printf("- %d\n", *leaf_node_key(node, i));
    }
    break;
  }
}

Cursor* table_start(Table* table) {
  Cursor* cursor = table_find(table, 0);

  void* node = get_page(table->pager, cursor->page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);
  cursor->end_of_table = num_cells == 0;

  return cursor;
}
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
  void* node = get_page(table->pager, page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);

  Cursor* cursor = malloc(sizeof(Cursor));
  cursor->table = table;
  cursor->page_num = page_num;

  uint32_t min = 0;
  uint32_t max = num_cells;
  while (max != min) {
    uint32_t index = (min + max) / 2;
    uint32_t indexKey = *leaf_node_key(node, index);
    if (key == indexKey) {
      cursor->cell_num = index;
      return cursor;
    }
    if (key < indexKey) {
      max = index;
    } else {
      min = index + 1;
    }
  }
  cursor->cell_num = min;
  return cursor;
}
// 内部节点根据key 获取其索引位置
uint32_t internal_node_find_child(void* node, uint32_t key) {
  uint32_t num_keys = *internal_node_num_keys(node);

  uint32_t min = 0;
  uint32_t max = num_keys;
  while (min != max) {
    uint32_t index = (min + max) / 2;
    uint32_t indexKey = *internal_node_key(node, index);
    if (indexKey >= key) {
      max = index;
    } else {
      min = index + 1;
    }
  }
  return min;
}
Cursor* internal_node_find(Table* table, uint32_t page_num, uint32_t key) {
  void* node = get_page(table->pager, page_num);
  uint32_t min = internal_node_find_child(node, key);
  uint32_t child_num = *internal_node_child(node, min);
  void* child = get_page(table->pager, child_num);
  switch (get_node_type(child)) {
  case NODE_INTERNAL:
    return internal_node_find(table, child_num, key);
  case NODE_LEAF:
    return leaf_node_find(table, child_num, key);
  }
}
Cursor* table_find(Table* table, uint32_t key) {
  uint32_t root_page_num = table->root_page_num;
  void* root_node = get_page(table->pager, root_page_num);

  if (get_node_type(root_node) == NODE_LEAF) {
    return leaf_node_find(table, root_page_num, key);
  } else {
    return internal_node_find(table, root_page_num, key);
  }
}
void cursor_advance(Cursor* cursor) {
  uint32_t page_num = cursor->page_num;
  void* node = get_page(cursor->table->pager, page_num);

  cursor->cell_num += 1;
  if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
    uint32_t next_page_num = *leaf_node_next_leaf(node);
    if (next_page_num == 0) {
      cursor->end_of_table = true;
    } else {
      cursor->page_num = next_page_num;
      cursor->cell_num = 0;
    }
  }
}

void del_table(Table* table) {
  free(table->pager);
  table->pager = NULL;
  free(table);
}
// 根据打开的文件，返回出Table上下文
Table* db_open(const char* filename);
MetaCommandResult do_meta_command(InputBuffer* intput_buffer, Table* table);
void print_row(Row* row) {
  printf("(%d %s %s)\n", row->id, row->username, row->email);
}
// 读取用户输入
void read_line(InputBuffer* input_buffer) {
  ssize_t bytes_read =
      getline(&input_buffer->buffer, &input_buffer->buffer_length, stdin);
  if (bytes_read == -1) {
    printf("get user input error: %s.\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  input_buffer->buffer[strlen(input_buffer->buffer) - 1] = '\0';
  input_buffer->str_length = strlen(input_buffer->buffer);
}

void* get_page(Pager* pager, uint32_t page_num) {
  if (pager->pages[page_num] == NULL) {
    void* page = malloc(PAGE_SIZE);
    // 判别原始文件内容大于查询页码
    uint32_t file_page_full_num = pager->file_length / PAGE_SIZE;
    if (pager->file_length % PAGE_SIZE) {
      file_page_full_num += 1;
    }
    // 超过则将文件数据拷贝给page内存
    if (file_page_full_num >= page_num) {
      off_t offset =
          lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
      if (offset == -1) {
        printf("get page error seek: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
      }
      ssize_t read_bytes = read(pager->file_descriptor, page, PAGE_SIZE);
      if (read_bytes == -1) {
        printf("get page error read: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
      }
    }
    pager->pages[page_num] = page;
    if (page_num >= pager->num_pages) {
      pager->num_pages = page_num + 1;
    }
  }
  return pager->pages[page_num];
}

void deserialize_row(Row* target, void* source) {
  memcpy(&target->id, source + ID_OFFSET, ID_SIZE);
  memcpy(&target->username, source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&target->email, source + EMAIL_OFFSET, EMAIL_SIZE);
}
void serialize_row(void* target, Row* source) {
  memcpy(target + ID_OFFSET, &source->id, ID_SIZE);
  memcpy(target + USERNAME_OFFSET, &source->username, USERNAME_SIZE);
  memcpy(target + EMAIL_OFFSET, &source->email, EMAIL_SIZE);
}

void* cursor_value(Cursor* cursor) {
  uint32_t page_num = cursor->page_num;
  Pager* pager = cursor->table->pager;
  void* page = get_page(pager, page_num);
  return leaf_node_value(page, cursor->cell_num);
}
// 替换内部节点的key
void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key) {
  // 先根据old_key找到对应索引
  uint32_t old_child_index = internal_node_find_child(node, old_key);
  // 拿到内存坐标后，替换为new_key
  *internal_node_key(node, old_child_index) = new_key;
}
// 这个方法的作用是，为父节点新增子节点的描述
// 并且，如果max(new_child) >
// max(right_child)，需要替换right_child的内存数据为new_child;
// 否则，在internal中添加new_child 的内存页面坐标描述(page_num 和 key)
void internal_node_insert(Table* table, uint32_t parent_page_num,
                          uint32_t child_page_num) {
  void* parent = get_page(table->pager, parent_page_num);
  void* child = get_page(table->pager, child_page_num);
  uint32_t child_max_key = get_node_max_key(child);
  uint32_t index = internal_node_find_child(parent, child_max_key);

  uint32_t original_num_keys = *internal_node_num_keys(parent);
  *internal_node_num_keys(parent) = original_num_keys + 1;

  uint32_t right_child_page_num = *internal_node_right_child(parent);
  void* right_child = get_page(table->pager, right_child_page_num);

  if (child_max_key > get_node_max_key(right_child)) {
    *internal_node_child(parent, original_num_keys) = right_child_page_num;
    *internal_node_key(parent, original_num_keys) =
        get_node_max_key(right_child);
    *internal_node_right_child(parent) = child_page_num;
  } else {
    for (uint32_t i = original_num_keys; i > index; i--) {
      void* destination = internal_node_cell(parent, i);
      void* source = internal_node_cell(parent, i);
      memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
    }
    *internal_node_child(parent, index) = child_page_num;
    *internal_node_key(parent, index) = child_max_key;
  }
}
void create_new_root(Table* table, uint32_t right_child_page_num) {
  void* root = get_page(table->pager, table->root_page_num);
  void* right_child = get_page(table->pager, right_child_page_num);
  // 生成left child
  uint32_t left_child_page_num = get_unused_page_num(table->pager);
  void* left_child = get_page(table->pager, left_child_page_num);
  // left_child 内容其实就是root内容(root 内容之后变更)
  memcpy(left_child, root, PAGE_SIZE);
  set_node_root(left_child, false);
  // 开始设置root 内容数据
  initialize_internal_node(root);
  set_node_root(root, true);
  *internal_node_num_keys(root) = 1;
  *internal_node_child(root, 0) = left_child_page_num;
  uint32_t left_child_max_key = get_node_max_key(left_child);
  *internal_node_key(root, 0) = left_child_max_key;
  *internal_node_right_child(root) = right_child_page_num;

  *node_parent(left_child) = table->root_page_num;
  *node_parent(right_child) = table->root_page_num;
}
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
  void* old_node = get_page(cursor->table->pager, cursor->page_num);
  uint32_t old_max = get_node_max_key(old_node);
  uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
  void* new_node = get_page(cursor->table->pager, new_page_num);
  initialize_leaf_node(new_node);
  *node_parent(new_node) = *node_parent(old_node);
  *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
  *leaf_node_next_leaf(old_node) = new_page_num;

  for (int32_t i = LEAF_NODE_MAX_CELLS; i >= 0; i--) {
    void* destination_node;
    if (i >= LEAF_NODE_LEFT_SPLIT_COUNT) {
      destination_node = new_node;
    } else {
      destination_node = old_node;
    }
    uint32_t index_within_node = i % LEAF_NODE_LEFT_SPLIT_COUNT;
    void* destination = leaf_node_cell(destination_node, index_within_node);

    if (i == cursor->cell_num) {
      serialize_row(leaf_node_value(destination_node, index_within_node),
                    value);
      *leaf_node_key(destination_node, index_within_node) = key;
    } else if (i > cursor->cell_num) {
      memcpy(destination, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE);
    } else {
      memcpy(destination, leaf_node_cell(old_node, i), LEAF_NODE_CELL_SIZE);
    }
  }
  *leaf_node_num_cells(old_node) = LEAF_NODE_LEFT_SPLIT_COUNT;
  *leaf_node_num_cells(new_node) = LEAF_NODE_RIGHT_SPLIT_COUNT;
  if (is_node_root(old_node)) {
    return create_new_root(cursor->table, new_page_num);
  } else {
    // 上面已经将一个满的页面切分成两个页面
    // 且得知old_node 不是root节点，所以目标就是更新原来的internal节点中元素
    uint32_t parent_page_num = *node_parent(old_node);
    uint32_t new_max = get_node_max_key(old_node);
    void* parent = get_page(cursor->table->pager, parent_page_num);
    // 更新父节点中max大小
    // 如录入1~20后，录入21,
    // 此时替换父节点中原old_max-->new_max；(仅替换，新增的则不管)
    update_internal_node_key(parent, old_max, new_max);
    internal_node_insert(cursor->table, parent_page_num, new_page_num);
  }
}
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
  void* node = get_page(cursor->table->pager, cursor->page_num);

  uint32_t num_cells = *leaf_node_num_cells(node);
  if (num_cells >= LEAF_NODE_MAX_CELLS) {
    leaf_node_split_and_insert(cursor, key, value);
    return;
  }
  if (cursor->cell_num < num_cells) {
    for (uint32_t i = num_cells; i > cursor->cell_num; i--) {
      memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
             LEAF_NODE_CELL_SIZE);
    }
  }
  *(leaf_node_num_cells(node)) += 1;
  *(leaf_node_key(node, cursor->cell_num)) = key;
  serialize_row(leaf_node_value(node, cursor->cell_num), value);
}
ExecuteResult execute_insert(Statement* statement, Table* table) {
  void* node = get_page(table->pager, table->root_page_num);
  uint32_t num_cells = *leaf_node_num_cells(node);

  Row* row_to_insert = &statement->row_to_insert;
  uint32_t key_to_insert = row_to_insert->id;
  Cursor* cursor = table_find(table, key_to_insert);

  if (cursor->cell_num < num_cells) {
    uint32_t key_at_index = *leaf_node_key(node, cursor->cell_num);
    if (key_at_index == key_to_insert) {
      return EXECUTE_DUPLICATE_KEY;
    }
  }

  leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

  free(cursor);
  return EXECUTE_SUCCESS;
}
ExecuteResult execute_select(Statement* statement, Table* table) {
  Row row;
  Cursor* cursor = table_start(table);
  // 简单处理，select时打印全部
  while (!cursor->end_of_table) {
    // 找到i在哪个page的offset 偏移内存点
    void* page = cursor_value(cursor);
    deserialize_row(&row, page);
    print_row(&row);
    cursor_advance(cursor);
  }
  free(cursor);

  return EXECUTE_SUCCESS;
}
ExecuteResult execute_statement(Statement* statement, Table* table) {
  switch (statement->type) {
  case STATEMENT_INSERT:
    return execute_insert(statement, table);
  case STATEMENT_SELECT:
    return execute_select(statement, table);
  }
}
// InputBuffer -> Statement
PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* statement) {
  static char* token = " ";
  strtok(input_buffer->buffer, token);
  char* idStr = strtok(NULL, token);
  char* username = strtok(NULL, token);
  char* email = strtok(NULL, token);
  if (!idStr || !username || !email) {
    return PREPARE_SYNTAX_ERROR;
  }
  uint32_t id = atoi(idStr);
  if (id < 0) {
    return PREPARE_NEGATIVE_ID;
  }
  if (strlen(username) > COLUMN_USERNAME) {
    return PREPARE_STRING_TOO_LONG;
  }
  if (strlen(email) > COLUMN_EMAIL) {
    return PREPARE_STRING_TOO_LONG;
  }
  statement->type = STATEMENT_INSERT;
  statement->row_to_insert.id = id;
  strcpy(statement->row_to_insert.username, username);
  strcpy(statement->row_to_insert.email, email);
  return PREPARE_SUCCESS;
}
// InputBuffer -> Statement 入口
PrepareResult prepare_statement(InputBuffer* input_buffer,
                                Statement* statement) {
  if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
    return prepare_insert(input_buffer, statement);
  }
  if (strcmp(input_buffer->buffer, "select") == 0) {
    statement->type = STATEMENT_SELECT;
    return PREPARE_SUCCESS;
  }
  return PREPARE_UNRECOGNIZED_STATEMENT;
}
int main(int argc, char** argv) {
  char* filename;
  if (argc > 1) {
    filename = argv[1];
  }
  Table* table = db_open(filename);
  while (true) {
    printf("> ");
    InputBuffer* input_buffer = new_input_buffer();
    read_line(input_buffer);

    // 对原字符进行识别是否有辅助指令
    if (input_buffer->buffer[0] == '.') {
      switch (do_meta_command(input_buffer, table)) {
      case META_COMMAND_SUCCESS:
        continue;
      case META_COMMAND_UNRECOGNIZED:
        printf("unrecognize command: %s.\n", input_buffer->buffer);
        continue;
      }
    }
    Statement statement;
    switch (prepare_statement(input_buffer, &statement)) {
    case PREPARE_SUCCESS:
      break;
    case PREPARE_NEGATIVE_ID:
      printf("input ID is negative: %s.\n", input_buffer->buffer);
      continue;
    case PREPARE_SYNTAX_ERROR:
      printf("input syntax is error: %s.\n", input_buffer->buffer);
      continue;
    case PREPARE_STRING_TOO_LONG:
      printf("input variable is too long: %s.\n", input_buffer->buffer);
      continue;
    case PREPARE_UNRECOGNIZED_STATEMENT:
      printf("input unrecognized: %s.\n", input_buffer->buffer);
      continue;
    }
    switch (execute_statement(&statement, table)) {
    case EXECUTE_SUCCESS:
      printf("Executed.\n");
      break;
    case EXECUTE_DUPLICATE_KEY:
      printf("Error: Duplicate key.\n");
      break;
    case EXECUTE_FULL_TABLE:
      printf("Table insertion is full!");
      break;
    }
    del_input_buffer(input_buffer);
  }
  return 0;
}

void pager_flush(Pager* pager, uint32_t page_num) {
  if (pager->pages[page_num] == NULL) {
    printf("flush error by empty page at %d, size: %d .\n", page_num,
           PAGE_SIZE);
    exit(EXIT_FAILURE);
  }
  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
  if (offset == -1) {
    printf("flush seek page at %d, error: %s.\n", page_num, strerror(errno));
    exit(EXIT_FAILURE);
  }
  ssize_t write_bytes =
      write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);
  if (write_bytes == -1) {
    printf("flush write page at %d, error: %s.\n", page_num, strerror(errno));
    exit(EXIT_FAILURE);
  }
}
void db_close(Table* table) {
  Pager* pager = table->pager;
  FORLESS(pager->num_pages) {
    if (pager->pages[i] != NULL) {
      pager_flush(pager, i);
      free(pager->pages[i]);
      pager->pages[i] = NULL;
    }
  }
  int result = close(pager->file_descriptor);
  if (result == -1) {
    printf("close file error: %s!\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  FORLESS(TABLE_MAX_PAGES) {
    if (pager->pages[i] != NULL) {
      pager->pages[i] = NULL;
    }
  }
  del_table(table);
}

MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
  // 模拟退出时保存数据
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    db_close(table);
    exit(EXIT_SUCCESS);
  } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
    printf("Tree:\n");
    print_tree(table->pager, 0, 0);
    return META_COMMAND_SUCCESS;
  } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
    printf("Constants:\n");
    print_constants();
    return META_COMMAND_SUCCESS;
  }
  return META_COMMAND_UNRECOGNIZED;
}

Pager* pager_open(const char* filename) {
  int fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    printf("open file: %s error: %s.\n", filename, strerror(errno));
    exit(EXIT_FAILURE);
  }
  // 使用lseek 移到SEEK_END知道文件大小
  size_t read_bytes = lseek(fd, 0, SEEK_END);
  if (read_bytes == -1) {
    printf("seek file: %s error: %s.\n", filename, strerror(errno));
    exit(EXIT_FAILURE);
  }
  Pager* pager = malloc(sizeof(Pager));
  pager->file_descriptor = fd;
  pager->file_length = read_bytes;
  pager->num_pages = (read_bytes / PAGE_SIZE);
  if (read_bytes % PAGE_SIZE != 0) {
    printf("Db file is not a whole number of pages. Corrupt file.\n");
    exit(EXIT_FAILURE);
  }
  FORLESS(TABLE_MAX_PAGES) { pager->pages[i] = NULL; }
  return pager;
}
Table* db_open(const char* filename) {
  Pager* pager = pager_open(filename);
  int num_rows = pager->file_length / ROW_SIZE;

  Table* table = malloc(sizeof(Table));
  table->pager = pager;
  table->root_page_num = 0;

  if (pager->num_pages == 0) {
    void* root_node = get_page(pager, 0);
    initialize_leaf_node(root_node);
    set_node_root(root_node, true);
  }
  return table;
}