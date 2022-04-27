# Part 1

这章主要讲解准备阶段，以及提前做好B+ 树概念，为Part 2 做铺垫。

## 实现REPL、输入解析、内存数据表、写入磁盘

数据准备阶段，数据结构为:

```
// 13个row满，有余空间
|{id(4)+username(33)+email(256)}|{id(4)+username(33)+email(256)}|{id(4)+username(33)+email(256)}|...
// 剩下2个row
|{id(4)+username(33)+email(256)}|{id(4)+username(33)+email(256)}|
```

上面描述了15个row.

下方为要准备的基础数据，先保证有大致印象。

```c
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
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
typedef enum {
  STATEMENT_INSERT,
  STATEMENT_SELECT
} StatementType;

// 执行结果状态码
typedef enum {
  EXECUTE_SUCCESS,
  EXECUTE_DUPLICATE_KEY,
  EXECUTE_FULL_TABLE
} ExecuteResult;

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
  char username[COLUMN_USERNAME+1];
  char email[COLUMN_EMAIL+1];
} Row;

// Statement 对象为操作对象
typedef struct {
  StatementType type;
  Row row_to_insert;
} Statement;

// 便捷宏
#define FOELESS(less) for (int i = 0; i < less; i++)
// 查看属性大小
#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

// 以下描述字段大小，ROW_SIZE指真实数据大小
const uint32_t ID_OFFSET = 0;
const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE; // 4
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username); // 33
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE; // 37 = 4 + 33
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email); // 256
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE; // 293 = 37 + 256

// 缓存按整块读取大小4 kilobytes(极大多数系统架构的虚拟内存的page大小都为4kb)，如果每次都读整块，那读写效率是最大的
const uint32_t PAGE_SIZE = 4096;
const uint32_t TABLE_MAX_PAGES = 100; // 模拟有100页
const uint32_t ROW_PER_PAGES = PAGE_SIZE / ROW_SIZE; // 13 = 4096 / 293
const uint32_t TABLE_MAX_ROWS = TABLE_MAX_PAGES * ROW_PER_PAGES; // 1300

// 页面属性
typedef struct {
  void* pages[TABLE_MAX_PAGES];
  int file_descriptor;
  int file_length;
} Pager;

// Table属性
typedef struct {
  // 保存页面数据，方便上下文获取
  Pager* pager;
  // 记录当前已有的row数据量，读入、写入
  uint32_t row_nums;
} Table;
// 根据打开的文件，返回出Table上下文
Table* db_open(const char* filename);
void print_row(Row* row) {
  printf("(%d %s %s)\n", row->id, row->username, row->email);
}
// 读取用户输入
void read_line(InputBuffer* input_buffer) {
  ssize_t bytes_read = getline(&input_buffer->buffer, &input_buffer->buffer_length, stdin);
  if (bytes_read == -1) {
    printf("get user input error: %s.\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  input_buffer->buffer[strlen(input_buffer->buffer)-1] = '\0';
  input_buffer->str_length = strlen(input_buffer->buffer);
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
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement) {
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        return prepare_insert(input_buffer, statement);
    }
    if (strcmp(input_buffer->buffer, "select") == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}
```

```c
int main(int argc, char **argv)
{
  char *filename;
  if (argc > 1)
  {
    filename = argv[1];
  }
  Table *table = db_open(filename);
  while (true)
  {
    printf("> ");
    InputBuffer *input_buffer = new_input_buffer();
    read_line(input_buffer);

    // 对原字符进行识别是否有辅助指令
    if (input_buffer->buffer[0] == '.')
    {
      switch (do_meta_command(input_buffer, table))
      {
      case META_COMMAND_SUCCESS:
        continue;
      case META_COMMAND_UNRECOGNIZED:
        printf("unrecognize command: %s.\n", input_buffer->buffer);
        continue;
      }
    }
    Statement statement;
    switch (prepare_statement(input_buffer, &statement))
    {
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
    switch (execute_statement(&statement, table))
    {
    case EXECUTE_SUCCESS:
      printf("Executed.\n");
      break;
    case EXECUTE_FULL_TABLE:
      printf("Table insertion is full!");
      break;
    }
    del_input_buffer(input_buffer);
  }
  return 0;
}
```

第一个数据结构版本:

