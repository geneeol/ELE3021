#ifndef FT_PRINTF_H
# define FT_PRINTF_H

#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"
#include "x86.h"
# include <stdarg.h>

// strlen return type: int or size_t?
int		ft_strlen(char *str);
int		ft_printf(const char *format, ...);
int		print_char(char ch);
int		print_str(char *str);
int		print_decimal(int n);
int		print_udecimal(unsigned int n);
int		print_hex(unsigned int n, char button);
void	pf_itoa(int n, char str[]);
void	pf_itoa_u(unsigned int n, char str[]);
void	pf_itoa_hex(unsigned long long n, char str[], char specifier);

#endif