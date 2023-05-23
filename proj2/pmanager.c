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
is_digit(char ch)
{
  return ('0' <= ch && ch <= '9');
}

int
pm_list(char *buf, int cmd_len)
{
  if (buf[cmd_len] != '\n')
    return (1);
  return (plist());
}

int
pm_kill(char *buf, int cmd_len)
{
  int arg1_idx;
  int pid;
  int i;

  if (buf[cmd_len] != ' ')
    return (1);
  arg1_idx = cmd_len + 1;
  i = 0;
  while (buf[arg1_idx + i] != 0 && is_digit(buf[arg1_idx + i]))
    i++;
  if (buf[arg1_idx + i] != '\n')
    return (1);
  pid = atoi(buf + arg1_idx);
  if (kill(pid))
    printf(2, "pmanger: kill failed\n");
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
  if (buf[arg2_idx + i] != '\n')
    return (1);
  // atoi는 '딱 숫자부분'만 정수로 변환함, 다른 문자가 사이에 껴있으면 바로 앞 문자까지만 atoi로 변환
  stacksize = atoi(buf + arg2_idx);
  argv[0] = path;
  argv[1] = 0;
  // exec2로 실행된 프로세스 자원회수를 init 프로세스에 인계하기 위해 fork를 두번 사용
  pid = fork();
  if (pid == 0)
  {
    pid = fork();
    if (pid == 0)
    {
      exec2(path, argv, stacksize);
      printf(2, "pmanager: exec failed\n");
    }
    exit();
  }
  wait();
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
  if (buf[arg2_idx + i] != '\n')
    return (1);
  pid = atoi(buf + arg1_idx);
  limit = atoi(buf + arg2_idx);
  // TODO: pid가 없을 때 예외처리
  if (setmemorylimit(pid, limit))
    printf(2, "pmanager: memlim failed\n");
  return (0);
}

int
pm_exit(char *buf, int cmd_len)
{
  if (buf[cmd_len] != '\n')
    return (1);
  exit();
  return (0);
}

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
	while (i < 5)
  {
		if (strcmp(cmd, cmd_dict[i]) == 0)
			return (i);
		i++;
	}
	return (-1);
}

// 입력이 '형식'에 정확히 들어맞을 때만 명령어를 실행한다.
// 형식: '명령어 옵션1 옵션2개행'
// 명령어, 옵션 앞 뒤의 추가적인 공백이 존재하면 명령어는 실행되지 않는다.
int
runcmd(char *buf)
{
  // 함수 포인터 배열
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
	while (buf[i] != 0 && buf[i] != ' ' && buf[i] != '\n')
  {
		cmd[i] = buf[i];
		i++;
	}
  cmd[i] = '\0';
	cmd_type = mapcmd(cmd);
  // 개행만 입력된 경우 아무 메시지를 출력하지 않도록 함.
  // 대신 eof가 들어오는 경우 메시지 출력
  if (cmd_type == NONE)
    return (i != 0);
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
      printf(2, "pmanager: Usage error\n");
  }
  exit();
}