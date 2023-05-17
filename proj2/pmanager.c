#include "types.h"
#include "stat.h"
#include "user.h"

typedef int (*t_exec_cmd)(char *buf, int cmd_len);

enum cmd_type
{
	NONE = -1,
	LIST = 0,
	KILL,
	EXECUTE,
	MEMLIM,
	EXIT
};

int
mapcmd(char *cmd)
{
	const char *cmd_dict[] = {
		"list",
		"kill",
		"execute",
		"memlim",
		"exit"
	};
	int i;

	i = 0;
	while (i < 5) {
		if (strcmp(cmd, cmd_dict[i]) == 0)
			return (i);
		i++;
	}
	return (0);
}

// TODO: 추후 thread 정보를 고려하여 출력
// TODO: ps 시스템콜 구현
int
pm_list(char *buf, int cmd_len)
{
  if (buf[cmd_len] != 0)
    return (1);
  return (ps());
}

int
pm_kill(char *buf, int cmd_len)
{
  int pid;

  if (buf[cmd_len] != ' ')
    return (1);
  pid = atoi(buf + cmd_len + 1);
  kill(pid);
  return (0);

}

int
pm_execute(char *buf, int cmd_len)
{
  char path[128];
  int stacksize; 
  char *argv[2];
  int arg1_idx;
  int arg2_idx;
  int pid;
  int i;

  if (buf[cmd_len] != ' ')
    return (1);
  // buf[cmd_len]: 공백, 바로 다음 인덱스가 첫번째 인자 시작 인덱스
  arg1_idx = cmd_len + 1;
  i = 0;
  while (buf[arg1_idx + i] != 0 && buf[arg1_idx + i] != ' ')
  {
    path[i] = buf[arg1_idx + i];
    i++;
  }
  path[i] = 0;
  if (buf[arg1_idx + i] != ' ')
    return (1);
  arg2_idx = arg1_idx + i + 1;
  i = 0;
  while (buf[arg2_idx + i] != 0 && is_digit(buf[arg2_idx + i]))
    i++;
  if (buf[arg2_idx + i] != 0)
    return (1);
  // atoi는 '딱 숫자부분'만 정수로 변환함, 다른 문자가 사이에 껴 있으면 반복문 탈출
  stacksize = atoi(buf + arg2_idx);
  argv[0] = path;
  argv[1] = 0;
  pid = fork();
  if (pid == 0)
  {
    exec2(path, argv, stacksize);
    printf(2, "pmanager: Failed to exec %s\n", path);
    exit();
  }
  return (0);
}

int
pm_memlim(char *buf, int cmd_len)
{
  int pid;
  int limit;
  int arg1_idx;
  int arg2_idx;
  int i;

  if (buf[cmd_len] != ' ')
    return (1);
  arg1_idx = cmd_len + 1;
  i = 0;
  while (buf[arg1_idx + i] != 0 && buf[arg1_idx + i] != ' ')
    i++;
  if (buf[arg1_idx + i] != ' ')
    return (1);
  arg2_idx = arg1_idx + i + 1;
  i = 0;
  while (buf[arg2_idx + i] != 0 && is_digit(buf[arg2_idx + i]))
    i++;
  if (buf[arg2_idx + i] != 0)
    return (1);
  pid = atoi(buf + arg1_idx);
  limit = atoi(buf + arg2_idx);
  setmemorylimit(pid, limit);
  return (0);
}

int
pm_exit(char *buf, int cmd_len)
{
  if (buf[cmd_len] != 0)
    return (1);
  exit();
  return (0);
}

// 입력이 '형식'에 정확히 맞을 때만 명령어를 실행한다.
int
runcmd(char *buf)
{
  t_exec_cmd exec_cmd[] = {
    pm_list,
    pm_kill,
    pm_execute,
    pm_memlim,
    pm_exit
  };
	char cmd[128];
	int i;
	int cmd_type;

	i = 0;
	while (buf[i] != ' ' && buf[i] != '\0') {
		cmd[i] = buf[i];
		i++;
	}
  cmd[i] = '\0';
	cmd_type = mapcmd(cmd);
  // 개행만 입력된 경우 아무 메시지를 출력하지 않도록 함.
  // 대신 eof가 들어오는 경우 메시지 출력
  if (cmd_type == NONE)
    return (cmd[0] != '\n');
  return (exec_cmd[cmd_type](buf, i));
}

int
getcmd(char *buf, int nbuf)
{
  printf(2, "pmanager$ "); // TODO: 왜 표준에러로 출력하지? (원본코드)
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

int
main(void)
{
	char buf[256];

	printf(1, "pmanager is running\n");
	while(getcmd(buf, sizeof(buf)) >= 0)
  {
    if (runcmd(buf))
      printf(2, "pmanager: Failed to run command\n");
  }
  exit();
}