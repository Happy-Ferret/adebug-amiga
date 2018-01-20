/*
 Copyright 1990-2006 Alexandre Lemaresquier, Raphael Lemoine
                     Laurent Chemla (Serial support), Daniel Verite (AmigaOS support)

 This file is part of Adebug.

 Adebug is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 Adebug is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Adebug; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/
/* ftoa.c */

#ifdef __HAVE_68881__
#define __M68881__ 1
#endif

#define NAN_HI 0x7fffffffL
#define NAN_LO 0xffffffffL
#define INF_HI 0x7ff00000L
#define INF_LO 0x00000000L

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <math.h>	/* mjr++ */

#ifndef __GNUC__	/* gcc lib has these typedefs in sys/types.h */
typedef unsigned char u_char;
typedef unsigned long u_long;
#endif

/* 11-bit exponent (VAX G floating point) is 308 decimal digits */
#define	MAXEXP		308

/* 128 bit fraction takes up 39 decimal digits; max reasonable precision */
# define	MAXFRACT	39
# define	MAXEXP		308

#define	DEFPREC		6
#define	BUF		(MAXEXP+MAXFRACT+1)	/* + decimal point */
#define	PUTC(ch)	 {*(printbuf++)=ch;}

#define ARG(signtag) \
	_ulong = \
		flags&LONGINT ? \
			(unsigned long)va_arg(argp, signtag long) : \
		(flags&SHORTINT ? \
			(unsigned long)(signtag short)va_arg(argp, signtag int) : \
		 	(unsigned long)va_arg(argp, signtag int))
	 /* shorts are always promoted to ints; thus, it's `va_arg(... int)'
	  * for `flags&SHORTINT'!
	  */

#define ISDIGIT(c)	(c>='0' && c<='9')
#define	ISASCII(c)	((unsigned)(c) <= 0177)
#define TEN_MUL(X)	((((X) << 2) + (X)) << 1)

#define	todigit(c)	((c) - '0')
#define	tochar(n)	((n) + '0')

#define	LONGINT		0x01		/* long integer */
#define	LONGLONG	0x02		/* long long integer (8 bytes) */
#define	LONGDBL		0x04		/* long double; unimplemented */
#define	SHORTINT	0x08		/* short integer */
#define	ALT		0x10		/* alternate form */
#define	LADJUST		0x20		/* left adjustment */
#define	ZEROPAD		0x40		/* zero (as opposed to blank) pad */
#define	HEXPREFIX	0x80		/* add 0x or 0X prefix */

/*#include	"flonum.h"*/
#if defined (__M68881__)
static char *exponent (char *, int, int);
static int _round (int, char *, char *, char *);
static int cvt (double, int, int, char *, int, char *);
#else
static char *exponent(char *, int, int);
static char *_round(double, int *, char *, char *, int, char *);
static int  cvt(double, int, int, char *, int, char *, char *);
#endif /* __M68881__ */

#if defined(__GNUC__) && (!defined(__NO_INLINE__))
#ifdef __M68020__

#define _ICONV(NUMBER, BASE, BUF)				\
{								\
  long i;							\
  do								\
    {								\
      __asm__ volatile						\
	("divull %3,%1:%0"					\
	 : "=d"((long)(NUMBER)), "=d"(i)			\
	 : "0"((long)(NUMBER)), "d"((long)(BASE)));		\
      *--(BUF) = digs[i];					\
    }								\
  while (NUMBER);						\
}

#else /* !__M68020 */  

