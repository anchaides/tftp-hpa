/*	$OpenBSD: main.c,v 1.4 1997/01/17 07:13:30 millert Exp $	*/
/*	$NetBSD: main.c,v 1.6 1995/05/21 16:54:10 mycroft Exp $	*/

/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "tftpsubs.h"

#ifndef lint
static const char *copyright UNUSED =
"@(#) Copyright (c) 1983, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
/* static char sccsid[] = "@(#)main.c	8.1 (Berkeley) 6/6/93"; */
/* static char rcsid[] = "$OpenBSD: main.c,v 1.4 1997/01/17 07:13:30 millert Exp $"; */
static const char *rcsid UNUSED =
 "tftp-hpa $Id$";
#endif /* not lint */

/* Many bug fixes are from Jim Guyton <guyton@rand-unix> */

/*
 * TFTP User Program -- Command Interface.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/file.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef WITH_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "extern.h"

#define	TIMEOUT		5		/* secs between rexmt's */
#define	LBUFLEN		200		/* size of input buffer */

struct	sockaddr_in peeraddr;
int	f;
short   port;
int	trace;
int	verbose;
int	connected;
char	mode[32];
#ifdef WITH_READLINE
char   *line = NULL;
#else
char	line[LBUFLEN];
#endif
int	margc;
char	*margv[20];
const char *prompt = "tftp> ";
sigjmp_buf	toplevel;
void	intr(int);
struct	servent *sp;

void	get (int, char **);
void	help (int, char **);
void	modecmd (int, char **);
void	put (int, char **);
void	quit (int, char **);
void	setascii (int, char **);
void	setbinary (int, char **);
void	setpeer (int, char **);
void	setrexmt (int, char **);
void	settimeout (int, char **);
void	settrace (int, char **);
void	setverbose (int, char **);
void	status (int, char **);

static void command (void);

static void getusage (char *);
static void makeargv (void);
static void putusage (char *);
static void settftpmode (const char *);

#define HELPINDENT (sizeof("connect"))

struct cmd {
	const char *name;
	const char *help;
	void	(*handler) (int, char **);
};

struct cmd cmdtab[] = {
  { "connect",
    "connect to remote tftp",
    setpeer },
  { "mode",
    "set file transfer mode",
    modecmd },
  { "put",
    "send file",
    put },
  { "get",
    "receive file",
    get },
  { "quit",
    "exit tftp",
    quit },
  { "verbose",
    "toggle verbose mode",
    setverbose },
  { "trace",
    "toggle packet tracing",
    settrace },
  { "status",
    "show current status",
    status },
  { "binary",
    "set mode to octet",
    setbinary },
  { "ascii",
    "set mode to netascii",
    setascii },
  { "rexmt",
    "set per-packet transmission timeout",
    setrexmt },
  { "timeout",
    "set total retransmission timeout",
    settimeout },
  { "?",
    "print help information",
    help },
  { "help",
    "print help information",
    help },
  { 0, 0, 0 }
};

struct	cmd *getcmd(char *);
char	*tail(char *);

char *xstrdup(const char *);

int
main(int argc, char *argv[])
{
	struct sockaddr_in s_in;

	sp = getservbyname("tftp", "udp");
	if (sp == 0) {
		fprintf(stderr, "tftp: udp/tftp: unknown service\n");
		exit(1);
	}
	f = socket(AF_INET, SOCK_DGRAM, 0);
	if (f < 0) {
		perror("tftp: socket");
		exit(3);
	}
	bzero((char *)&s_in, sizeof (s_in));
	s_in.sin_family = AF_INET;
	if (bind(f, (struct sockaddr *)&s_in, sizeof (s_in)) < 0) {
		perror("tftp: bind");
		exit(1);
	}
	strcpy(mode, "netascii");
	bsd_signal(SIGINT, intr);
	if (argc > 1) {
		if (sigsetjmp(toplevel,1) != 0)
			exit(0);
		setpeer(argc, argv);
	}
	if (sigsetjmp(toplevel,1) != 0)
		(void)putchar('\n');

#ifdef WITH_READLINE
	using_history();
#endif

	command();

	return 0;		/* Never reached */
}

char    *hostname;

