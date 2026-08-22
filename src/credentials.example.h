// Скопировать этот файл в: credentials.h и заменить примеры кредов на свои.
struct Credential {
  const char* name;
  const char* login;
  const char* pass;
};

Credential credentials[] = {
  {"Login1", "your_login_1", "your_pass_1"},
  {"Login2", "your_login_2", "your_pass_2"},
  {"Login3", "your_login_3", "your_pass_3"},
  {"Login4", "your_login_4", "your_pass_4"},
  {"Login5", "your_login_5", "your_pass_5"},
  // ...
  // до 12 кредов вполне удобно читаются.
};

#define NUMITEMS(arg) ((unsigned int) (sizeof (arg) / sizeof (arg [0])))