#define _ICONV(NUMBER, BASE, BUF) 				\
{								\
								\
    while((NUMBER) > 65535L)					\
    {								\
        extern unsigned long __udivsi3(); /* quot = d0, rem = d1 */ \
        register long i __asm ("d1");				    \
	__asm__ volatile("
		movl	%3,sp@-;
 		movl	%2,sp@-;
 		jsr	___udivsi3;
 		addqw	#8,sp;
 		movl	d0,%0"					\
	         : "=r"((long)NUMBER), "=d"(i)			\
		 : "0"((long)NUMBER), "r"((long)BASE)		\
	         : "d0", "d1", "a0", "a1");			\
	    *--BUF = digs[i];					\
    }								\
    do 								\
    {								\
            short i;						\
	    __asm__ volatile("
 		divu	%3,%2;
 		swap	%0;
 		movw	%0,%1;
 		clrw	%0;
                swap    %0"					\
	         : "=d"((long)NUMBER), "=g"(i)			\
		 : "0"((long)NUMBER), "dm"((short)BASE));	\
	    *--BUF = digs[i];					\
    } while(NUMBER);						\
}

#endif /* __M68020 */

#else /* !__GNUC__ */

#define _ICONV(NUMBER, BASE, BUF) 				\
  do {								\
    *--(BUF) = digs[(NUMBER) % (BASE)];				\
    (NUMBER) /= (BASE);						\
  } while (NUMBER);

#endif /* __GNUC__ */

union double_di {
	u_long	i[2];
	double	d;
};

int dsprintf(char *printbuf,const char *fmt0, double argdouble)
{
	register const u_char *fmt;	/* format string */
	register int ch;	/* character from fmt */
	register int cnt;	/* return value accumulator */
	register int n;		/* random handy integer */
	register char *t;	/* buffer pointer */
/*	double _double;		*//* double precision arguments %[eEfgG] */
	union double_di _dd;	/* _double is #defined to be _dd later on */
	char softsign;		/* temporary negative sign for floats */
	typedef u_long _ulongtype;
	typedef long _ulongsignedtype;
	_ulongtype _ulong;		/* integer arguments %[diouxX] */
	short base;		/* base for [diouxX] conversion */
	short dprec;		/* decimal precision in [diouxX] */
	short fieldsz;		/* field size expanded by sign, etc */
	short flags;		/* flags as above */
	short fpprec;		/* `extra' floating precision in [eEfgG] */
	short prec;		/* precision from format (%.3d), or -1 */
	short realsz;		/* field size expanded by decimal precision */
	short size;		/* size of converted field or string */
	short width;		/* width from format (%8d), or 0 */
	char sign;		/* sign prefix (' ', '+', '-', or \0) */
	char *digs;		/* digits for [diouxX] conversion */
	char buf[BUF];		/* space for %c, %[diouxX], %[eEfgG] */

	fmt = (const u_char *) fmt0;
	digs = "0123456789abcdef";
	for (cnt = 0;; ++fmt) {
		if ((ch = *fmt) == 0) {
			PUTC('\0');
			return (cnt);
		}
		if (ch != '%') {
			PUTC(ch);
			cnt++;
			continue;
		}
		flags = 0; dprec = 0; fpprec = 0; width = 0;
		prec = -1;
		sign = '\0';

rflag:		switch (*++fmt) {
		case ' ':
			/*
			 * ``If the space and + flags both appear, the space
			 * flag will be ignored.''
			 *	-- ANSI X3J11
			 */
			if (!sign)
				sign = ' ';
			goto rflag;
		case '-':
			flags |= LADJUST;
			goto rflag;
		case '+':
			sign = '+';
			goto rflag;
		case '.':
			n = 0;
			while (isascii(*fmt) && isdigit(*fmt))
				n = TEN_MUL(n) + todigit(*fmt++);
			--fmt;
			prec = n < 0 ? -1 : n;
			goto rflag;
		case '0':
			/*
			 * ``Note that 0 is taken as a flag, not as the
			 * beginning of a field width.''
			 *	-- ANSI X3J11
			 */
			flags |= ZEROPAD;
			goto rflag;
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = 0;
			do {
				n = TEN_MUL(n) + todigit(*fmt);
			} while (ISASCII(*++fmt) && ISDIGIT(*fmt));
			width = n;
			--fmt;
			goto rflag;
		case 'L':
			flags |= LONGDBL;
			goto rflag;
		case 'l':
			flags |= LONGINT;
			goto rflag;
		case 'e':
		case 'E':
		case 'f':
		case 'g':
		case 'G':

/* mjr: check for NANs */
#define	_double _dd.d
			_double = argdouble;
			if (_dd.i[0] == NAN_HI)
			{
				t = buf;
				t = strcpy(t, "NaN");
				size = strlen(t);
				goto pforw;
			}
			/*
			 * don't do unrealistic precision; just pad it with
			 * zeroes later, so buffer size stays rational.
			 */
			if (prec > (MAXEXP - MAXFRACT)) {
				if ((*fmt != 'g' && *fmt != 'G') || (flags&ALT))
					fpprec = prec - MAXFRACT;
				prec = MAXFRACT;
			}
			else if (prec == -1)
				prec = DEFPREC;
			/*
			 * softsign avoids negative 0 if _double is < 0 and
			 * no significant digits will be shown
			 */
			if (_double < 0) {
				softsign = '-';
				_double = -_double;
			}
			else
				softsign = 0;
/* hyc: check for overflows ... */
			if ((((unsigned long)_dd.i[0] > INF_HI) &&
/* mj: check for a negative zero; it is not smaller than zero, so it
   was not negated */
			     (_dd.i[0] != 0x80000000L || _dd.i[1] != 0)) ||
			    ((unsigned long)_dd.i[0] == INF_HI &&
			     (unsigned long)_dd.i[1] > INF_LO))
			  {
			    t = buf;
			    if(softsign == 0)
				t = strcpy(t, "NaN");
			    else
				t = strcpy(t, "-NaN");
			    size = strlen(t);
			    goto pforw;
			}
			else	/* Not greater, see if equal to Inf */

/* mjr: check for +-INFINITY */
			if ((unsigned long)_dd.i[0] == INF_HI)
			  {
			    t = buf;
			    if(softsign == 0)
				t = strcpy(t, "+Inf");
			    else
				t = strcpy(t, "-Inf");
			    size = strlen(t);
			    goto pforw;
			}
			/*
			 * cvt may have to round up past the "start" of the
			 * buffer, i.e. ``printf("%.2f", (double)9.999);'';
			 * if the first char isn't '\0', it did.
			 */
			*buf = '\0';
			size = cvt(_double, (int)prec, (int)flags, &softsign,
				   *fmt, buf
#if !defined (__M68881__)
				   , buf + (int)sizeof(buf)
#endif
				   ); 
			if (softsign)
				sign = '-';
			t = *buf ? buf : buf + 1;
			/*goto pforw;*/

pforw:
			/*
			 * All reasonable formats wind up here.  At this point,
			 * `t' points to a string which (if not flags&LADJUST)
			 * should be padded out to `width' places.  If
			 * flags&ZEROPAD, it should first be prefixed by any
			 * sign or other prefix; otherwise, it should be blank
			 * padded before the prefix is emitted.  After any
			 * left-hand padding and prefixing, emit zeroes
			 * required by a decimal [diouxX] precision, then print
			 * the string proper, then emit zeroes required by any
			 * leftover floating precision; finally, if LADJUST,
			 * pad with blanks.
			 */

			/*
			 * compute actual size, so we know how much to pad
			 * fieldsz excludes decimal prec; realsz includes it
			 */
			fieldsz = size + fpprec;
			realsz = dprec > fieldsz ? dprec : fieldsz;
			if (sign)
				realsz++;
			if (flags & HEXPREFIX)
				realsz += 2;

			/* right-adjusting blank padding */
			if ((flags & (LADJUST|ZEROPAD)) == 0 && width)
				for (n = realsz; n < width; n++)
					PUTC(' ');
			/* prefix */
			if (sign)
				PUTC(sign);
			if (flags & HEXPREFIX) {
				PUTC('0');
				PUTC((char)*fmt);
			}
			/* right-adjusting zero padding */
			if ((flags & (LADJUST|ZEROPAD)) == ZEROPAD)
				for (n = realsz; n < width; n++)
					PUTC('0');
			/* leading zeroes from decimal precision */
			for (n = fieldsz; n < dprec; n++)
				PUTC('0');

			if (fpprec > 0)
			  {
			    /* the number without exponent */
			    n = size;
			    while (*t != 'e' && *t != 'E' && --n >= 0)
			      PUTC (*t++);
			  }
			else

			/* the string or number proper */
			for (n = size; --n >= 0; )
				PUTC(*t++);
			/* trailing f.p. zeroes */
			while (--fpprec >= 0)
				PUTC('0');
			/* exponent */
			while (--n >= 0)
			  PUTC (*t++);
			/* left-adjusting padding (always blank) */
			if (flags & LADJUST)
				for (n = realsz; n < width; n++)
					PUTC(' ');
			/* finally, adjust cnt */
			cnt += width > realsz ? width : realsz;
			break;
		case '\0':	/* "%?" prints ?, unless ? is NULL */
			PUTC('\0');
			return (cnt);
		default:
			PUTC((char)*fmt);
			cnt++;
		}
	}
	/* NOTREACHED */
}

#if defined (__M68881__)

/* Format of packed decimal (from left to right):

    1 Bit: sign of mantissa
    1 Bit: sign of exponent
    2 Bits zero
   12 Bits: three digits exponent
    4 Bits unused, fourth (higher order) digit of exponent
    8 Bits zero
   68 Bits: 17 digits of mantissa, decimal point after first digit
  --------
   96 Bits == 12 Bytes

   All numbers in BCD format.  */

static int
cvt (number, prec, flags, signp, fmtch, startp)
     double number;
     int prec, flags, fmtch;
     char *signp, *startp;
{
  char buf[12];
  char digits[18];
  int i;
  char *p, *t;
  int expcnt;
  int gformat = 0, dotrim;

  /* Convert to packed decimal.  */
  asm volatile ("fmovep %0,%1{#17}" : : "f" (number), "m" (buf[0]));
  /* Unpack it. */
  p = buf + 3;
  for (i = 0; i < 17; i++)
    digits[i] = ((i & 1 ? *p >> 4 : *p++) & 15) + '0';
  digits[i] = 0;
  expcnt = ((buf[0] & 15) * 10 + ((buf[1] >> 4) & 15)) * 10 + (buf[1] & 15);
  if (buf[0] & 0x40)
    expcnt = -expcnt;
  t = ++startp;
  p = digits;
  switch (fmtch)
    {
    case 'f':
      if (expcnt >= 0)
	{
	  *t++ = *p++;
	  while (expcnt > 0 && *p)
	    {
	      *t++ = *p++;
	      expcnt--;
	    }
	  while (expcnt > 0)
	    {
	      *t++ = '0';
	      expcnt--;
	    }
	}
      else
	{
	  /* Note: decimal point after the first digit. */
	  expcnt++;
	  *t++ = '0';
	}
      if (prec || flags & ALT)
	*t++ = '.';
      while (prec > 0 && expcnt < 0)
	{
	  *t++ = '0';
	  prec--;
	  expcnt++;
	}
      while (prec > 0 && *p)
	{
	  *t++ = *p++;
	  prec--;
	}
      if (*p)
	if (_round (*p, startp, t - 1, signp))
	  *--startp = '1';
      while (prec > 0)
	{
	  *t++ = '0';
	  prec--;
	}
      break;

    case 'e':
    case 'E':
    eformat:
      *t++ = *p++;
      if (prec || flags & ALT)
	*t++ = '.';
      while (prec > 0 && *p)
	{
	  *t++ = *p++;
	  prec--;
	}
      if (*p)
	if (_round (*p, startp, t - 1, signp))
	  {
	    expcnt++;
	    *startp = '1';
	  }
      if (!gformat || flags & ALT)
	{
	  while (prec > 0)
	    {
	      *t++ = '0';
	      prec--;
	    }
	}
      else
	{
	  /* Suppress trailing zero's. */
	  while (t > startp && t[-1] == '0')
	    t--;
	  if (t[-1] == '.')
	    t--;
	}
      t = exponent (t, expcnt, fmtch);
      break;

    case 'g':
    case 'G':
      if (prec == 0)
	prec++;
      /* If the exponent is not less than the precision or less than -4,
	 use 'e' format, otherwise use 'f' format.  */
      if (expcnt >= prec || expcnt < -4)
	{
	  /* Count the significant digit before the decimal point.  */
	  prec--;
	  /* 'g'/'G' -> 'e'/'E' */
	  fmtch -= 2;
	  gformat = 1;
	  goto eformat;
	}
      /* Like 'f', but prec counts significant digits.  */
      if (expcnt >= 0)
	{
	  *t++ = *p++;
	  prec--;
	  /* Note that prec >= expcnt */
	  while (expcnt > 0 && *p)
	    {
	      *t++ = *p++;
	      expcnt--;
	      prec--;
	    }
	  while (expcnt > 0)
	    {
	      *t++ = '0';
	      expcnt--;
	      prec--;
	    }
	}
      else
	{
	  *t++ = '0';
	  expcnt++;
	}
      if (prec > 0 || flags & ALT)
	{
	  dotrim = 1;
	  *t++ = '.';
	}
      else
	dotrim = 0;
      if (prec > 0)
	/* Pad with 0's */
	while (expcnt < 0)
	  {
	    *t++ = '0';
	    expcnt++;
	  }
      /* While more precision required and digits left */
      while (prec > 0 && *p)
	{
	  *t++ = *p++;
	  prec--;
	}
      if (*p)
	if (_round (*p, startp, t - 1, signp))
	  /* Overflow, e.g. 9.999 -> 10.000 */
	  *--startp = '1';
      if (flags & ALT)
	{
	  while (prec > 0)
	    {
	      *t++ = '0';
	      prec--;
	    }
	}
      else if (dotrim)
	{
	  /* Strip trailing 0's. */
	  while (t > startp && *--t == '0');
	  if (*t != '.')
	    t++;
	}
      break;
    }
  return (int) (t - startp);
}

static int
_round (nxtdig, start, end, signp)
     int nxtdig;
     char *start, *end;
     char *signp;
{
  if (nxtdig > '4')
    {
      for (;; --end)
	{
	  if (*end == '.')
	    --end;
	  if (++*end <= '9')
	    break;
	  *end = '0';
	  if (end == start)
	    /* Report overflow, caller must handle appropriately.  */
	    return 1;
	}
    }
  else if (*signp == '-')
    {
      for (;; --end)
	{
	  if (*end == '.')
	    --end;
	  if (*end != '0')
	    break;
	  if (end == start)
	    *signp = 0;
	}
    }
  return 0;
}

#else /* !(__M68881__) */

static int
cvt(number,prec,flags, signp, fmtch, startp, endp)
	double number;
	register int prec;
	int flags;
	int fmtch;
	char *signp, *startp, *endp;
{
	register char *p, *t;
	register double fract;
	int dotrim, expcnt, gformat;
	double integer, tmp, modf (double, double *);
	char *exponent (char *, int, int),
	     *_round (double, int *, char *, char *, int, char *);

	dotrim = expcnt = gformat = 0;
	fract = modf(number, &integer);

	/* get an extra slot for rounding. */
	t = ++startp;
/* jrb -- #define DBL_EPSILON 1.1107651257113995e-16 */ /* mjr ++ */
	/*
	 * get integer portion of number; put into the end of the buffer; the
	 * .01 is added for modf(356.0 / 10, &integer) returning .59999999...
	 */
	for (p = endp - 1; integer; ++expcnt) {
		tmp = modf(integer / 10, &integer);
		*p-- = tochar((int)((tmp + .01) * 10));
/* await michals advise on this	-- in the mean while use above line
	*p-- = tochar((int)((tmp + DBL_EPSILON) * 10));
 */
	}
	switch(fmtch) {
	case 'f':
		/* reverse integer into beginning of buffer */
		if (expcnt)
			for (; ++p < endp; *t++ = *p);
		else
			*t++ = '0';
		/*
		 * if precision required or alternate flag set, add in a
		 * decimal point.
		 */
		if (prec || flags&ALT)
			*t++ = '.';
		/* if requires more precision and some fraction left */
		if (fract) {
			if (prec)
				do {
					fract = modf(fract * 10, &tmp);
					*t++ = tochar((int)tmp);
				} while (--prec && fract);
			if (fract)
				startp = _round(fract, (int *)NULL, startp,
				    t - 1, (char)0, signp);
		}
		for (; prec--; *t++ = '0');
		break;
	case 'e':
	case 'E':
eformat:	if (expcnt) {
			*t++ = *++p;
			if (prec || flags&ALT)
				*t++ = '.';
			/* if requires more precision and some integer left */
			for (; prec && ++p < endp; --prec)
				*t++ = *p;
			/*
			 * if done precision and more of the integer component,
			 * round using it; adjust fract so we don't re-round
			 * later.
			 */
			if (!prec && ++p < endp) {
				fract = 0;
				startp = _round((double)0, &expcnt, startp,
				    t - 1, *p, signp);
			}
			/* adjust expcnt for digit in front of decimal */
			--expcnt;
		}
		/* until first fractional digit, decrement exponent */
		else if (fract) {
			/* adjust expcnt for digit in front of decimal */
			for (expcnt = -1;; --expcnt) {
				fract = modf(fract * 10, &tmp);
				if (tmp)
					break;
			}
			*t++ = tochar((int)tmp);
			if (prec || flags&ALT)
				*t++ = '.';
		}
		else {
			*t++ = '0';
			if (prec || flags&ALT)
				*t++ = '.';
		}
		/* if requires more precision and some fraction left */
		if (fract) {
			if (prec)
				do {
					fract = modf(fract * 10, &tmp);
					*t++ = tochar((int)tmp);
				} while (--prec && fract);
			if (fract)
				startp = _round(fract, &expcnt, startp,
				    t - 1, (char)0, signp);
		}
		/* if requires more precision */
		for (; prec--; *t++ = '0');

		/* unless alternate flag, trim any g/G format trailing 0's */
		if (gformat && !(flags&ALT)) {
			while (t > startp && *--t == '0');
			if (*t == '.')
				--t;
			++t;
		}
		t = exponent(t, expcnt, fmtch);
		break;
	case 'g':
	case 'G':
		/* a precision of 0 is treated as a precision of 1. */
		if (!prec)
			++prec;
		/*
		 * ``The style used depends on the value converted; style e
		 * will be used only if the exponent resulting from the
		 * conversion is less than -4 or greater than the precision.''
		 *	-- ANSI X3J11
		 */
		if (expcnt > prec || (!expcnt && fract && fract < .0001)) {
			/*
			 * g/G format counts "significant digits, not digits of
			 * precision; for the e/E format, this just causes an
			 * off-by-one problem, i.e. g/G considers the digit
			 * before the decimal point significant and e/E doesn't
			 * count it as precision.
			 */
			--prec;
			fmtch -= 2;		/* G->E, g->e */
			gformat = 1;
			goto eformat;
		}
		/*
		 * reverse integer into beginning of buffer,
		 * note, decrement precision
		 */
		if (expcnt)
			for (; ++p < endp; *t++ = *p, --prec);
		else
			*t++ = '0';
		/*
		 * if precision required or alternate flag set, add in a
		 * decimal point.  If no digits yet, add in leading 0.
		 */
		if (prec || flags&ALT) {
			dotrim = 1;
			*t++ = '.';
		}
		else
			dotrim = 0;
		/* if requires more precision and some fraction left */
		if (fract) {
			if (prec) {
				if (0 == expcnt) {
				    /* if no significant digits yet */
					do {
						fract = modf(fract * 10, &tmp);
						*t++ = tochar((int)tmp);
					} while(!tmp);
					prec--;
				}
				while (prec && fract) {
					fract = modf(fract * 10, &tmp);
					*t++ = tochar((int)tmp);
					prec--;
				}
			}
			if (fract)
				startp = _round(fract, (int *)NULL, startp,
				    t - 1, (char)0, signp);
		}
		/* alternate format, adds 0's for precision, else trim 0's */
		if (flags&ALT)
			for (; prec--; *t++ = '0');
		else if (dotrim) {
			while (t > startp && *--t == '0');
			if (*t != '.')
				++t;
		}
	}
	return((int)(t - startp));
}

static char *
_round(fract, exp, start, end, ch, signp)
	double fract;
	int *exp;
	register char *start, *end;
	int ch;
	char *signp;
{
	double tmp;

	if (fract)
		(void)modf(fract * 10, &tmp);
	else
		tmp = todigit(ch);
	if (tmp > 4)
		for (;; --end) {
			if (*end == '.')
				--end;
			if (++*end <= '9')
				break;
			*end = '0';
			if (end == start) {
				if (exp) {	/* e/E; increment exponent */
					*end = '1';
					++*exp;
				}
				else {		/* f; add extra digit */
					*--end = '1';
					--start;
				}
				break;
			}
		}
	/* ``"%.3f", (double)-0.0004'' gives you a negative 0. */
	else if (*signp == '-')
		for (;; --end) {
			if (*end == '.')
				--end;
			if (*end != '0')
				break;
			if (end == start)
				*signp = 0;
		}
	return(start);
}

#endif /* !(__M68881__) */

static char *
exponent(p, exp, fmtch)
	register char *p;
	register int exp;
	int fmtch;
{
	register char *t;
	char expbuf[MAXEXP];

	*p++ = fmtch;
	if (exp < 0) {
		exp = -exp;
		*p++ = '-';
	}
	else
		*p++ = '+';
	t = expbuf + MAXEXP;
	if (exp > 9) {
		do {
			*--t = tochar(exp % 10);
		} while ((exp /= 10) > 9);
		*--t = tochar(exp);
		for (; t < expbuf + MAXEXP; *p++ = *t++);
	}
	else {
		*p++ = '0';
		*p++ = tochar(exp);
	}
	return(p);
}
