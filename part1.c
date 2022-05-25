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
const uint32_t TABLE_MAX_PAGES = 100;                // 模拟有100页
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
    // 查看是否需要从文件中读取，如果有的话
    uint32_t file_page_full_num = pager->file_length / PAGE_SIZE;
    if (pager->file_length % PAGE_SIZE) {
      file_page_full_num += 1;
    }
    // 说明文件中有数据，需要去获取对应的页面数据
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

void* row_slot(Table* table, uint32_t row_num) {
  Pager* pager = table->pager;
  uint32_t page_num = row_num / ROW_PER_PAGES;
  void* page = get_page(pager, page_num);
  uint32_t offset = row_num % ROW_PER_PAGES;
  ssize_t offset_bytes = offset * ROW_SIZE;
  return page + offset_bytes;
}

ExecuteResult execute_insert(Statement* statement, Table* table) {
  Row* row_to_insert = &statement->row_to_insert;
  void* page = row_slot(table, table->row_nums);
  serialize_row(page, row_to_insert);
  table->row_nums++;
  return EXECUTE_SUCCESS;
}
ExecuteResult execute_select(Statement* statement, Table* table) {
  Row row;
  // 简单处理，select时打印全部
  FORLESS(table->row_nums) {
    // 找到i在哪个page的offset 偏移内存点
    void* page = row_slot(table, i);
    deserialize_row(&row, page);
    print_row(&row);
  }
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

    // 对原字符进行识别是否是辅助指令
    if (input_buffer->buffer[0] == '.') {
      switch (do_meta_command(input_buffer, table)) {
      case META_COMMAND_SUCCESS:
        continue;
      case META_COMMAND_UNRECOGNIZED:
        printf("unrecognized command: %s.\n", input_buffer->buffer);
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
    case EXECUTE_FULL_TABLE:
      printf("Table insertion is full!");
      break;
    }
    del_input_buffer(input_buffer);
  }
  return 0;
}

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
  // 计算需要写入的整页面数
  uint32_t full_num_rows = table->row_nums / ROW_PER_PAGES;
  FORLESS(full_num_rows) {
    if (pager->pages[i] != NULL) {
      pager_flush(pager, i, PAGE_SIZE);
      free(pager->pages[i]);
      pager->pages[i] = NULL;
    }
  }
  // 多余的row数目也是要写入的，除余后将剩余在下一页补上
  uint32_t additional_num_rows = table->row_nums % ROW_PER_PAGES;
  if (additional_num_rows > 0) {
    // 下一页坐标，就是上面的full_num_rows
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
  // 将所有数据都释放掉
  FORLESS(TABLE_MAX_PAGES) {
    if (pager->pages[i] != NULL) {
      pager->pages[i] = NULL;
    }
  }
  del_table(table);
}

MetaCommandResult do_meta_command(InputBuffer* intput_buffer, Table* table) {
  // 模拟退出时保存数据
  if (strcmp(intput_buffer->buffer, ".exit") == 0) {
    db_close(table);
    exit(EXIT_SUCCESS);
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