我们先从db_open 和 .exit 时将数据保存的动作入手，此时数据没有顺序，存入时整页保存4096 bytes，剩下的row就依次写入；取出时，read_bytes / 293 得到row 个数(有余数不精确，先不解决)。

```c
// open
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
  FORLESS(TABLE_MAX_PAGES) { pager->pages[i] = NULL; }
  return pager;
}
Table* db_open(const char* filename) {
  Pager* pager = pager_open(filename);
  int num_rows = pager->file_length / ROW_SIZE;

  Table* table = malloc(sizeof(Table));
  table->pager = pager;
  table->row_nums = num_rows;
  return table;
}
```

```c
// close & flush
void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
  if (pager->pages[page_num] == NULL) {
    printf("flush error by empty page at %d, size: %d .\n", page_num, size);
    exit(EXIT_FAILURE);
  }
  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
  if (offset == -1) {
    printf("flush seek page at %d, error: %s.\n", page_num, strerror(errno));
    exit(EXIT_FAILURE);
  }
  ssize_t write_bytes =
      write(pager->file_descriptor, pager->pages[page_num], size);
  if (write_bytes == -1) {
    printf("flush write page at %d, error: %s.\n", page_num, strerror(errno));
    exit(EXIT_FAILURE);
  }
}
void db_close(Table* table) {
  Pager* pager = table->pager;
  uint32_t full_num_rows = table->row_nums / ROW_SIZE;
  FORLESS(full_num_rows) {
    if (pager->pages[i] != NULL) {
      pager_flush(pager, i, PAGE_SIZE);
      free(pager->pages[i]);
      pager->pages[i] = NULL;
    }
  }
  uint32_t additional_num_rows = table->row_nums % ROW_SIZE;
  if (additional_num_rows > 0) {
    const page_num = full_num_rows;
    if (pager->pages[page_num] != NULL) {
      pager_flush(pager, page_num, additional_num_rows * ROW_SIZE);
      free(pager->pages[page_num]);
      pager->pages[page_num] = NULL;
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
```

接下来是将InputBuffer 转为statement。这块属于简单单一功能，识别文本前缀，直接看代码。

我们直接到execute_statement.

```c
// 选择功能
ExecuteResult execute_select(Statement* statement, Table* table) {
  Row row;
  // 简单处理，select时打印全部
  FORLESS(table->row_nums) {
    // 找到i在哪个page的offset 偏移内存点
    void* page = page_slot(table, i);
    // 将该内存信息赋到row中
    deserialize_row(&row, page);
    print_row(&row);
  }
  return EXECUTE_SUCCESS;
}
```

page_slot 的作用是为了获取row_index 在内存中的位置:

```c
void* page_slot(Table* table, uint32_t row_num) {
  Pager* pager = table->pager;
  // 首先找到page_num
  uint32_t page_num = row_num / ROW_PER_PAGES;
  // 根据page_num 找到页面的指针
  void* page = get_page(pager, page_num);
  // 查看row_num 的偏移量，根据偏移量算出内存偏移量
  uint32_t offset = row_num % ROW_PER_PAGES;
  ssize_t offset_bytes = offset * ROW_SIZE;
  return page + offset_bytes;
}
```

其中get_page 作用就是根据page_num 在pager->pages 中找到对应的内存数据，如果没有，那就创建一个；并且根据pager->file_length 对比是否大于查询的page_num, 超过则将文件内容复制给已申请的内存:

```c
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
  }
  return pager->pages[page_num];
}
```

select 最后, 将内存数据拷贝给对象数据:

```c
void deserialize_row(Row* target, void* source) {
  memcpy(&target->id, source + ID_OFFSET, ID_SIZE);
  memcpy(&target->username, source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&target->email, source + EMAIL_OFFSET, EMAIL_SIZE);
}
```

上面实现了select, 接下来insert。insert 根据table->row_nums，每次insert 之后，table->row_nums++:

```c
ExecuteResult execute_insert(Statement* statement, Table* table) {
  Row* row_to_insert = &statement->row_to_insert;
  void* page = page_slot(table, table->row_nums);
  serialize_row(page, row_to_insert);
  table->row_nums++;
  return EXECUTE_SUCCESS;
}
```

## 测试

insert 到内存，.exit 保存到文件；open 之后数据回来:

    $./part1 aa.db
    $> insert 1 1 1 // Executed.
    $> select // (1 1 1)
    $> .exit // 关闭
    $>./part1 aa.db
    $> select // (1 1 1)