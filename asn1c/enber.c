/*-
 * Copyright (c) 2004 Lev Walkin <vlm@lionet.info>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sysexits.h>		/* for EX_USAGE */
#include <assert.h>
#include <errno.h>

#ifdef	HAVE_CONFIG_H
#include "config.h"
#endif

#include <asn1parser.h>		/* For static string tables */

#include <asn_application.h>
#include <constraints.c>
#include <ber_tlv_tag.c>
#include <ber_tlv_length.c>

static void usage(const char *av0);	/* Print the Usage screen and exit */
static int process(const char *fname);	/* Perform the BER decoding */
static int process_line(const char *fname, char *line, int lineno);

#undef	COPYRIGHT
#define	COPYRIGHT	\
	"Copyright (c) 2004 Lev Walkin <vlm@lionet.info>\n"

int
main(int ac, char **av) {
	int ch;				/* Command line character */
	int i;				/* Index in some loops */

	/*
	 * Process command-line options.
	 */
	while((ch = getopt(ac, av, "hv")) != -1)
	switch(ch) {
	case 'v':
		fprintf(stderr, "ASN.1 BER Decoder, v" VERSION "\n" COPYRIGHT);
		exit(0);
		break;
	case 'h':
	default:
		usage(av[0]);
	}

	/*
	 * Ensure that there are some input files present.
	 */
	if(ac > optind) {
		ac -= optind;
		av += optind;
	} else {
		fprintf(stderr, "%s: No input files specified\n", av[0]);
		exit(1);
	}

	setvbuf(stdout, 0, _IOLBF, 0);

	/*
	 * Iterate over input files and parse each.
	 * All syntax trees from all files will be bundled together.
	 */
	for(i = 0; i < ac; i++) {
		if(process(av[i]))
			exit(EX_DATAERR);
	}

	return 0;
}

/*
 * Print the usage screen and exit(EX_USAGE).
 */
static void
usage(const char *av0) {
	fprintf(stderr,
"Convertor of under(1) output back into BER, v" VERSION "\n" COPYRIGHT
"Usage: %s [-] [file ...]\n"
	, av0);
	exit(EX_USAGE);
}

/*
 * Open the file and initiate recursive processing.
 */
static int
process(const char *fname) {
	char buf[8192];
	char *collector = 0;
	size_t collector_size = sizeof(buf);
	size_t collector_offset = 0;
	int lineno = 0;
	FILE *fp;

	if(strcmp(fname, "-")) {
		fp = fopen(fname, "r");
		if(!fp) {
			perror(fname);
			return -1;
		}
	} else {
		fp = stdin;
	}


	while(fgets(buf, sizeof(buf), fp) || !feof(fp)) {
		size_t len = strlen(buf);

		if(!len) continue;
		if(collector_offset || buf[len-1] != '\n') {
			if((collector_size - collector_offset) <= len
			|| !collector) {
				collector_size <<= 1;
				collector = realloc(collector, collector_size);
				if(!collector) {
					perror("realloc()");
					exit(EX_OSERR);
				}
			}
			memcpy(collector + collector_offset, buf, len + 1);
			collector_offset += len;
		}
		if(buf[len-1] != '\n') continue;

		if(collector_offset) {
			assert(collector[collector_offset-1] == '\n');
			process_line(fname, collector, ++lineno);
			collector_offset = 0;
		} else {
			process_line(fname, buf, ++lineno);
		}
	}

	if(fp != stdin)
		fclose(fp);

	return 0;
}

