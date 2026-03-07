#ifndef _SHELL_H_
#define _SHELL_H_

void shell_init(void);
void shell_run(void);
const char* shell_get_cwd(void);
void shell_set_cwd(const char* path);

#endif // _SHELL_H_
