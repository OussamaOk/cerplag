#ifndef H_ERROR
#define H_ERROR

extern FILE* error_stream;
extern int error_count;
extern int warning_count;

enum {ERROR, WARNING};

void print_error(const char* category, int type, int err_num, int line, int col,
    const char* lexeme, const char* err_code, const char* err_msg);

#endif  // H_ERROR