void
setpeer(int argc, char *argv[])
{
	struct hostent *host;

	if (argc < 2) {
		strcpy(line, "Connect ");
		printf("(to) ");
		fgets(&line[strlen(line)], LBUFLEN-strlen(line), stdin);
		makeargv();
		argc = margc;
		argv = margv;
	}
	if ((argc < 2) || (argc > 3)) {
		printf("usage: %s host-name [port]\n", argv[0]);
		return;
	}
	if (inet_aton(argv[1], &peeraddr.sin_addr) != 0) {
		peeraddr.sin_family = AF_INET;
		hostname = xstrdup(argv[1]);
	} else {
		host = gethostbyname(argv[1]);
		if (host == 0) {
			connected = 0;
			printf("%s: unknown host\n", argv[1]);
			return;
		}
		peeraddr.sin_family = host->h_addrtype;
		bcopy(host->h_addr, &peeraddr.sin_addr, host->h_length);
		hostname = xstrdup(host->h_name);
	}
	port = sp->s_port;
	if (argc == 3) {
		port = atoi(argv[2]);
		if (port < 0) {
			printf("%s: bad port number\n", argv[2]);
			connected = 0;
			return;
		}
		port = htons(port);
	}
	connected = 1;
}

struct	modes {
  const char *m_name;
  const char *m_mode;
} modes[] = {
	{ "ascii",	"netascii" },
	{ "netascii",   "netascii" },
	{ "binary",     "octet" },
	{ "image",      "octet" },
	{ "octet",     "octet" },
/*      { "mail",       "mail" },       */
	{ 0,		0 }
};

void
modecmd(int argc, char *argv[])
{
	struct modes *p;
	const char *sep;

	if (argc < 2) {
		printf("Using %s mode to transfer files.\n", mode);
		return;
	}
	if (argc == 2) {
		for (p = modes; p->m_name; p++)
			if (strcmp(argv[1], p->m_name) == 0)
				break;
		if (p->m_name) {
			settftpmode(p->m_mode);
			return;
		}
		printf("%s: unknown mode\n", argv[1]);
		/* drop through and print usage message */
	}

	printf("usage: %s [", argv[0]);
	sep = " ";
	for (p = modes; p->m_name; p++) {
		printf("%s%s", sep, p->m_name);
		if (*sep == ' ')
			sep = " | ";
	}
	printf(" ]\n");
	return;
}

void
setbinary(int argc, char *argv[])
{      
	(void)argc; (void)argv;	/* Quiet unused warning */
	settftpmode("octet");
}

void
setascii(int argc, char *argv[])
{
	(void)argc; (void)argv;	/* Quiet unused warning */
	settftpmode("netascii");
}

static void
settftpmode(const char *newmode)
{
	strcpy(mode, newmode);
	if (verbose)
		printf("mode set to %s\n", mode);
}


/*
 * Send file(s).
 */
void
put(int argc, char *argv[])
{
	int fd;
	int n;
	char *cp, *targ;

	if (argc < 2) {
		strcpy(line, "send ");
		printf("(file) ");
		fgets(&line[strlen(line)], LBUFLEN-strlen(line), stdin);
		makeargv();
		argc = margc;
		argv = margv;
	}
	if (argc < 2) {
		putusage(argv[0]);
		return;
	}
	targ = argv[argc - 1];
	if (strchr(argv[argc - 1], ':')) {
		struct hostent *hp;

		for (n = 1; n < argc - 1; n++)
			if (strchr(argv[n], ':')) {
				putusage(argv[0]);
				return;
			}
		cp = argv[argc - 1];
		targ = strchr(cp, ':');
		*targ++ = 0;
		hp = gethostbyname(cp);
		if (hp == NULL) {
			fprintf(stderr, "tftp: %s: ", cp);
			herror((char *)NULL);
			return;
		}
		bcopy(hp->h_addr, (caddr_t)&peeraddr.sin_addr, hp->h_length);
		peeraddr.sin_family = hp->h_addrtype;
		connected = 1;
		hostname = xstrdup(hp->h_name);
	}
	if (!connected) {
		printf("No target machine specified.\n");
		return;
	}
	if (argc < 4) {
		cp = argc == 2 ? tail(targ) : argv[1];
		fd = open(cp, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "tftp: "); perror(cp);
			return;
		}
		if (verbose)
			printf("putting %s to %s:%s [%s]\n",
				cp, hostname, targ, mode);
		peeraddr.sin_port = port;
		tftp_sendfile(fd, targ, mode);
		return;
	}
				/* this assumes the target is a directory */
				/* on a remote unix system.  hmmmm.  */
	cp = strchr(targ, '\0'); 
	*cp++ = '/';
	for (n = 1; n < argc - 1; n++) {
		strcpy(cp, tail(argv[n]));
		fd = open(argv[n], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "tftp: "); perror(argv[n]);
			continue;
		}
		if (verbose)
			printf("putting %s to %s:%s [%s]\n",
				argv[n], hostname, targ, mode);
		peeraddr.sin_port = port;
		tftp_sendfile(fd, targ, mode);
	}
}