static int
process_line(const char *fname, char *line, int lineno) {
	char buf[32];
	char *op;	/* '<' */
	char *cl;	/* '>' */
	char *tcl_pos;	/* tag class position */
	char *tl_pos;
	char *v_pos;
	int constr;
	ber_tlv_tag_t tag_value;
	ber_tlv_tag_t tag_class;
	ber_tlv_tag_t tlv_tag;
	ber_tlv_len_t tlv_len;
	ber_tlv_len_t tl_len;
	ssize_t ret;
	(void)fname;

	/* Find a tag opening angle bracket */
	for(; *line == ' '; line++);
	op = line;
	if(*op != '<') {
		fprintf(stderr, "%s: Missing '<' after whitespace\n", fname);
		exit(EX_DATAERR);
	}

	/* Find a tag closing angle bracket */
	for(; *line && *line != '>'; line++) {
		if(*line < ' ') {
			fprintf(stderr, "%s: Invalid charset (%d)\n",
				fname, *(const unsigned char *)line);
			exit(EX_DATAERR);
		}
	}
	cl = line;
	if(*cl != '>') {
		fprintf(stderr, "%s: Missing '>'\n", fname);
		exit(EX_DATAERR);
	}

	/* Ignore closing tags */
	if(op[1] == '/') {
		if(strchr(cl, '<')) {	/* We are not very robust */
			fprintf(stderr, "%s: Multiple tags per line\n", fname);
			exit(EX_DATAERR);
		}
		/* End-of-content octets */
		if(op[2] == 'I') {
			buf[0] = buf[1] = 0x00;
			fwrite(buf, 1, 2, stdout);
		}
		return 0;
	}

	switch(op[1]) {
	case 'C': constr = 1; break;
	case 'P': constr = 0; break;
	case 'I': constr = 2; break;
	default:
		fprintf(stderr,
			"%s: Expected \"C\"/\"P\"/\"I\" as the XML tag name (%c)\n",
				fname, op[1]);
		exit(EX_DATAERR);
	}

	*cl = '\0';
	if(cl[-1] == 'F') {
		fprintf(stderr, "%s: Uses pretty-printing of values. "
			"Use -p option to unber\n", fname);
		exit(EX_DATAERR);
	}

	tcl_pos = strstr(op, "T=\"[");
	tl_pos = strstr(op, "TL=\"");
	v_pos = strstr(op, "V=\"");
	if(!tcl_pos || !tl_pos || !v_pos) {
		fprintf(stderr,
			"%s: Mandatory attribute %s is not found at line %d\n",
			fname, (!tcl_pos)?"T":(v_pos?"V":"TCL"), lineno);
		exit(EX_DATAERR);
	}
	errno = 0;
	tl_len = strtoul(tl_pos + 4, 0, 10);
	if(constr == 2) {
		tlv_len = 0;
	} else {
		tlv_len = strtoul(v_pos + 3, 0, 10);
	}
	if(errno || tl_len < 2 || tlv_len < 0) {
		fprintf(stderr, "%s: Invalid TL or V value at line %d\n",
			fname, lineno);
		exit(EX_DATAERR);
	}

	tcl_pos += 4;
	switch(*tcl_pos) {
	case 'U':	/* UNIVERSAL */
		tag_class = ASN_TAG_CLASS_UNIVERSAL; break;
	case 'P':	/* PRIVATE */
		tag_class = ASN_TAG_CLASS_PRIVATE; break;
	case 'A':	/* APPLICATION */
		tag_class = ASN_TAG_CLASS_APPLICATION; break;
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':	/* context */
		tag_class = ASN_TAG_CLASS_CONTEXT; break;
	default:
		fprintf(stderr, "%s: Invalid tag class (%c) at line %d\n",
			fname, tcl_pos[4], lineno);
		exit(EX_DATAERR);
	}
	for(;; tcl_pos++) {
		switch(*tcl_pos) {
		case '"': tcl_pos = "";
		case '\0':
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			break;
		default: continue;
		}
		break;
	}
	errno = 0;
	if(!*tcl_pos
	|| ((long)(tag_value = strtoul(tcl_pos, 0, 10))) < 0
	|| errno) {
		fprintf(stderr, "%s: Invalid tag value (%c) at line %d\n",
			fname, *tcl_pos, lineno);
		exit(EX_DATAERR);
	}
	tlv_tag = ((tag_value << 2) | tag_class);

	if(0) {
		printf("[%s>]\n", op);
		printf(" <%c T=\"%s\" TL=\"%d\" V=\"%d\">\n",
			constr?'C':'P',
			ber_tlv_tag_string(tlv_tag),
			tl_len, tlv_len);
	}

	ret = ber_tlv_tag_serialize(tlv_tag, buf, sizeof(buf));
	assert(ret >= 1 && (size_t)ret < sizeof(buf));
	if(constr == 2) {
		buf[ret] = 0x80;
		ret += 1;
	} else {
		ret += der_tlv_length_serialize(tlv_len,
			buf + ret, sizeof(buf) - ret);
		assert(ret >= 2 && (size_t)ret < sizeof(buf));
	}
	if(ret != tl_len) {
		fprintf(stderr, "%s: Cannot encode TL at line %d "
			"in the given number of bytes (%d!=%d)\n",
			fname, lineno, ret, tl_len);
		exit(EX_DATAERR);
	}
	if(constr) *buf |= 0x20;	/* Enable "constructed" bit */
	fwrite(buf, 1, tl_len, stdout);

	if(!constr) {
	  ber_tlv_len_t len;
	  for(len = 0, cl++; *cl && *cl != '<'; cl++, len++) {
		unsigned char v;
		int h;
		if(*cl != '&') {
			fputc(*cl, stdout);
			continue;
		}
		cl++;
		if(*cl != 'x') {
			fprintf(stderr, "%s: Expected \"&xNN;\" at line %d\n",
				fname, lineno);
			exit(EX_DATAERR);
		}
		for(v = 0, h = 0; h < 2; h++) {
			unsigned char clv = *++cl;
			v <<= 4;
			switch(clv) {
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				v |= clv - '0'; break;
			case 'A': case 'B': case 'C':
			case 'D': case 'E': case 'F':
				v |= clv - 'A' + 10; break;
			case 'a': case 'b': case 'c':
			case 'd': case 'e': case 'f':
				v |= clv - 'a' + 10; break;
			default:
				fprintf(stderr,
					"%s: Expected \"&xNN;\" at line %d (%c)\n",
					fname, lineno, clv);
				exit(EX_DATAERR);
			}
		}
		cl++;
		if(*cl != ';') {
			fprintf(stderr,
				"%s: Expected \"&xNN;\" at line %d\n",
				fname, lineno);
			exit(EX_DATAERR);
		}
		fputc(v, stdout);
	  }
	  if(len != tlv_len) {
		fprintf(stderr,
			"%s: Could not encode value of %d chars "
			"at line %d in %d bytes\n",
			fname, len, lineno, tlv_len);
		exit(EX_DATAERR);
	  }
	}
	
	return 0;
}

