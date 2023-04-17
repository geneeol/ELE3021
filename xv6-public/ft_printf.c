#include "ft_printf.h"

// static int	print_addr(void *ptr)
// {
// 	char				str[20];
// 	unsigned long long	addr;

// 	addr = (unsigned long long)ptr;
// 	if (write(1, "0x", 2) > 0)
// 	{
// 		pf_itoa_hex(addr, str, 'x');
// 		if (write(1, str, ft_strlen(str)) > 0)
// 			return (ft_strlen(str) + 2);
// 	}
// 	return (-1);
// }

int	ft_strlen(char *str)
{
	int	len;

	len = 0;
	while (str[len])
		len++;
	return (len);
}

static int	pf_conversion(const char *format, va_list ap)
{
	if (*format == 'c')
		return (print_char(va_arg(ap, int)));
	else if (*format == 's')
		return (print_str(va_arg(ap, char *)));
	// else if (*format == 'p')
	// 	return (print_addr(va_arg(ap, void *)));
	else if (*format == 'd' || *format == 'i')
		return (print_decimal(va_arg(ap, int)));
	else if (*format == 'u')
		return (print_udecimal(va_arg(ap, unsigned int)));
	else if (*format == 'x' || *format == 'X')
		return (print_hex(va_arg(ap, unsigned int), *format));
	else if (*format == '%')
		return (write(1, "%", 1));
	else
		return (-1);
}

int	ft_printf(const char *format, ...)
{
	int		ret;
	int		converted_len;
	va_list	ap;

	ret = 0;
	va_start(ap, format);
	while (*format)
	{
		if (*format == '%')
		{
			converted_len = pf_conversion(++format, ap);
			if (converted_len < 0)
				return (-1);
			ret += converted_len;
			format++;
		}
		else
		{
			if (write(1, format++, 1) < 0)
				return (-1);
			ret++;
		}
	}
	va_end(ap);
	return (ret);
}

/*************/

int	print_decimal(int n)
{
	char	str[15];

	pf_itoa(n, str);
	return (write(1, str, ft_strlen(str)));
}

int	print_udecimal(unsigned int n)
{
	char	str[15];

	pf_itoa_u(n, str);
	return (write(1, str, ft_strlen(str)));
}

int	print_hex(unsigned int n, char button)
{
	char	str[15];

	pf_itoa_hex((unsigned long long)n, str, button);
	return (write(1, str, ft_strlen(str)));
}

/*************/

#include "ft_printf.h"

int	print_char(char ch)
{
	return (write(1, &ch, 1));
}

int	print_str(char *str)
{
	if (!str)
		str = "(null)";
	return (write(1, str, ft_strlen(str)));
}

/*****************/

int	pf_len(int n)
{
	int	len;

	len = 1;
	while (n / 10)
	{
		len++;
		n /= 10;
	}
	return (len);
}

void	pf_itoa_hex(unsigned long long n, char str[], char specifier)
{
	unsigned long long	nb;
	int					i;

	nb = n;
	i = 1;
	while (n / 16)
	{
		i++;
		n /= 16;
	}
	str[i] = 0;
	while (--i >= 0)
	{
		if (specifier == 'x')
			str[i] = "0123456789abcdef"[nb % 16];
		else if (specifier == 'X')
			str[i] = "0123456789ABCDEF"[nb % 16];
		nb /= 16;
	}
}

void	pf_itoa(int n, char str[])
{
	int				i;
	unsigned int	nb;

	if (n < 0)
		nb = -n;
	else
		nb = n;
	i = pf_len(n) + (n < 0);
	str[i] = 0;
	while (--i >= (n < 0))
	{
		str[i] = "0123456789"[nb % 10];
		nb /= 10;
	}
	if (n < 0)
		str[0] = '-';
}

void	pf_itoa_u(unsigned int n, char str[])
{
	unsigned int	nb;
	int				i;

	nb = n;
	i = 1;
	while (n / 10)
	{
		i++;
		n /= 10;
	}
	str[i] = 0;
	while (--i >= 0)
	{
		str[i] = "0123456789"[nb % 10];
		nb /= 10;
	}
}