static void
putusage(char *s)
{
	printf("usage: %s file ... host:target, or\n", s);
	printf("       %s file ... target (when already connected)\n", s);
}

/*
 * Receive file(s).
 */
void
get(int argc, char *argv[])
{
	int fd;
	int n;
	char *cp;
	char *src;

	if (argc < 2) {
		strcpy(line, "get ");
		printf("(files) ");
		fgets(&line[strlen(line)], LBUFLEN-strlen(line), stdin);
		makeargv();
		argc = margc;
		argv = margv;
	}
	if (argc < 2) {
		getusage(argv[0]);
		return;
	}
	if (!connected) {
		for (n = 1; n < argc ; n++)
			if (strchr(argv[n], ':') == 0) {
				getusage(argv[0]);
				return;
			}
	}
	for (n = 1; n < argc ; n++) {
		src = strchr(argv[n], ':');
		if (src == NULL)
			src = argv[n];
		else {
			struct hostent *hp;

			*src++ = 0;
			hp = gethostbyname(argv[n]);
			if (hp == NULL) {
				fprintf(stderr, "tftp: %s: ", argv[n]);
				herror((char *)NULL);
				continue;
			}
			bcopy(hp->h_addr, (caddr_t)&peeraddr.sin_addr,
			    hp->h_length);
			peeraddr.sin_family = hp->h_addrtype;
			connected = 1;
			hostname = xstrdup(hp->h_name);
		}
		if (argc < 4) {
			cp = argc == 3 ? argv[2] : tail(src);
			fd = creat(cp, 0644);
			if (fd < 0) {
				fprintf(stderr, "tftp: "); perror(cp);
				return;
			}
			if (verbose)
				printf("getting from %s:%s to %s [%s]\n",
					hostname, src, cp, mode);
			peeraddr.sin_port = port;
			tftp_recvfile(fd, src, mode);
			break;
		}
		cp = tail(src);         /* new .. jdg */
		fd = creat(cp, 0644);
		if (fd < 0) {
			fprintf(stderr, "tftp: "); perror(cp);
			continue;
		}
		if (verbose)
			printf("getting from %s:%s to %s [%s]\n",
				hostname, src, cp, mode);
		peeraddr.sin_port = port;
		tftp_recvfile(fd, src, mode);
	}
}

static void
getusage(char *s)
{
	printf("usage: %s host:file host:file ... file, or\n", s);
	printf("       %s file file ... file if connected\n", s);
}

int	rexmtval = TIMEOUT;

void
setrexmt(int argc, char *argv[])
{
	int t;

	if (argc < 2) {
		strcpy(line, "Rexmt-timeout ");
		printf("(value) ");
		fgets(&line[strlen(line)], LBUFLEN-strlen(line), stdin);
		makeargv();
		argc = margc;
		argv = margv;
	}
	if (argc != 2) {
		printf("usage: %s value\n", argv[0]);
		return;
	}
	t = atoi(argv[1]);
	if (t < 0)
		printf("%s: bad value\n", argv[1]);
	else
		rexmtval = t;
}

int	maxtimeout = 5 * TIMEOUT;

void
settimeout(int argc, char *argv[])
{
	int t;

	if (argc < 2) {
		strcpy(line, "Maximum-timeout ");
		printf("(value) ");
		fgets(&line[strlen(line)], LBUFLEN-strlen(line), stdin);
		makeargv();
		argc = margc;
		argv = margv;
	}
	if (argc != 2) {
		printf("usage: %s value\n", argv[0]);
		return;
	}
	t = atoi(argv[1]);
	if (t < 0)
		printf("%s: bad value\n", argv[1]);
	else
		maxtimeout = t;
}

void
status(int argc, char *argv[])
{
	(void)argc; (void)argv;	/* Quiet unused warning */
	if (connected)
		printf("Connected to %s.\n", hostname);
	else
		printf("Not connected.\n");
	printf("Mode: %s Verbose: %s Tracing: %s\n", mode,
		verbose ? "on" : "off", trace ? "on" : "off");
	printf("Rexmt-interval: %d seconds, Max-timeout: %d seconds\n",
		rexmtval, maxtimeout);
}

void
intr(int sig)
{
	(void)sig;	/* Quiet unused warning */

	bsd_signal(SIGALRM, SIG_IGN);
	alarm(0);
	siglongjmp(toplevel, -1);
}

char *
tail(char *filename)
{
	char *s;
	
	while (*filename) {
		s = strrchr(filename, '/');
		if (s == NULL)
			break;
		if (s[1])
			return (s + 1);
		*s = '\0';
	}
	return (filename);
}

/*
 * Command parser.
 */
static void
command(void)
{
	struct cmd *c;

	for (;;) {
#ifdef WITH_READLINE
	        if ( line )
		     free(line);
	        line = readline(prompt);
		if ( !line )
		     exit(0);	/* EOF */
#else
	        fputs(prompt, stdout);
		if (fgets(line, LBUFLEN, stdin) == 0) {
			if (feof(stdin)) {
				exit(0);
			} else {
				continue;
			}
		}
#endif
		if ((line[0] == 0) || (line[0] == '\n'))
			continue;
#ifdef WITH_READLINE
		add_history(line);
#endif
		makeargv();
		if (margc == 0)
			continue;

		c = getcmd(margv[0]);
		if (c == (struct cmd *)-1) {
			printf("?Ambiguous command\n");
			continue;
		}
		if (c == 0) {
			printf("?Invalid command\n");
			continue;
		}
		(*c->handler)(margc, margv);
	}
}

struct cmd *
getcmd(char *name)
{
	const char *p;
	char *q;
	struct cmd *c, *found;
	int nmatches, longest;

	longest = 0;
	nmatches = 0;
	found = 0;
	for (c = cmdtab; (p = c->name) != NULL; c++) {
		for (q = name; *q == *p++; q++)
			if (*q == 0)		/* exact match? */
				return (c);
		if (!*q) {			/* the name was a prefix */
			if (q - name > longest) {
				longest = q - name;
				nmatches = 1;
				found = c;
			} else if (q - name == longest)
				nmatches++;
		}
	}
	if (nmatches > 1)
		return ((struct cmd *)-1);
	return (found);
}

/*
 * Slice a string up into argc/argv.
 */
static void
makeargv(void)
{
	char *cp;
	char **argp = margv;

	margc = 0;
	for (cp = line; *cp;) {
		while (isspace(*cp))
			cp++;
		if (*cp == '\0')
			break;
		*argp++ = cp;
		margc += 1;
		while (*cp != '\0' && !isspace(*cp))
			cp++;
		if (*cp == '\0')
			break;
		*cp++ = '\0';
	}
	*argp++ = 0;
}

void
quit(int argc, char *argv[])
{
	(void)argc; (void)argv;	/* Quiet unused warning */
	exit(0);
}

/*
 * Help command.
 */
void
help(int argc, char *argv[])
{
	struct cmd *c;

	if (argc == 1) {
		printf("Commands may be abbreviated.  Commands are:\n\n");
		for (c = cmdtab; c->name; c++)
			printf("%-*s\t%s\n", (int)HELPINDENT, c->name, c->help);
		return;
	}
	while (--argc > 0) {
		char *arg;
		arg = *++argv;
		c = getcmd(arg);
		if (c == (struct cmd *)-1)
			printf("?Ambiguous help command %s\n", arg);
		else if (c == (struct cmd *)0)
			printf("?Invalid help command %s\n", arg);
		else
			printf("%s\n", c->help);
	}
}

void
settrace(int argc, char *argv[])
{
	(void)argc; (void)argv;	/* Quiet unused warning */

	trace = !trace;
	printf("Packet tracing %s.\n", trace ? "on" : "off");
}

void
setverbose(int argc, char *argv[])
{
	(void)argc; (void)argv;	/* Quiet unused warning */

	verbose = !verbose;
	printf("Verbose mode %s.\n", verbose ? "on" : "off");
}
