/*
 * zle_tricky.c - expansion and completion
 *
 * This file is part of zsh, the Z shell.
 *
 * Copyright (c) 1992-1997 Paul Falstad
 * All rights reserved.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and to distribute modified versions of this software for any
 * purpose, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * In no event shall Paul Falstad or the Zsh Development Group be liable
 * to any party for direct, indirect, special, incidental, or consequential
 * damages arising out of the use of this software and its documentation,
 * even if Paul Falstad and the Zsh Development Group have been advised of
 * the possibility of such damage.
 *
 * Paul Falstad and the Zsh Development Group specifically disclaim any
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose.  The software
 * provided hereunder is on an "as is" basis, and Paul Falstad and the
 * Zsh Development Group have no obligation to provide maintenance,
 * support, updates, enhancements, or modifications.
 *
 */

#include "zle.mdh"
#include "zle_tricky.pro"

/* The main part of ZLE maintains the line being edited as binary data, *
 * but here, where we interface with the lexer and other bits of zsh,   *
 * we need the line metafied.  The technique used is quite simple: on   *
 * entry to the expansion/completion system, we metafy the line in      *
 * place, adjusting ll and cs to match.  All completion and expansion   *
 * is done on the metafied line.  Immediately before returning, the     *
 * line is unmetafied again, changing ll and cs back.  (ll and cs might *
 * have changed during completion, so they can't be merely saved and    *
 * restored.)  The various indexes into the line that are used in this  *
 * file only are not translated: they remain indexes into the metafied  *
 * line.                                                                */


#define inststr(X) inststrlen((X),1,-1)

/* The line before completion was tried. */

static char *origline;
static int origcs, origll;

/* Words on the command line, for use in completion */
 
/**/
int clwsize, clwnum, clwpos;
/**/
char **clwords;

/* wb and we hold the beginning/end position of the word we are completing. */

/**/
int wb, we;

/* offs is the cursor position within the tokenized *
 * current word after removing nulargs.             */

/**/
int offs;

/* We store the following prefixes/suffixes:                               *
 * ipre,ripre  -- the ignored prefix (quoted and unquoted)                 *
 * isuf        -- the ignored suffix                                       *
 * autoq       -- quotes to automatically insert                           */

/**/
char *ipre, *ripre, *isuf, *qipre, *qisuf, autoq;

/* the last completion widget called */

static Widget lastcompwidget;

/* These control the type of completion that will be done.  They are      *
 * affected by the choice of ZLE command and by relevant shell options.   *
 * usemenu is set to 2 if we have to start automenu and 3 if we have to   *
 * insert a match as if for menucompletion but without really stating it. */

/**/
int usemenu, useglob, useexact, useline, uselist;

/* Non-zero if we should keep an old list. */

static int oldlist, oldins;

/* Non-zero if we have to redisplay the list of matches. */

static int showagain = 0;

/* The match and group number to insert when starting menucompletion.   */

static int insmnum, insgnum, insgroup, insspace;

/* This is used to decide when the cursor should be moved to the end of    *
 * the inserted word: 0 - never, 1 - only when a single match is inserted, *
 * 2 - when a full match is inserted (single or menu), 3 - always.         */

static int movetoend;

/* != 0 if we are in the middle of a menu completion and number of matches
* accepted with accept-and-menu-complete */

/**/
int menucmp, menuacc;

/* Information about menucompletion. */

/**/
struct menuinfo minfo;

/* Lists of brace-infos before/after cursor (first and last for each). */

/**/
Brinfo brbeg, lastbrbeg, brend, lastbrend;

/**/
int nbrbeg, nbrend;

static char *lastprebr, *lastpostbr;
static int hasunqu, useqbr, brpcs, brscs;

/* The list of matches.  fmatches contains the matches we first ignore *
 * because of fignore.                                                 */

/**/
LinkList matches, fmatches;

/* This holds the list of matches-groups. lastmatches holds the last list of 
 * permanently allocated matches, pmatches is the same for the list
 * currently built, amatches is the heap allocated stuff during completion
 * (after all matches have been generated it is an alias for pmatches), and
 * lmatches/lastlmatches is a pointer to the last element in the lists. */

/**/
Cmgroup lastmatches, pmatches, amatches, lmatches, lastlmatches;

/* Non-zero if we have permanently allocated matches (old and new). */

/**/
int hasoldlist, hasperm;

/* Non-zero if we have newly added matches. */

/**/
int newmatches;

/* Number of permanently allocated matches and groups. */

static int permmnum, permgnum, lastpermmnum, lastpermgnum;

/* The total number of matches and the number of matches to be listed. */

/**/
int nmatches, smatches;

/* !=0 if we have a valid completion list. */

/**/
int validlist;

/* != 0 if only explanation strings should be printed */

/**/
int onlyexpl;

/* Information about the matches for listing. */

/**/
struct cldata listdat;

/* This flag is non-zero if we are completing a pattern (with globcomplete) */

/**/
int ispattern, haspattern;

/* Non-zero if at least one match was added without -U. */

/**/
int hasmatched;

/* A parameter expansion prefix (like ${). */

static char *parpre;

/* Flags for parameter expansions for new style completion. */

static int parflags;

/* This holds the word we are completing in quoted from. */

static char *qword;

/* The current group of matches. */

static Cmgroup mgroup;

/* Match counters: all matches, normal matches (not alternate set). */

/**/
int mnum;

/* The match counter when unambig_data() was called. */

static int unambig_mnum;

/* Match flags for all matches in this group. */

/**/
int mflags;

/* Length of longest/shortest match. */

static int maxmlen, minmlen;

/* This holds the explanation strings we have to print in this group and *
 * a pointer to the current cexpl structure. */

static LinkList expls;

/**/
Cexpl curexpl;

/* A stack of completion matchers to be used. */

/**/
Cmlist mstack;

/* The completion matchers used when building new stuff for the line. */

/**/
Cmlist bmatchers;

/* A list with references to all matchers we used. */

/**/
LinkList matchers;

/* A heap of free Cline structures. */

static Cline freecl;

/* Ambiguous information. */

static Aminfo ainfo, fainfo;

/* This contains the name of the function to call if this is for a new  *
 * style completion. */

static char *compfunc = NULL;

/* The memory heap to use for new style completion generation. */

/**/
Heap compheap;

/* Find out if we have to insert a tab (instead of trying to complete). */

/* A list of some data.
 *
 * Well, actually, it's the list of all compctls used so far, but since
 * conceptually we don't know anything about compctls here... */

/**/
LinkList allccs;

/**/
static int
usetab(void)
{
    unsigned char *s = line + cs - 1;

    for (; s >= line && *s != '\n'; s--)
	if (*s != '\t' && *s != ' ')
	    return 0;
    return 1;
}

/* Non-zero if the last completion done was ambiguous (used to find   *
 * out if AUTOMENU should start).  More precisely, it's nonzero after *
 * successfully doing any completion, unless the completion was       *
 * unambiguous and did not cause the display of a completion list.    *
 * From the other point of view, it's nonzero iff AUTOMENU (if set)   *
 * should kick in on another completion.                              *
 *                                                                    *
 * If both AUTOMENU and BASHAUTOLIST are set, then we get a listing   *
 * on the second tab, a` la bash, and then automenu kicks in when     *
 * lastambig == 2.                                                    */

/**/
int lastambig;

/* This says what of the state the line is in when completion is started *
 * came from a previous completion. If the FC_LINE bit is set, the       *
 * string was inserted. If FC_INWORD is set, the last completion moved   *
 * the cursor into the word although it was at the end of it when the    *
 * last completion was invoked.                                          *
 * This is used to detect if the string should be taken as an exact      *
 * match (see do_ambiguous()) and if the cursor has to be moved to the   *
 * end of the word before generating the completions.                    */

static int fromcomp;

/* This holds the end-position of the last string inserted into the line. */

static int lastend;

#define FC_LINE   1
#define FC_INWORD 2

/* Arguments for and return value of completion widget. */

static char **cfargs;
static int cfret;

/**/
int
completecall(char **args)
{
    cfargs = args;
    cfret = 0;
    compfunc = compwidget->u.comp.func;
    if (compwidget->u.comp.fn(zlenoargs) && !cfret)
	cfret = 1;
    compfunc = NULL;

    return cfret;
}

/**/
int
completeword(char **args)
{
    usemenu = !!isset(MENUCOMPLETE);
    useglob = isset(GLOBCOMPLETE);
    if (c == '\t' && usetab())
	return selfinsert(args);
    else {
	int ret;
	if (lastambig == 1 && isset(BASHAUTOLIST) && !usemenu && !menucmp) {
	    ret = docomplete(COMP_LIST_COMPLETE);
	    lastambig = 2;
	} else
	    ret = docomplete(COMP_COMPLETE);
	return ret;
    }
}

/**/
int
menucomplete(char **args)
{
    usemenu = 1;
    useglob = isset(GLOBCOMPLETE);
    if (c == '\t' && usetab())
	return selfinsert(args);
    else
	return docomplete(COMP_COMPLETE);
}

/**/
int
listchoices(char **args)
{
    usemenu = !!isset(MENUCOMPLETE);
    useglob = isset(GLOBCOMPLETE);
    return docomplete(COMP_LIST_COMPLETE);
}

/**/
int
spellword(char **args)
{
    usemenu = useglob = 0;
    return docomplete(COMP_SPELL);
}

/**/
int
deletecharorlist(char **args)
{
    Cmgroup mg = minfo.group;
    Cmatch *mc = minfo.cur;
    int ret;

    usemenu = !!isset(MENUCOMPLETE);
    useglob = isset(GLOBCOMPLETE);
    if (cs != ll) {
	fixsuffix();
	ret = deletechar(args);
    } else
	ret = docomplete(COMP_LIST_COMPLETE);

    minfo.cur = mc;
    minfo.group = mg;
    return ret;
}

/**/
int
expandword(char **args)
{
    usemenu = useglob = 0;
    if (c == '\t' && usetab())
	return selfinsert(args);
    else
	return docomplete(COMP_EXPAND);
}

/**/
int
expandorcomplete(char **args)
{
    usemenu = !!isset(MENUCOMPLETE);
    useglob = isset(GLOBCOMPLETE);
    if (c == '\t' && usetab())
	return selfinsert(args);
    else {
	int ret;
	if (lastambig == 1 && isset(BASHAUTOLIST) && !usemenu && !menucmp) {
	    ret = docomplete(COMP_LIST_COMPLETE);
	    lastambig = 2;
	} else
	    ret = docomplete(COMP_EXPAND_COMPLETE);
	return ret;
    }
}

/**/
int
menuexpandorcomplete(char **args)
{
    usemenu = 1;
    useglob = isset(GLOBCOMPLETE);
    if (c == '\t' && usetab())
	return selfinsert(args);
    else
	return docomplete(COMP_EXPAND_COMPLETE);
}

/**/
int
listexpand(char **args)
{
    usemenu = !!isset(MENUCOMPLETE);
    useglob = isset(GLOBCOMPLETE);
    return docomplete(COMP_LIST_EXPAND);
}

/**/
int
reversemenucomplete(char **args)
{
    if (!menucmp)
	return menucomplete(args);

    HEAPALLOC {
	do {
	    if (minfo.cur == (minfo.group)->matches) {
		do {
		    if (!(minfo.group = (minfo.group)->prev))
			minfo.group = lmatches;
		} while (!(minfo.group)->mcount);
		minfo.cur = (minfo.group)->matches + (minfo.group)->mcount - 1;
	    } else
		minfo.cur--;
	} while (menuacc &&
		 !hasbrpsfx(*(minfo.cur), minfo.prebr, minfo.postbr));
	metafy_line();
	do_single(*(minfo.cur));
	unmetafy_line();
    } LASTALLOC;
    return 0;
}

/* Accepts the current completion and starts a new arg, *
 * with the next completions. This gives you a way to   *
 * accept several selections from the list of matches.  */

/**/
void
acceptlast(void)
{
    if (!menuacc) {
	zsfree(minfo.prebr);
	minfo.prebr = ztrdup(lastprebr);
	zsfree(minfo.postbr);
	minfo.postbr = ztrdup(lastpostbr);

	if (listshown && (lastprebr || lastpostbr)) {
	    Cmgroup g;
	    Cmatch *m;

	    for (g = amatches, m = NULL; g && (!m || !*m); g = g->next)
		for (m = g->matches; *m; m++)
		    if (!hasbrpsfx(*m, minfo.prebr, minfo.postbr)) {
			showinglist = -2;
			break;
		    }
	}
    }
    menuacc++;

    if (brbeg) {
	int l;

	iremovesuffix(',', 1);

	l = (brscs >= 0 ? brscs : cs) - brpcs;

	zsfree(lastbrbeg->str);
	lastbrbeg->str = (char *) zalloc(l + 2);
	memcpy(lastbrbeg->str, line + brpcs, l);
	lastbrbeg->str[l] = ',';
	lastbrbeg->str[l + 1] = '\0';
    } else {
	int l;

	cs = minfo.pos + minfo.len + minfo.insc;
	iremovesuffix(' ', 1);
	l = cs;
	cs = minfo.pos + minfo.len + minfo.insc - (*(minfo.cur))->qisl;
	if (cs < l)
	    foredel(l - cs);
	else if (cs > ll)
	    cs = ll;
	inststrlen(" ", 1, 1);
	if (parpre)
	    inststr(parpre);
	minfo.insc = minfo.len = 0;
	minfo.pos = cs;
	minfo.we = 1;
    }
}

/**/
int
acceptandmenucomplete(char **args)
{
    if (!menucmp)
	return 1;
    acceptlast();
    return menucomplete(args);
}

/* These are flags saying if we are completing in the command *
 * position, in a redirection, or in a parameter expansion.   */

/**/
int lincmd, linredir, ispar, parq, eparq, linwhat, linarr;

/* The string for the redirection operator. */

static char *rdstr;

/* This holds the name of the current command (used to find the right *
 * compctl).                                                          */

/**/
char *cmdstr;

/* This hold the name of the variable we are working on. */

static char *varname;

/* != 0 if we are in a subscript */

/**/
int insubscr;

/* Parameter pointer for completing keys of an assoc array. */

/**/
Param keypm;

/* 1 if we are completing in a quoted string (or inside `...`) */

/**/
int instring, inbackt;

/* Convenience macro for calling bslashquote() (formerly quotename()). *
 * This uses the instring variable above.                              */

#define quotename(s, e) bslashquote(s, e, instring)

/* Copy the given string and remove backslashes from the copy and return it. */

/**/
char *
rembslash(char *s)
{
    char *t = s = dupstring(s);

    while (*s)
	if (*s == '\\') {
	    chuck(s);
	    if (*s)
		s++;
	} else
	    s++;

    return t;
}

/* Check if the given string is the name of a parameter and if this *
 * parameter is one worth expanding.                                */

/**/
static int
checkparams(char *p)
{
    int t0, n, l = strlen(p), e = 0;
    struct hashnode *hn;

    for (t0 = paramtab->hsize - 1, n = 0; n < 2 && t0 >= 0; t0--)
	for (hn = paramtab->nodes[t0]; n < 2 && hn; hn = hn->next)
	    if (pfxlen(p, hn->nam) == l) {
		n++;
		if (strlen(hn->nam) == l)
		    e = 1;
	    }
    return (n == 1) ? (getsparam(p) != NULL) :
	(!menucmp && e && isset(RECEXACT));
}

/* Check if the given string has wildcards.  The difficulty is that we *
 * have to treat things like job specifications (%...) and parameter   *
 * expressions correctly.                                              */

/**/
static int
cmphaswilds(char *str)
{
    if ((*str == Inbrack || *str == Outbrack) && !str[1])
	return 0;

    /* If a leading % is immediately followed by ?, then don't *
     * treat that ? as a wildcard.  This is so you don't have  *
     * to escape job references such as %?foo.                 */
    if (str[0] == '%' && str[1] ==Quest)
	str += 2;

    for (; *str;) {
	if (*str == String || *str == Qstring) {
	    /* A parameter expression. */

	    if (*++str == Inbrace)
		skipparens(Inbrace, Outbrace, &str);
	    else if (*str == String || *str == Qstring)
		str++;
	    else {
		/* Skip all the things a parameter expression might start *
		 * with (before we come to the parameter name).           */
		for (; *str; str++)
		    if (*str != '^' && *str != Hat &&
			*str != '=' && *str != Equals &&
			*str != '~' && *str != Tilde)
			break;
		if (*str == '#' || *str == Pound)
		    str++;
		/* Star and Quest are parameter names here, not wildcards */
		if (*str == Star || *str == Quest)
		    str++;
	    }
	} else {
	    /* Not a parameter expression so we check for wildcards */
	    if (((*str == Pound || *str == Hat) && isset(EXTENDEDGLOB)) ||
		*str == Star || *str == Bar || *str == Quest ||
		!skipparens(Inbrack, Outbrack, &str) ||
		!skipparens(Inang,   Outang,   &str) ||
		(unset(IGNOREBRACES) &&
		 !skipparens(Inbrace, Outbrace, &str)) ||
		(*str == Inpar && str[1] == ':' &&
		 !skipparens(Inpar, Outpar, &str)))
		return 1;
	    if (*str)
		str++;
	}
    }
    return 0;
}

/* Check if we have to complete a parameter name. */

/**/
char *
check_param(char *s, int set, int test)
{
    char *p;

    zsfree(parpre);
    parpre = NULL;

    if (!test)
	ispar = parq = eparq = 0;
    /* Try to find a `$'. */
    for (p = s + offs; p > s && *p != String && *p != Qstring; p--);
    if (*p == String || *p == Qstring) {
	/* Handle $$'s */
	while (p > s && (p[-1] == String || p[-1] == Qstring))
	    p--;
	while ((p[1] == String || p[1] == Qstring) &&
	       (p[2] == String || p[2] == Qstring))
	    p += 2;
    }
    if ((*p == String || *p == Qstring) && p[1] != Inpar && p[1] != Inbrack) {
	/* This is really a parameter expression (not $(...) or $[...]). */
	char *b = p + 1, *e = b;
	int n = 0, br = 1, nest = 0;

	if (*b == Inbrace) {
	    char *tb = b;

	    /* If this is a ${...}, see if we are before the '}'. */
	    if (!skipparens(Inbrace, Outbrace, &tb))
		return NULL;

	    /* Ignore the possible (...) flags. */
	    b++, br++;
	    n = skipparens(Inpar, Outpar, &b);

	    for (tb = p - 1; tb > s && *tb != Outbrace && *tb != Inbrace; tb--);
	    if (tb > s && *tb == Inbrace && (tb[-1] == String || *tb == Qstring))
		nest = 1;
	}

	/* Ignore the stuff before the parameter name. */
	for (; *b; b++)
	    if (*b != '^' && *b != Hat &&
		*b != '=' && *b != Equals &&
		*b != '~' && *b != Tilde)
		break;
	if (*b == '#' || *b == Pound || *b == '+')
	    b++;

	e = b;
	if (br) {
	    while (*e == (test ? Dnull : '"'))
		e++, parq++;
	    if (!test)
		b = e;
	}
	/* Find the end of the name. */
	if (*e == Quest || *e == Star || *e == String || *e == Qstring ||
	    *e == '?'   || *e == '*'  || *e == '$'    ||
	    *e == '-'   || *e == '!'  || *e == '@')
	    e++;
	else if (idigit(*e))
	    while (idigit(*e))
		e++;
	else if (iident(*e))
	    while (iident(*e) ||
		   (comppatmatch && *comppatmatch &&
		    (*e == Star || *e == Quest)))
		e++;

	/* Now make sure that the cursor is inside the name. */
	if (offs <= e - s && offs >= b - s && n <= 0) {
	    char sav;

	    if (br) {
		p = e;
		while (*p == (test ? Dnull : '"'))
		    p++, parq--, eparq++;
	    }
	    /* It is. */
	    if (test)
		return b;
	    /* If we were called from makecomplistflags(), we have to set the
	     * global variables. */

	    if (set) {
		if (br >= 2) {
		    mflags |= CMF_PARBR;
		    if (nest)
			mflags |= CMF_PARNEST;
		}
		/* Get the prefix (anything up to the character before the name). */
		isuf = dupstring(e);
		untokenize(isuf);
		sav = *b;
		*b = *e = '\0';
		ripre = dyncat((ripre ? ripre : ""), s);
		ipre = dyncat((ipre ? ipre : ""), s);
		*b = sav;

		untokenize(ipre);
	    }
	    else
		parq = eparq = 0;

	    /* Save the prefix. */
	    if (compfunc) {
		parflags = (br >= 2 ? CMF_PARBR : 0);
		sav = *b;
		*b = '\0';
		untokenize(parpre = ztrdup(s));
		*b = sav;
	    }
	    /* And adjust wb, we, and offs again. */
	    offs -= b - s;
	    wb = cs - offs;
	    we = wb + e - b;
	    ispar = (br >= 2 ? 2 : 1);
	    b[we-wb] = '\0';
	    return b;
	}
    }
    return NULL;
}

/* The main entry point for completion. */

/**/
static int
docomplete(int lst)
{
    char *s, *ol;
    int olst = lst, chl = 0, ne = noerrs, ocs, ret = 0, omc = menucmp;

    if (showagain && validlist)
	showinglist = -2;
    showagain = 0;

    /* If we are doing a menu-completion... */

    if (menucmp && lst != COMP_LIST_EXPAND && 
	(!compwidget || compwidget == lastcompwidget)) {
	do_menucmp(lst);
	return 0;
    }
    lastcompwidget = compwidget;

    /* We may have to reset the cursor to its position after the   *
     * string inserted by the last completion. */

    if (fromcomp & FC_INWORD)
	if ((cs = lastend) > ll)
	    cs = ll;

    /* Check if we have to start a menu-completion (via automenu). */

    if (isset(AUTOMENU) && lastambig &&
	(!isset(BASHAUTOLIST) || lastambig == 2))
	usemenu = 2;

    /* Expand history references before starting completion.  If anything *
     * changed, do no more.                                               */

    if (doexpandhist())
	return 0;

    metafy_line();

    ocs = cs;
    origline = dupstring((char *) line);
    origcs = cs;
    origll = ll;
    if (!isfirstln && chline != NULL) {
	/* If we are completing in a multi-line buffer (which was not  *
	 * taken from the history), we have to prepend the stuff saved *
	 * in chline to the contents of line.                          */

	ol = dupstring((char *)line);
	/* Make sure that chline is zero-terminated. */
	*hptr = '\0';
	cs = 0;
	inststr(chline);
	chl = cs;
	cs += ocs;
    } else
	ol = NULL;
    inwhat = IN_NOTHING;
    qword = NULL;
    zsfree(qipre);
    qipre = ztrdup("");
    zsfree(qisuf);
    qisuf = ztrdup("");
    autoq = '\0';
    /* Get the word to complete. */
    noerrs = 1;
    s = get_comp_string();
    DPUTS(wb < 0 || cs < wb || cs > we,
	  "BUG: 0 <= wb <= cs <= we is not true!");
    noerrs = ne;
    /* For vi mode, reset the start-of-insertion pointer to the beginning *
     * of the word being completed, if it is currently later.  Vi itself  *
     * would never change the pointer in the middle of an insertion, but  *
     * then vi doesn't have completion.  More to the point, this is only  *
     * an emulation.                                                      */
    if (viinsbegin > ztrsub((char *) line + wb, (char *) line))
	viinsbegin = ztrsub((char *) line + wb, (char *) line);
    /* If we added chline to the line buffer, reset the original contents. */
    if (ol) {
	cs -= chl;
	wb -= chl;
	we -= chl;
	if (wb < 0) {
	    strcpy((char *) line, ol);
	    ll = strlen((char *) line);
	    cs = ocs;
	    unmetafy_line();
	    return 1;
	}
	ocs = cs;
	cs = 0;
	foredel(chl);
	cs = ocs;
    }
    freeheap();
    /* Save the lexer state, in case the completion code uses the lexer *
     * somewhere (e.g. when processing a compctl -s flag).              */
    lexsave();
    if (inwhat == IN_ENV)
	lincmd = 0;
    if (s) {
	if (lst == COMP_EXPAND_COMPLETE) {
	    /* Check if we have to do expansion or completion. */
	    char *q = s;

	    if (*q == Equals) {
		/* The word starts with `=', see if we can expand it. */
		q = s + 1;
		if (cmdnamtab->getnode(cmdnamtab, q) || hashcmd(q, pathchecked)) {
		    if (isset(RECEXACT))
			lst = COMP_EXPAND;
		    else {
			int t0, n = 0;
			struct hashnode *hn;

			for (t0 = cmdnamtab->hsize - 1; t0 >= 0; t0--)
			    for (hn = cmdnamtab->nodes[t0]; hn;
				 hn = hn->next) {
				if (strpfx(q, hn->nam) && findcmd(hn->nam, 0))
				    n++;
				if (n == 2)
				    break;
			    }

			if (n == 1)
			    lst = COMP_EXPAND;
		    }
		}
	    }
	    if (lst == COMP_EXPAND_COMPLETE)
		do {
		    /* Check if there is a parameter expression. */
		    for (; *q && *q != String; q++);
		    if (*q == String && q[1] != Inpar && q[1] != Inbrack) {
			if (*++q == Inbrace) {
			    if (! skipparens(Inbrace, Outbrace, &q) &&
				q == s + cs - wb)
				lst = COMP_EXPAND;
			} else {
			    char *t, sav, sav2;

			    /* Skip the things parameter expressions might *
			     * start with (the things before the parameter *
			     * name).                                      */
			    for (; *q; q++)
				if (*q != '^' && *q != Hat &&
				    *q != '=' && *q != Equals &&
				    *q != '~' && *q != Tilde)
				    break;
			    if ((*q == '#' || *q == Pound || *q == '+') &&
				q[1] != String)
				q++;

			    sav2 = *(t = q);
			    if (*q == Quest || *q == Star || *q == String ||
				*q == Qstring)
				*q = ztokens[*q - Pound], ++q;
			    else if (*q == '?' || *q == '*' || *q == '$' ||
				     *q == '-' || *q == '!' || *q == '@')
				q++;
			    else if (idigit(*q))
				do q++; while (idigit(*q));
			    else
				while (iident(*q))
				    q++;
			    sav = *q;
			    *q = '\0';
			    if (cs - wb == q - s &&
				(idigit(sav2) || checkparams(t)))
				lst = COMP_EXPAND;
			    *q = sav;
			    *t = sav2;
			}
			if (lst != COMP_EXPAND)
			    lst = COMP_COMPLETE;
		    } else
			break;
		} while (q < s + cs - wb);
	    if (lst == COMP_EXPAND_COMPLETE) {
		/* If it is still not clear if we should use expansion or   *
		 * completion and there is a `$' or a backtick in the word, *
		 * than do expansion.                                       */
		for (q = s; *q; q++)
		    if (*q == Tick || *q == Qtick ||
			*q == String || *q == Qstring)
			break;
		lst = *q ? COMP_EXPAND : COMP_COMPLETE;
	    }
	    /* And do expansion if there are wildcards and globcomplete is *
	     * not used.                                                   */
	    if (unset(GLOBCOMPLETE) && cmphaswilds(s))
		lst = COMP_EXPAND;
	}
	if (lincmd && (inwhat == IN_NOTHING))
	    inwhat = IN_CMD;

	if (lst == COMP_SPELL) {
	    char *x, *q, *ox;

	    for (q = s; *q; q++)
		if (INULL(*q))
		    *q = Nularg;
	    cs = wb;
	    foredel(we - wb);
	    HEAPALLOC {
		untokenize(x = ox = dupstring(s));
		if (*s == Tilde || *s == Equals || *s == String)
		    *x = *s;
		spckword(&x, 0, lincmd, 0);
		ret = !strcmp(x, ox);
	    } LASTALLOC;
	    untokenize(x);
	    inststr(x);
	} else if (COMP_ISEXPAND(lst)) {
	    /* Do expansion. */
	    char *ol = (olst == COMP_EXPAND_COMPLETE) ?
		dupstring((char *)line) : (char *)line;
	    int ocs = cs, ne = noerrs;

	    noerrs = 1;
	    ret = doexpansion(s, lst, olst, lincmd);
	    lastambig = 0;
	    noerrs = ne;

	    /* If expandorcomplete was invoked and the expansion didn't *
	     * change the command line, do completion.                  */
	    if (olst == COMP_EXPAND_COMPLETE &&
		!strcmp(ol, (char *)line)) {
		cs = ocs;
		errflag = 0;

		if (!compfunc) {
		    char *p;

		    p = s;
		    if (*p == Tilde || *p == Equals)
			p++;
		    for (; *p; p++)
			if (itok(*p)) {
			    if (*p != String && *p != Qstring)
				*p = ztokens[*p - Pound];
			    else if (p[1] == Inbrace)
				p++, skipparens(Inbrace, Outbrace, &p);
			}
		}
		ret = docompletion(s, lst, lincmd);
	    } else if (ret)
		clearlist = 1;
	} else
	    /* Just do completion. */
	    ret = docompletion(s, lst, lincmd);
	zsfree(s);
    } else
	ret = 1;
    /* Reset the lexer state, pop the heap. */
    lexrestore();
    popheap();
    zsfree(qword);
    unmetafy_line();

    if (menucmp && !omc) {
	struct chdata dat;

	dat.matches = amatches;
	dat.num = nmatches;
	dat.cur = NULL;
	if (runhookdef(MENUSTARTHOOK, (void *) &dat))
	    menucmp = menuacc = 0;
    }
    return ret;
}

/* Do completion, given that we are in the middle of a menu completion.  We *
 * don't need to generate a list of matches, because that's already been    *
 * done by previous commands.  We will either list the completions, or      *
 * insert the next completion.                                              */

/**/
void
do_menucmp(int lst)
{
    /* Just list the matches if the list was requested. */
    if (lst == COMP_LIST_COMPLETE) {
	showinglist = -2;
	return;
    }
    /* Otherwise go to the next match in the array... */
    HEAPALLOC {
	do {
	    if (!*++(minfo.cur)) {
		do {
		    if (!(minfo.group = (minfo.group)->next))
			minfo.group = amatches;
		} while (!(minfo.group)->mcount);
		minfo.cur = minfo.group->matches;
	    }
	} while (menuacc &&
		 !hasbrpsfx(*(minfo.cur), minfo.prebr, minfo.postbr));
	/* ... and insert it into the command line. */
	metafy_line();
	do_single(*(minfo.cur));
	unmetafy_line();
    } LASTALLOC;
}

/* 1 if we are completing the prefix */
static int comppref;

/* This function inserts an `x' in the command line at the cursor position. *
 *                                                                          *
 * Oh, you want to know why?  Well, if completion is tried somewhere on an  *
 * empty part of the command line, the lexer code would normally not be     *
 * able to give us the `word' we want to complete, since there is no word.  *
 * But we need to call the lexer to find out where we are (and for which    *
 * command we are completing and such things).  So we temporarily add a `x' *
 * (any character without special meaning would do the job) at the cursor   *
 * position, than the lexer gives us the word `x' and its beginning and end *
 * positions and we can remove the `x'.                                     *
 *									    *
 * If we are just completing the prefix (comppref set), we also insert a    *
 * space after the x to end the word.  We never need to remove the space:   *
 * anywhere we are able to retrieve a word for completion it will be	    *
 * discarded as whitespace.  It has the effect of making any suffix	    *
 * referrable to as the next word on the command line when indexing	    *
 * from a completion function.                                              */

/**/
static void
addx(char **ptmp)
{
    int addspace = 0;

    if (!line[cs] || line[cs] == '\n' ||
	(iblank(line[cs]) && (!cs || line[cs-1] != '\\')) ||
	line[cs] == ')' || line[cs] == '`' ||
	(instring && (line[cs] == '"' || line[cs] == '\'')) ||
	(addspace = (comppref && !iblank(line[cs])))) {
	*ptmp = (char *)line;
	line = (unsigned char *)zhalloc(strlen((char *)line) + 3 + addspace);
	memcpy(line, *ptmp, cs);
	line[cs] = 'x';
	if (addspace)
	    line[cs+1] = ' ';
	strcpy((char *)line + cs + 1 + addspace, (*ptmp) + cs);
	addedx = 1 + addspace;
    } else {
	addedx = 0;
	*ptmp = NULL;
    }
}

/* Like dupstring, but add an extra space at the end of the string. */

/**/
char *
dupstrspace(const char *str)
{
    int len = strlen((char *)str);
    char *t = (char *) ncalloc(len + 2);
    strcpy(t, str);
    strcpy(t+len, " ");
    return t;
}

/* These functions metafy and unmetafy the ZLE buffer, as described at the *
 * top of this file.  Note that ll and cs are translated.  They *must* be  *
 * called in matching pairs, around all the expansion/completion code.     *
 * Currently, there are four pairs: in history expansion, in the main      *
 * completion function, and one in each of the middle-of-menu-completion   *
 * functions (there's one for each direction).                             */

/**/
static void
metafy_line(void)
{
    int len = ll;
    char *s;

    for (s = (char *) line; s < (char *) line + ll;)
	if (imeta(*s++))
	    len++;
    sizeline(len);
    (void) metafy((char *) line, ll, META_NOALLOC);
    ll = len;
    cs = metalen((char *) line, cs);
}

/**/
static void
unmetafy_line(void)
{
    cs = ztrsub((char *) line + cs, (char *) line);
    (void) unmetafy((char *) line, &ll);
}

/* Free a brinfo list. */

/**/
void
freebrinfo(Brinfo p)
{
    Brinfo n;

    while (p) {
	n = p->next;
	zsfree(p->str);
	zfree(p, sizeof(*p));

	p = n;
    }
}

/* Duplicate a brinfo list. */

/**/
Brinfo
dupbrinfo(Brinfo p, Brinfo *last)
{
    Brinfo ret = NULL, *q = &ret, n = NULL;

    while (p) {
	n = *q = (Brinfo) alloc(sizeof(*n));
	q = &(n->next);

	n->next = NULL;
	n->str = dupstring(p->str);
	n->pos = p->pos;
	n->qpos = p->qpos;
	n->curpos = p->curpos;

	p = p->next;
    }
    if (last)
	*last = n;

    return ret;
}

/* Lasciate ogni speranza.                                                  *
 * This function is a nightmare.  It works, but I'm sure that nobody really *
 * understands why.  The problem is: to make it cleaner we would need       *
 * changes in the lexer code (and then in the parser, and then...).         */

/**/
static char *
get_comp_string(void)
{
    int t0, tt0, i, j, k, cp, rd, sl, ocs, ins, oins, ia, parct, varq = 0;
    char *s = NULL, *linptr, *tmp, *p, *tt = NULL;

    freebrinfo(brbeg);
    freebrinfo(brend);
    brbeg = lastbrbeg = brend = lastbrend = NULL;
    nbrbeg = nbrend = 0;
    zsfree(lastprebr);
    zsfree(lastpostbr);
    lastprebr = lastpostbr = NULL;

    /* This global flag is used to signal the lexer code if it should *
     * expand aliases or not.                                         */
    noaliases = isset(COMPLETEALIASES);

    /* Find out if we are somewhere in a `string', i.e. inside '...', *
     * "...", `...`, or ((...)). Nowadays this is only used to find   *
     * out if we are inside `...`.                                    */

    for (i = j = k = 0, p = (char *)line; p < (char *)line + cs; p++)
	if (*p == '`' && !(k & 1))
	    i++;
	else if (*p == '\"' && !(k & 1) && !(i & 1))
	    j++;
	else if (*p == '\'' && !(j & 1))
	    k++;
	else if (*p == '\\' && p[1] && !(k & 1))
	    p++;
    inbackt = (i & 1);
    instring = 0;
    addx(&tmp);
    linptr = (char *)line;
    pushheap();
    HEAPALLOC {
      start:
	inwhat = IN_NOTHING;
	/* Now set up the lexer and start it. */
	parbegin = parend = -1;
	lincmd = incmdpos;
	linredir = inredir;
	zsfree(cmdstr);
	cmdstr = NULL;
	zsfree(varname);
	varname = NULL;
	insubscr = 0;
	zleparse = 1;
	clwpos = -1;
	lexsave();
	inpush(dupstrspace((char *) linptr), 0, NULL);
	strinbeg(0);
	i = tt0 = cp = rd = ins = oins = linarr = parct = ia = 0;

	/* This loop is possibly the wrong way to do this.  It goes through *
	 * the previously massaged command line using the lexer.  It stores *
	 * each token in each command (commands being regarded, roughly, as *
	 * being separated by tokens | & &! |& || &&).  The loop stops when *
	 * the end of the command containing the cursor is reached.  It's a *
	 * simple way to do things, but suffers from an inability to        *
	 * distinguish actual command arguments from, for example,          *
	 * filenames in redirections.  (But note that code elsewhere checks *
	 * if we are completing *in* a redirection.)  The only way to fix   *
	 * this would be to pass the command line through the parser too,   *
	 * and get the arguments that way.  Maybe in 3.1...                 */
	do {
	    lincmd = ((incmdpos && !ins) || (oins == 2 && i == 2) ||
		      (ins == 3 && i == 1));
	    linredir = (inredir && !ins);
	    oins = ins;
	    /* Get the next token. */
	    if (linarr)
		incmdpos = 0;
	    ctxtlex();

	    if (tok == LEXERR) {
		if (!tokstr)
		    break;
		for (j = 0, p = tokstr; *p; p++)
		    if (*p == Snull || *p == Dnull)
			j++;
		if (j & 1) {
		    if (lincmd && strchr(tokstr, '=')) {
			varq = 1;
			tok = ENVSTRING;
		    } else
			tok = STRING;
		}
	    } else if (tok == ENVSTRING)
		varq = 0;
	    if (tok == ENVARRAY) {
		linarr = 1;
		zsfree(varname);
		varname = ztrdup(tokstr);
	    } else if (tok == INPAR)
		parct++;
	    else if (tok == OUTPAR) {
		if (parct)
		    parct--;
		else
		    linarr = 0;
	    }
	    if (inredir)
		rdstr = tokstrings[tok];
	    if (tok == DINPAR)
		tokstr = NULL;

	    /* We reached the end. */
	    if (tok == ENDINPUT)
		break;
	    if ((ins && (tok == DO || tok == SEPER)) ||
		(ins == 2 && i == 2) ||	(ins == 3 && i == 3) ||
		tok == BAR    || tok == AMPER     ||
		tok == BARAMP || tok == AMPERBANG ||
		((tok == DBAR || tok == DAMPER) && !incond)) {
		/* This is one of the things that separate commands.  If we  *
		 * already have the things we need (e.g. the token strings), *
		 * leave the loop.                                           */
		if (tt)
		    break;
		/* Otherwise reset the variables we are collecting data in. */
		i = tt0 = cp = rd = ins = 0;
	    }
	    if (lincmd && (tok == STRING || tok == FOR || tok == FOREACH ||
			   tok == SELECT || tok == REPEAT || tok == CASE)) {
		/* The lexer says, this token is in command position, so *
		 * store the token string (to find the right compctl).   */
		ins = (tok == REPEAT ? 2 : (tok != STRING));
		zsfree(cmdstr);
		cmdstr = ztrdup(tokstr);
		i = 0;
	    }
	    if (!zleparse && !tt0) {
		/* This is done when the lexer reached the word the cursor is on. */
		tt = tokstr ? dupstring(tokstr) : NULL;
		/* If we added a `x', remove it. */
		if (addedx && tt)
		    chuck(tt + cs - wb);
		tt0 = tok;
		/* Store the number of this word. */
		clwpos = i;
		cp = lincmd;
		rd = linredir;
		ia = linarr;
		if (inwhat == IN_NOTHING && incond)
		    inwhat = IN_COND;
	    } else if (linredir)
		continue;
	    if (!tokstr)
		continue;
	    /* Hack to allow completion after `repeat n do'. */
	    if (oins == 2 && !i && !strcmp(tokstr, "do"))
		ins = 3;
	    /* We need to store the token strings of all words (for some of *
	     * the more complicated compctl -x things).  They are stored in *
	     * the clwords array.  Make this array big enough.              */
	    if (i + 1 == clwsize) {
		int n;
		clwords = (char **)realloc(clwords,
					   (clwsize *= 2) * sizeof(char *));
		for(n = clwsize; --n > i; )
		    clwords[n] = NULL;
	    }
	    zsfree(clwords[i]);
	    /* And store the current token string. */
	    clwords[i] = ztrdup(tokstr);
	    sl = strlen(tokstr);
	    /* Sometimes the lexer gives us token strings ending with *
	     * spaces we delete the spaces.                           */
	    while (sl && clwords[i][sl - 1] == ' ' &&
		   (sl < 2 || (clwords[i][sl - 2] != Bnull &&
			       clwords[i][sl - 2] != Meta)))
		clwords[i][--sl] = '\0';
	    /* If this is the word the cursor is in and we added a `x', *
	     * remove it.                                               */
	    if (clwpos == i++ && addedx)
		chuck(&clwords[i - 1][((cs - wb) >= sl) ?
				     (sl - 1) : (cs - wb)]);
	} while (tok != LEXERR && tok != ENDINPUT &&
		 (tok != SEPER || (zleparse && !tt0)));
	/* Calculate the number of words stored in the clwords array. */
	clwnum = (tt || !i) ? i : i - 1;
	zsfree(clwords[clwnum]);
	clwords[clwnum] = NULL;
	t0 = tt0;
	if (ia) {
	    lincmd = linredir = 0;
	    inwhat = IN_ENV;
	} else {
	    lincmd = cp;
	    linredir = rd;
	}
	strinend();
	inpop();
	errflag = zleparse = 0;
	if (parbegin != -1) {
	    /* We are in command or process substitution if we are not in
	     * a $((...)). */
	    if (parend >= 0 && !tmp)
		line = (unsigned char *) dupstring(tmp = (char *)line);
	    linptr = (char *) line + ll + addedx - parbegin + 1;
	    if ((linptr - (char *) line) < 3 || *linptr != '(' ||
		linptr[-1] != '(' || linptr[-2] != '$') {
		if (parend >= 0) {
		    ll -= parend;
		    line[ll + addedx] = '\0';
		}
		lexrestore();
		goto start;
	    }
	}

	if (inwhat == IN_MATH)
	    s = NULL;
	else if (!t0 || t0 == ENDINPUT) {
	    /* There was no word (empty line). */
	    s = ztrdup("");
	    we = wb = cs;
	    clwpos = clwnum;
	    t0 = STRING;
	} else if (t0 == STRING) {
	    /* We found a simple string. */
	    s = ztrdup(clwords[clwpos]);
	} else if (t0 == ENVSTRING) {
	    char sav;
	    /* The cursor was inside a parameter assignment. */

	    if (varq)
		tt = clwords[clwpos];

	    for (s = tt; iident(*s); s++);
	    sav = *s;
	    *s = '\0';
	    zsfree(varname);
	    varname = ztrdup(tt);
	    *s = sav;
	    if (skipparens(Inbrack, Outbrack, &s) > 0 || s > tt + cs - wb) {
		s = NULL;
		inwhat = IN_MATH;
		if ((keypm = (Param) paramtab->getnode(paramtab, varname)) &&
		    (keypm->flags & PM_HASHED))
		    insubscr = 2;
		else
		    insubscr = 1;
	    } else if (*s == '=' && cs > wb + (s - tt)) {
		s++;
		wb += s - tt;
		t0 = STRING;
		s = ztrdup(s);
		inwhat = IN_ENV;
	    }
	    lincmd = 1;
	}
	if (we > ll)
	    we = ll;
	tt = (char *)line;
	if (tmp) {
	    line = (unsigned char *)tmp;
	    ll = strlen((char *)line);
	}
	if (t0 != STRING && inwhat != IN_MATH) {
	    if (tmp) {
		tmp = NULL;
		linptr = (char *)line;
		lexrestore();
		addedx = 0;
		goto start;
	    }
	    noaliases = 0;
	    lexrestore();
	    LASTALLOC_RETURN NULL;
	}

	noaliases = 0;

	/* Check if we are in an array subscript.  We simply assume that  *
	 * we are in a subscript if we are in brackets.  Correct solution *
	 * is very difficult.  This is quite close, but gets things like  *
	 * foo[_ wrong (note no $).  If we are in a subscript, treat it   *
	 * as being in math.                                              */
	if (inwhat != IN_MATH) {
	    int i = 0;
	    char *nnb = (iident(*s) ? s : s + 1), *nb = NULL, *ne = NULL;

	    for (tt = s; ++tt < s + cs - wb;)
		if (*tt == Inbrack) {
		    i++;
		    nb = nnb;
		    ne = tt;
		} else if (i && *tt == Outbrack)
		    i--;
		else if (!iident(*tt))
		    nnb = tt + 1;
	    if (i) {
		inwhat = IN_MATH;
		insubscr = 1;
		if (nb < ne) {
		    char sav = *ne;
		    *ne = '\0';
		    zsfree(varname);
		    varname = ztrdup(nb);
		    *ne = sav;
		    if ((keypm = (Param) paramtab->getnode(paramtab,
							   varname)) &&
			(keypm->flags & PM_HASHED))
			insubscr = 2;
		}
	    }
	}
	if (inwhat == IN_MATH) {
	    if (compfunc || insubscr == 2) {
		int lev;
		char *p;

		for (wb = cs - 1, lev = 0; wb > 0; wb--)
		    if (line[wb] == ']' || line[wb] == ')')
			lev++;
		    else if (line[wb] == '[') {
			if (!lev--)
			    break;
		    } else if (line[wb] == '(') {
			if (!lev && line[wb - 1] == '(')
			    break;
			if (lev)
			    lev--;
		    }
		p = (char *) line + wb;
		wb++;
		if (wb && (*p == '[' || *p == '(') &&
		    !skipparens(*p, (*p == '[' ? ']' : ')'), &p)) {
			we = (p - (char *) line) - 1;
			if (insubscr == 2)
			    insubscr = 3;
		}
	    } else {
		/* In mathematical expression, we complete parameter names  *
		 * (even if they don't have a `$' in front of them).  So we *
		 * have to find that name.                                  */
		for (we = cs; iident(line[we]); we++);
		for (wb = cs; --wb >= 0 && iident(line[wb]););
		wb++;
	    }
	    zsfree(s);
	    s = zalloc(we - wb + 1);
	    strncpy(s, (char *) line + wb, we - wb);
	    s[we - wb] = '\0';
	    if (wb > 2 && line[wb - 1] == '[' && iident(line[wb - 2])) {
		int i = wb - 3;
		unsigned char sav = line[wb - 1];

		while (i >= 0 && iident(line[i]))
		    i--;

		line[wb - 1] = '\0';
		zsfree(varname);
		varname = ztrdup((char *) line + i + 1);
		line[wb - 1] = sav;
		if ((keypm = (Param) paramtab->getnode(paramtab, varname)) &&
		    (keypm->flags & PM_HASHED)) {
		    if (insubscr != 3)
			insubscr = 2;
		} else
		    insubscr = 1;
	    }
	}
	/* This variable will hold the current word in quoted form. */
	qword = ztrdup(s);
	offs = cs - wb;
	if ((p = check_param(s, 0, 1))) {
	    for (p = s; *p; p++)
		if (*p == Dnull)
		    *p = '"';
		else if (*p == Snull)
		    *p = '\'';
	}
	if ((*s == Snull || *s == Dnull) && !has_token(s + 1)) {
	    char *q = (*s == Snull ? "'" : "\""), *n = tricat(qipre, q, "");
	    int sl = strlen(s);

	    instring = (*s == Snull ? 1 : 2);
	    zsfree(qipre);
	    qipre = n;
	    if (sl > 1 && s[sl - 1] == *s) {
		n = tricat(q, qisuf, "");
		zsfree(qisuf);
		qisuf = n;
	    }
	    autoq = *q;
	}
	/* While building the quoted form, we also clean up the command line. */
	for (p = s, tt = qword, i = wb; *p; p++, tt++, i++)
	    if (INULL(*p)) {
		if (i < cs)
		    offs--;
		if (p[1] || *p != Bnull) {
		    if (*p == Bnull) {
			*tt = '\\';
			if (cs == i + 1)
			    cs++, offs++;
		    } else {
			ocs = cs;
			cs = i;
			foredel(1);
			chuck(tt--);
			if ((cs = ocs) > i--)
			    cs--;
			we--;
		    }
		} else {
		    ocs = cs;
		    *tt = '\0';
		    cs = we;
		    backdel(1);
		    if (ocs == we)
			cs = we - 1;
		    else
			cs = ocs;
		    we--;
		}
		chuck(p--);
	    }

	if (!isset(IGNOREBRACES)) {
	    /* Try and deal with foo{xxx etc. */
	    char *curs = s + (isset(COMPLETEINWORD) ? offs : strlen(s));
	    char *predup = dupstring(s), *dp = predup;
	    char *bbeg = NULL, *bend = NULL, *dbeg;
	    char *lastp = NULL, *firsts = NULL;
	    int cant = 0, begi, boffs = offs, hascom = 0;

	    for (i = 0, p = s; *p; p++, dp++, i++) {
		/* careful, ${... is not a brace expansion...
		 * we try to get braces after a parameter expansion right,
		 * but this may fail sometimes. sorry.
		 */
		if (*p == String || *p == Qstring) {
		    if (p[1] == Inbrace || p[1] == Inpar || p[1] == Inbrack) {
			char *tp = p + 1;

			if (skipparens(*tp, (*tp == Inbrace ? Outbrace :
					     (*tp == Inpar ? Outpar : Outbrack)),
				       &tp)) {
			    tt = NULL;
			    break;
			}
			i += tp - p;
			p = tp;
			dp += tp - p;
		    } else {
			char *tp = p + 1;

			for (; *tp == '^' || *tp == Hat ||
				 *tp == '=' || *tp == Equals ||
				 *tp == '~' || *tp == Tilde ||
				 *tp == '#' || *tp == Pound || *tp == '+';
			     tp++);
			if (*tp == Quest || *tp == Star || *tp == String ||
			    *tp == Qstring || *tp == '?' || *tp == '*' ||
			    *tp == '$' || *tp == '-' || *tp == '!' ||
			    *tp == '@')
			    p++, i++;
			else {
			    if (idigit(*tp))
				while (idigit(*tp))
				    tp++;
			    else if (iident(*tp))
				while (iident(*tp))
				    tp++;
			    else {
				tt = NULL;
				break;
			    }
			    if (*tp == Inbrace) {
				cant = 1;
				break;
			    }
			    tp--;
			    i += tp - p;
			    p = tp;
			    dp += tp - p;
			}
		    }
		} else if (p < curs) {
		    if (*p == Outbrace) {
			cant = 1;
			break;
		    }
		    if (*p == Inbrace) {
			if (bbeg) {
			    Brinfo new;
			    int len = bend - bbeg;

			    new = (Brinfo) zalloc(sizeof(*new));
			    nbrbeg++;

			    new->next = NULL;
			    if (lastbrbeg)
				lastbrbeg->next = new;
			    else
				brbeg = new;
			    lastbrbeg = new;

			    new->next = NULL;
			    PERMALLOC {
				new->str = dupstrpfx(bbeg, len);
				untokenize(new->str);
			    } LASTALLOC;
			    new->pos = begi;
			    *dbeg = '\0';
			    new->qpos = strlen(quotename(predup, NULL));
			    *dbeg = '{';
			    i -= len;
			    boffs -= len;
			    strcpy(dbeg, dbeg + len);
			    dp -= len;
			}
			bbeg = lastp = p;
			dbeg = dp;
			bend = p + 1;
			begi = i;
		    } else if (*p == Comma && bbeg) {
			bend = p + 1;
			hascom = 1;
		    }
		} else {
		    if (*p == Inbrace) {
			cant = 1;
			break;
		    }
		    if (p == curs) {
			if (bbeg) {
			    Brinfo new;
			    int len = bend - bbeg;

			    new = (Brinfo) zalloc(sizeof(*new));
			    nbrbeg++;

			    new->next = NULL;
			    if (lastbrbeg)
				lastbrbeg->next = new;
			    else
				brbeg = new;
			    lastbrbeg = new;

			    PERMALLOC {
				new->str = dupstrpfx(bbeg, len);
				untokenize(new->str);
			    } LASTALLOC;
			    new->pos = begi;
			    *dbeg = '\0';
			    new->qpos = strlen(quotename(predup, NULL));
			    *dbeg = '{';
			    i -= len;
			    boffs -= len;
			    strcpy(dbeg, dbeg + len);
			    dp -= len;
			}
			bbeg = NULL;
		    }
		    if (*p == Comma) {
			if (!bbeg)
			    bbeg = p;
			hascom = 1;
		    } else if (*p == Outbrace) {
			Brinfo new;
			int len;

			if (!bbeg)
			    bbeg = p;
			len = p + 1 - bbeg;
			if (!firsts)
			    firsts = p + 1;
			
			new = (Brinfo) zalloc(sizeof(*new));
			nbrend++;

			if (!lastbrend)
			    lastbrend = new;

			new->next = brend;
			brend = new;

			PERMALLOC {
			    new->str = dupstrpfx(bbeg, len);
			    untokenize(new->str);
			} LASTALLOC;
			new->pos = dp - predup - len + 1;
			new->qpos = len;
			bbeg = NULL;
		    }
		}
	    }
	    if (cant) {
		freebrinfo(brbeg);
		freebrinfo(brend);
		brbeg = lastbrbeg = brend = lastbrend = NULL;
		nbrbeg = nbrend = 0;
	    } else {
		if (p == curs && bbeg) {
		    Brinfo new;
		    int len = bend - bbeg;

		    new = (Brinfo) zalloc(sizeof(*new));
		    nbrbeg++;

		    new->next = NULL;
		    if (lastbrbeg)
			lastbrbeg->next = new;
		    else
			brbeg = new;
		    lastbrbeg = new;

		    PERMALLOC {
			new->str = dupstrpfx(bbeg, len);
			untokenize(new->str);
		    } LASTALLOC;
		    new->pos = begi;
		    *dbeg = '\0';
		    new->qpos = strlen(quotename(predup, NULL));
		    *dbeg = '{';
		    boffs -= len;
		    strcpy(dbeg, dbeg + len);
		}
		if (brend) {
		    Brinfo bp, prev = NULL;
		    int p, l;

		    for (bp = brend; bp; bp = bp->next) {
			bp->prev = prev;
			prev = bp;
			p = bp->pos;
			l = bp->qpos;
			bp->pos = strlen(predup + p + l);
			bp->qpos = strlen(quotename(predup + p + l, NULL));
			strcpy(predup + p, predup + p + l);
		    }
		}
		if (hascom) {
		    if (lastp) {
			char sav = *lastp;

			*lastp = '\0';
			untokenize(lastprebr = ztrdup(s));
			*lastp = sav;
		    }
		    if ((lastpostbr = ztrdup(firsts)))
			untokenize(lastpostbr);
		}
		zsfree(s);
		s = ztrdup(predup);
		offs = boffs;
	    }
	}
    } LASTALLOC;
    lexrestore();

    return (char *)s;
}

/* Expand the current word. */

/**/
static int
doexpansion(char *s, int lst, int olst, int explincmd)
{
    int ret = 1;
    LinkList vl;
    char *ss;

    DPUTS(useheap, "BUG: useheap in doexpansion()");
    HEAPALLOC {
	pushheap();
	vl = newlinklist();
	ss = dupstring(s);
	addlinknode(vl, ss);
	prefork(vl, 0);
	if (errflag)
	    goto end;
	if ((lst == COMP_LIST_EXPAND) || (lst == COMP_EXPAND)) {
	    int ng = opts[NULLGLOB];

	    opts[NULLGLOB] = 1;
	    globlist(vl);
	    opts[NULLGLOB] = ng;
	}
	if (errflag)
	    goto end;
	if (empty(vl) || !*(char *)peekfirst(vl))
	    goto end;
	if (peekfirst(vl) == (void *) ss ||
		(olst == COMP_EXPAND_COMPLETE &&
		 !nextnode(firstnode(vl)) && *s == Tilde &&
		 (ss = dupstring(s), filesubstr(&ss, 0)) &&
		 !strcmp(ss, (char *)peekfirst(vl)))) {
	    /* If expansion didn't change the word, try completion if *
	     * expandorcomplete was called, otherwise, just beep.     */
	    if (lst == COMP_EXPAND_COMPLETE)
		docompletion(s, COMP_COMPLETE, explincmd);
	    goto end;
	}
	if (lst == COMP_LIST_EXPAND) {
	    /* Only the list of expansions was requested. */
	    ret = listlist(vl);
	    showinglist = 0;
	    goto end;
	}
	/* Remove the current word and put the expansions there. */
	cs = wb;
	foredel(we - wb);
	while ((ss = (char *)ugetnode(vl))) {
	    ret = 0;
	    untokenize(ss);
	    ss = quotename(ss, NULL);
	    inststr(ss);
#if 0
	    if (nonempty(vl)) {
		spaceinline(1);
		line[cs++] = ' ';
	    }
#endif
	    if (olst != COMP_EXPAND_COMPLETE || nonempty(vl) ||
		(cs && line[cs-1] != '/')) {
		spaceinline(1);
		line[cs++] = ' ';
	    }
	}
      end:
	popheap();
    } LASTALLOC;

    return ret;
}

/* This is called from the lexer to give us word positions. */

/**/
void
gotword(void)
{
    we = ll + 1 - inbufct + (addedx == 2 ? 1 : 0);
    if (cs <= we) {
	wb = ll - wordbeg + addedx;
	zleparse = 0;
    }
}

/* This compares two cpattern lists and returns non-zero if they are
 * equal. */

static int
cmp_cpatterns(Cpattern a, Cpattern b)
{
    while (a) {
	if (a->equiv != b->equiv || memcmp(a->tab, b->tab, 256))
	    return 0;
	a = a->next;
	b = b->next;
    }
    return 1;
}

/* This compares two cmatchers and returns non-zero if they are equal. */

static int
cmp_cmatchers(Cmatcher a, Cmatcher b)
{
    return (a == b ||
	    (a->flags == b->flags &&
	     a->llen == b->llen && a->wlen == b->wlen &&
	     (!a->llen || cmp_cpatterns(a->line, b->line)) &&
	     (a->wlen <= 0 || cmp_cpatterns(a->word, b->word)) &&
	     (!(a->flags & CMF_LEFT) ||
	      (a->lalen == b->lalen &&
	       (!a->lalen || cmp_cpatterns(a->left, b->left)))) &&
	     (!(a->flags & CMF_RIGHT) ||
	      (a->ralen == b->ralen &&
	       (!a->ralen || cmp_cpatterns(a->right, b->right))))));
}

/* Add the given matchers to the bmatcher list. */

static void
add_bmatchers(Cmatcher m)
{
    Cmlist old = bmatchers, *q = &bmatchers, n;

    for (; m; m = m->next) {
	if ((!m->flags && m->wlen > 0 && m->llen > 0) ||
	    (m->flags == CMF_RIGHT && m->wlen == -1 && !m->llen)) {
	    *q = n = (Cmlist) zhalloc(sizeof(struct cmlist));
	    n->matcher = m;
	    q = &(n->next);
	}
    }
    *q = old;
}

/* This is called when the matchers in the mstack have changed to
 * ensure that the bmatchers list contains no matchers not in mstack. */

static void
update_bmatchers(void)
{
    Cmlist p = bmatchers, q = NULL, ms;
    Cmatcher mp;
    int t;

    while (p) {
	t = 0;
	for (ms = mstack; ms && !t; ms = ms->next)
	    for (mp = ms->matcher; mp && !t; mp = mp->next)
		t = cmp_cmatchers(mp, p->matcher);

	p = p->next;
	if (!t) {
	    if (q)
		q->next = p;
	    else
		bmatchers = p;
	}
    }
}

/* This returns a new Cline structure. */

static Cline
get_cline(char *l, int ll, char *w, int wl, char *o, int ol, int fl)
{
    Cline r;

    /* Prefer to take it from the buffer list (freecl), if there
     * is none, allocate a new one. */

    if ((r = freecl))
	freecl = r->next;
    else
	r = (Cline) zhalloc(sizeof(*r));

    r->next = NULL;
    r->line = l; r->llen = ll;
    r->word = w; r->wlen = wl;
    r->orig = o; r->olen = ol;
    r->slen = 0;
    r->flags = fl;
    r->prefix = r->suffix = NULL;
    return r;
}

/* This frees a cline list. */

static void
free_cline(Cline l)
{
    Cline n;

    while (l) {
	n = l->next;
	l->next = freecl;
	freecl = l;
	free_cline(l->prefix);
	free_cline(l->suffix);
	l = n;
    }
}

/* Copy a cline list. */

static Cline
cp_cline(Cline l, int deep)
{
    Cline r = NULL, *p = &r, t, lp = NULL;

    while (l) {
	if ((t = freecl))
	    freecl = t->next;
	else
	    t = (Cline) zhalloc(sizeof(*t));
	memcpy(t, l, sizeof(*t));
	if (deep) {
	    if (t->prefix)
		t->prefix = cp_cline(t->prefix, 0);
	    if (t->suffix)
		t->suffix = cp_cline(t->suffix, 0);
	}
	*p = lp = t;
	p = &(t->next);
	l = l->next;
    }
    *p = NULL;

    return r;
}

/* Calculate the length of a cline and its sub-lists. */

static int
cline_sublen(Cline l)
{
    int len = ((l->flags & CLF_LINE) ? l->llen : l->wlen);

    if (l->olen && !((l->flags & CLF_SUF) ? l->suffix : l->prefix))
	len += l->olen;
    else {
	Cline p;

	for (p = l->prefix; p; p = p->next)
	    len += ((p->flags & CLF_LINE) ? p->llen : p->wlen);
	for (p = l->suffix; p; p = p->next)
	    len += ((p->flags & CLF_LINE) ? p->llen : p->wlen);
    }
    return len;
}

/* Set the lengths in the cline lists. */

static void
cline_setlens(Cline l, int both)
{
    while (l) {
	l->max = cline_sublen(l);
	if (both)
	    l->min = l->max;
	l = l->next;
    }
}

/* This sets the CLF_MATCHED flag in the given clines. */

static void
cline_matched(Cline p)
{
    while (p) {
	p->flags |= CLF_MATCHED;
	cline_matched(p->prefix);
	cline_matched(p->suffix);

	p = p->next;
    }
}

/* This reverts the order of the elements of the given cline list and
 * returns a pointer to the new head. */

static Cline
revert_cline(Cline p)
{
    Cline r = NULL, n;

    while (p) {
	n = p->next;
	p->next = r;
	r = p;
	p = n;
    }
    return r;
}

/* Check if the given pattern matches the given string.             *
 * `in' and `out' are used for {...} classes. In `out' we store the *
 * character number that was matched. In the word pattern this is   *
 * given in `in' so that we can easily test if we found the         *
 * corresponding character. */

/**/
static int
pattern_match(Cpattern p, char *s, unsigned char *in, unsigned char *out)
{
    unsigned char c;

    while (p) {
	c = *((unsigned char *) s);

	if (out)
	    *out = 0;

	if (p->equiv) {
	    if (in) {
		c = p->tab[c];
		if ((*in && *in != c) || (!*in && !c))
		    return 0;
	    } else if (out) {
		if (!(*out = p->tab[c]))
		    return 0;
	    } else if (!p->tab[c])
		return 0;

	    if (in && *in)
		in++;
	    if (out)
		out++;
	} else if (!p->tab[c])
	    return 0;

	s++;
	p = p->next;
    }
    return 1;
}

/* This splits the given string into a list of cline structs, separated
 * at those places where one of the anchors of an `*' pattern was found.
 * plen gives the number of characters on the line that matched this
 * string. In lp we return a pointer to the last cline struct we build. */

static Cline
bld_parts(char *str, int len, int plen, Cline *lp)
{
    Cline ret = NULL, *q = &ret, n;
    Cmlist ms;
    Cmatcher mp;
    int t, op = plen;
    char *p = str;

    while (len) {
	for (t = 0, ms = bmatchers; ms && !t; ms = ms->next) {
	    mp = ms->matcher;
	    if (mp->flags == CMF_RIGHT && mp->wlen == -1 &&
		!mp->llen && len >= mp->ralen && mp->ralen &&
		pattern_match(mp->right, str, NULL, NULL)) {
		int olen = str - p, llen;

		/* We found an anchor, create a new cline. The NEW flag
		 * is set if the characters before the anchor were not
		 * on the line. */
		*q = n = get_cline(NULL, mp->ralen, str, mp->ralen, NULL, 0,
				   ((plen < 0) ? CLF_NEW : 0));

		/* If there were any characters before the anchor, add
		 * them as a cline struct. */

		if (p != str) {
		    llen = (op < 0 ? 0 : op);

		    if (llen > olen)
			llen = olen;
		    n->prefix = get_cline(NULL, llen, p, olen, NULL, 0, 0);
		}
		q = &(n->next);
		str += mp->ralen; len -= mp->ralen;
		plen -= mp->ralen;
		op -= olen;
		p = str;
		t = 1;
	    }
	}
	if (!t) {
	    /* No anchor was found here, skip. */
	    str++; len--;
	    plen--;
	}
    }
    /* This is the cline struct for the remaining string at the end. */

    *q = n = get_cline(NULL, 0, NULL, 0, NULL, 0, (plen < 0 ? CLF_NEW : 0));
    if (p != str) {
	int olen = str - p, llen = (op < 0 ? 0 : op);

	if (llen > olen)
	    llen = olen;
	n->prefix = get_cline(NULL, llen, p, olen, NULL, 0, 0);
    }
    n->next = NULL;

    if (lp)
	*lp = n;

    return ret;
}

/* Global variables used during matching: a char-buffer for the string to
 * use for the match, and two cline lists for the two levels we use. */

static char *matchbuf = NULL;
static int matchbuflen = 0, matchbufadded;

static Cline matchparts, matchlastpart;
static Cline matchsubs, matchlastsub;

/* This initialises the variables above. */

static void
start_match(void)
{
    if (matchbuf)
	*matchbuf = '\0';
    matchbufadded = 0;
    matchparts = matchlastpart = matchsubs = matchlastsub = NULL;
}

/* This aborts a matching, freeing the cline lists build. */

static void
abort_match(void)
{
    free_cline(matchparts);
    free_cline(matchsubs);
    matchparts = matchsubs = NULL;
}

/* This adds a new string in the static char buffer. The arguments are
 * the matcher used (if any), the strings from the line and the word
 * and the length of the string from the word. The last argument is
 * non-zero if we are matching a suffix (where the given string has to 
 * be prepended to the contents of the buffer). */

static void
add_match_str(Cmatcher m, char *l, char *w, int wl, int sfx)
{
    /* Get the string and length to insert: either from the line 
     * or from the match. */
    if (m && (m->flags & CMF_LINE)) {
	wl = m->llen; w = l;
    }
    if (wl) {
	/* Probably resize the buffer. */
	if (matchbuflen - matchbufadded <= wl) {
	    int blen = matchbuflen + wl + 20;
	    char *buf;

	    buf = (char *) zalloc(blen);
	    memcpy(buf, matchbuf, matchbuflen);
	    zfree(matchbuf, matchbuflen);
	    matchbuf = buf;
	    matchbuflen = blen;
	}
	/* Insert the string. */
	if (sfx) {
	    memmove(matchbuf + wl, matchbuf, matchbufadded + 1);
	    memcpy(matchbuf, w, wl);
	} else
	    memcpy(matchbuf + matchbufadded, w, wl);
	matchbufadded += wl;
	matchbuf[matchbufadded] = '\0';
    }
}

/* This adds a cline for a word-part during matching. Arguments are the
 * matcher used, pointers to the line and word strings for the anchor,
 * a pointer to the original line string for the whole part, the string
 * before (or after) the anchor that has not yet been added, the length
 * of the line-string for that, and a flag saying if we are matching a 
 * suffix. */

static void
add_match_part(Cmatcher m, char *l, char *w, int wl,
	       char *o, int ol, char *s, int sl, int osl, int sfx)
{
    Cline p, lp;

    /* If the anchors are equal, we keep only one. */

    if (!strncmp(l, w, wl))
	l = NULL;

    /* Split the new part into parts and turn the last one into a
     * `suffix' if we have a left anchor. */

    p = bld_parts(s, sl, osl, &lp);

    p->flags &= ~CLF_NEW;
    if (m && (m->flags & CMF_LEFT)) {
	lp->flags |= CLF_SUF;
	lp->suffix = lp->prefix;
	lp->prefix = NULL;
    }
    /* cline lists for suffixes are sorted from back to front, so we have
     * to revert the list we got. */
    if (sfx)
	p = revert_cline(lp = p);
    /* Now add the sub-clines we already had. */
    if (matchsubs) {
	if (sfx) {
	    Cline q;

	    if ((q = lp->prefix)) {
		while (q->next)
		    q = q->next;
		q->next = matchsubs;
	    } else
		lp->prefix = matchsubs;

	    matchlastsub->next = NULL;
	} else {
	    matchlastsub->next = p->prefix;
	    p->prefix = matchsubs;
	}
	matchsubs = matchlastsub = NULL;
    }
    /* Store the arguments in the last part-cline. */
    lp->line = l; lp->llen = wl;
    lp->word = w; lp->wlen = wl;
    lp->orig = o; lp->olen = ol;
    lp->flags &= ~CLF_NEW;

    /* Finally, put the new parts on the list. */
    if (matchlastpart)
	matchlastpart->next = p;
    else
	matchparts = p;
    matchlastpart = lp;
}

/* This adds a new sub-cline. Arguments are the matcher and the strings from
 * the line and the word. */

static void
add_match_sub(Cmatcher m, char *l, int ll, char *w, int wl)
{
    int flags;
    Cline n;

    /* Check if we are interested only in the string from the line. */
    if (m && (m->flags & CMF_LINE)) {
	w = NULL; wl = 0;
	flags = CLF_LINE;
    } else
	flags = 0;

    /* And add the cline. */
    if (wl || ll) {
	n = get_cline(l, ll, w, wl, NULL, 0, flags);
	if (matchlastsub)
	    matchlastsub->next = n;
	else
	    matchsubs = n;
	matchlastsub = n;
    }
}

/* This tests if the string from the line l matches the word w. In bp
 * the offset for the brace is returned, in rwlp the length of the
 * matched prefix or suffix, not including the stuff before or after
 * the last anchor is given. When sfx is non-zero matching is done from
 * the ends of the strings backward, if test is zero, the global variables
 * above are used to build the string for the match and the cline. If
 * part is non-zero, we are satisfied if only a part of the line-string
 * is used (and return the length used). */

static int
match_str(char *l, char *w, Brinfo *bpp, int bc, int *rwlp,
	  int sfx, int test, int part)
{
    int ll = strlen(l), lw = strlen(w), oll = ll, olw = lw;
    int il = 0, iw = 0, t, ind, add, he = 0, bpc, obc = bc;
    VARARR(unsigned char, ea, ll + 1);
    char *ow;
    Cmlist ms;
    Cmatcher mp, lm = NULL;
    Brinfo bp = NULL;

    if (!test) {
	start_match();
	bp = *bpp;
    }
    /* Adjust the pointers and get the values for subscripting and
     * incrementing. */

    if (sfx) {
	l += ll; w += lw;
	ind = -1; add = -1;
    } else {
	ind = 0; add = 1;
    }
    /* ow will always point to the beginning (or end) of that sub-string
     * in w that wasn't put in the match-variables yet. */

    ow = w;

    /* If the brace is at the beginning, we have to treat it now. */

    if (!test && bp && bc >= bp->pos) {
	bp->curpos = bc;
	bp = bp->next;
    }
    while (ll && lw) {
	/* First try the matchers. */
	for (mp = NULL, ms = mstack; !mp && ms; ms = ms->next) {
	    for (mp = ms->matcher; mp; mp = mp->next) {
		t = 1;
		if ((lm && lm == mp) ||
		    ((oll == ll || olw == lw) &&
		     (test == 1 || (test && !mp->left && !mp->right)) &&
		     mp->wlen < 0))
		    /* If we were called recursively, don't use `*' patterns
		     * at the beginning (avoiding infinite recursion). */
		    continue;

		if (mp->wlen < 0) {
		    int both, loff, aoff, llen, alen, zoff, moff, ct, ict;
		    char *tp, savl = '\0', savw;
		    Cpattern ap;

		    /* This is for `*' patterns, first initialise some
		     * local variables. */
		    llen = mp->llen;
		    alen = (mp->flags & CMF_LEFT ? mp->lalen : mp->ralen);

		    /* Give up if we don't have enough characters for the
		     * line-string and the anchor. */
		    if (ll < llen + alen || lw < alen)
			continue;

		    if (mp->flags & CMF_LEFT) {
			ap = mp->left; zoff = 0; moff = alen;
			if (sfx) {
			    both = 0; loff = -llen; aoff = -(llen + alen);
			} else {
			    both = 1; loff = alen; aoff = 0;
			}
		    } else {
			ap = mp->right; zoff = alen; moff = 0;
			if (sfx) {
			    both = 1; loff = -(llen + alen); aoff = -alen;
			} else {
			    both = 0; loff = 0; aoff = llen;
			}
		    }
		    /* Try to match the line pattern and the anchor. */
		    if (!pattern_match(mp->line, l + loff, NULL, NULL))
			continue;
		    if (ap) {
			if (!pattern_match(ap, l + aoff, NULL, NULL) ||
			    (both && (!pattern_match(ap, w + aoff, NULL, NULL) ||
				      !match_parts(l + aoff, w + aoff, alen,
						   part))))
				continue;
		    } else if (!both || il || iw)
			continue;

		    /* Fine, now we call ourselves recursively to find the
		     * string matched by the `*'. */
		    if (sfx) {
			savl = l[-(llen + zoff)];
			l[-(llen + zoff)] = '\0';
		    }
		    for (t = 0, tp = w, ct = 0, ict = lw - alen + 1;
			 ict;
			 tp += add, ct++, ict--) {
			if ((both &&
			     (!ap || !test ||
			      !pattern_match(ap, tp + aoff, NULL, NULL))) ||
			    (!both &&
			     pattern_match(ap, tp - moff, NULL, NULL) &&
			     match_parts(l + aoff , tp - moff, alen, part))) {
			    if (sfx) {
				savw = tp[-zoff];
				tp[-zoff] = '\0';
				t = match_str(l - ll, w - lw,
					      NULL, 0, NULL, 1, 2, part);
				tp[-zoff] = savw;
			    } else
				t = match_str(l + llen + moff, tp + moff,
					      NULL, 0, NULL, 0, 1, part);
			    if (t || !both)
				break;
			}
		    }
		    ict = ct;
		    if (sfx)
			l[-(llen + zoff)] = savl;

		    /* Have we found a position in w where the rest of l
		     * matches? */
		    if (!t)
			continue;

		    /* Yes, add the strings and clines if this is a 
		     * top-level call. */
		    if (!test && (!he || (llen + alen))) {
			char *op, *lp, *map, *wap, *wmp;
			int ol;

			if (sfx) {
			    op = w; ol = ow - w; lp = l - (llen + alen);
			    map = tp - alen;
			    if (mp->flags & CMF_LEFT) {
				wap = tp - alen; wmp = tp;
			    } else {
				wap = w - alen; wmp = tp - alen;
			    }
			} else {
			    op = ow; ol = w - ow; lp = l;
			    map = ow;
			    if (mp->flags & CMF_LEFT) {
				wap = w; wmp = w + alen;
			    } else {
				wap = tp; wmp = ow;
			    }
			}
			/* If the matcher says that we are only interested
			 * in the line pattern, we just add that and the
			 * anchor and the string not added yet. Otherwise
			 * we add a new part. */
			if (mp->flags & CMF_LINE) {
			    add_match_str(NULL, NULL, op, ol, sfx);
			    add_match_str(NULL, NULL, lp, llen + alen, sfx);
			    add_match_sub(NULL, NULL, ol, op, ol);
			    add_match_sub(NULL, NULL, llen + alen,
					  lp, llen + alen);
			} else if (sfx) {
			    add_match_str(NULL, NULL,
					  map, ct + ol + alen, sfx);
			    add_match_part(mp, l + aoff, wap, alen,
					   l + loff, llen, op, ol, ol, sfx);
			    add_match_sub(NULL, NULL, 0, wmp, ct);
			} else {
			    add_match_str(NULL, NULL,
					  map, ct + ol + alen, sfx);
			    if (both) {
				add_match_sub(NULL, NULL, ol, op, ol);
				ol = -1;
			    } else
				ct += ol;
			    add_match_part(mp, l + aoff, wap, alen,
					   l + loff, llen, wmp, ct, ol, sfx);
			}
		    }
		    /* Now skip over the matched portion and the anchor. */
		    llen += alen; alen += ict;
		    if (sfx) {
			l -= llen; w -= alen;
		    } else {
			l += llen; w += alen;
		    }
		    ll -= llen; il += llen;
		    lw -= alen; iw += alen;
		    bc += llen;

		    if (!test)
			while (bp &&
			       bc >= (bpc = (useqbr ? bp->qpos : bp->pos))) {
			    bp->curpos = matchbufadded + bpc - bc + obc;
			    bp = bp->next;
			}
		    ow = w;

		    if (!llen && !alen) {
			lm = mp;
			if (he)
			    mp = NULL;
			else
			    he = 1;
		    } else {
			lm = NULL; he = 0;
		    }
		    break;
		} else if (ll >= mp->llen && lw >= mp->wlen) {
		    /* Non-`*'-pattern. */
		    char *tl, *tw;
		    int tll, tlw, til, tiw;

		    /* We do this only if the line- and word-substrings
		     * are not equal. */
		    if (!(mp->flags & (CMF_LEFT | CMF_RIGHT)) &&
			mp->llen == mp->wlen &&
			!(sfx ? strncmp(l - mp->llen, w - mp->wlen, mp->llen) :
			  strncmp(l, w, mp->llen)))
			continue;

		    /* Using local variables to make the following
		     * independent of whether we match a prefix or a
		     * suffix. */
		    if (sfx) {
			tl = l - mp->llen; tw = w - mp->wlen;
			til = ll - mp->llen; tiw = lw - mp->wlen;
			tll = il + mp->llen; tlw = iw + mp->wlen;
		    } else {
			tl = l; tw = w;
			til = il; tiw = iw;
			tll = ll; tlw = lw;
		    }
		    if (mp->flags & CMF_LEFT) {
			/* Try to match the left anchor, if any. */
			if (til < mp->lalen || tiw < mp->lalen)
			    continue;
			else if (mp->left)
			    t = pattern_match(mp->left, tl - mp->lalen,
					      NULL, NULL) &&
				pattern_match(mp->left, tw - mp->lalen,
					      NULL, NULL);
			else
			    t = (!sfx && !il && !iw);
		    }
		    if (mp->flags & CMF_RIGHT) {
			/* Try to match the right anchor, if any. */
			if (tll < mp->llen + mp->ralen ||
			    tlw < mp->wlen + mp->ralen)
			    continue;
			else if (mp->left)
			    t = pattern_match(mp->right,
					      tl + mp->llen - mp->ralen,
					      NULL, NULL) &&
				pattern_match(mp->right,
					      tw + mp->wlen - mp->ralen,
					      NULL, NULL);
			else
			    t = (sfx && !il && !iw);
		    }
		    /* Now try to match the line and word patterns. */
		    if (!t ||
			!pattern_match(mp->line, tl, NULL, ea) ||
			!pattern_match(mp->word, tw, ea, NULL))
			continue;

		    /* Probably add the matched strings. */
		    if (!test) {
			if (sfx)
			    add_match_str(NULL, NULL, w, ow - w, 0);
			else
			    add_match_str(NULL, NULL, ow, w - ow, 0);
			add_match_str(mp, tl, tw, mp->wlen, 0);
			if (sfx)
			    add_match_sub(NULL, NULL, 0, w, ow - w);
			else
			    add_match_sub(NULL, NULL, 0, ow, w - ow);

			add_match_sub(mp, tl, mp->llen, tw, mp->wlen);
		    }
		    if (sfx) {
			l = tl;	w = tw;
		    } else {
			l += mp->llen; w += mp->wlen;
		    }
		    il += mp->llen; iw += mp->wlen;
		    ll -= mp->llen; lw -= mp->wlen;
		    bc += mp->llen;

		    if (!test)
			while (bp &&
			       bc >= (bpc = (useqbr ? bp->qpos : bp->pos))) {
			    bp->curpos = matchbufadded + bpc - bc + obc;
			    bp = bp->next;
			}
		    ow = w;
		    lm = NULL;
		    he = 0;
		    break;
		}
	    }
	}
	if (mp)
	    continue;

	if (l[ind] == w[ind]) {
	    /* No matcher could be used, but the strings have the same
	     * character here, skip over it. */
	    l += add; w += add;
	    il++; iw++;
	    ll--; lw--;
	    bc++;
	    if (!test)
		while (bp && bc >= (useqbr ? bp->qpos : bp->pos)) {
		    bp->curpos = matchbufadded + (sfx ? (ow - w) : (w - ow)) + obc;
		    bp = bp->next;
		}
	    lm = NULL;
	    he = 0;
	} else {
	    /* No matcher and different characters: l does not match w. */
	    if (test)
		return 0;

	    abort_match();

	    return -1;
	}
    }
    /* If this is a recursive call, we just return if l matched w or not. */
    if (test)
	return (part || !ll);

    /* In top-level calls, if ll is non-zero (unmatched portion in l),
     * we have to free the collected clines. */
    if (!part && ll) {
	abort_match();

	return -1;
    }
    if (rwlp)
	*rwlp = iw - (sfx ? ow - w : w - ow);

    /* If we matched a suffix, the anchors stored in the top-clines
     * will be in the wrong clines: shifted by one. Adjust this. */
    if (sfx && matchparts) {
	Cline t, tn, s;

	if (matchparts->prefix || matchparts->suffix) {
	    t = get_cline(NULL, 0, NULL, 0, NULL, 0, 0);
	    t->next = matchparts;
	    if (matchparts->prefix)
		t->prefix = (Cline) 1;
	    else
		t->suffix = (Cline) 1;
	    matchparts = t;
	}
	for (t = matchparts; (tn = t->next); t = tn) {
	    s = (tn->prefix ? tn->prefix : tn->suffix);
	    if (t->suffix)
		t->suffix = s;
	    else
		t->prefix = s;
	}
	t->prefix = t->suffix = NULL;
    }
    /* Finally, return the number of matched characters. */

    *bpp = bp;
    return (part ? il : iw);
}

/* Wrapper for match_str(), only for a certain length and only doing
 * the test. */

/**/
static int
match_parts(char *l, char *w, int n, int part)
{
    char lsav = l[n], wsav = w[n];
    int ret;

    l[n] = w[n] = '\0';
    ret = match_str(l, w, NULL, 0, NULL, 0, 1, part);
    l[n] = lsav;
    w[n] = wsav;

    return ret;
}

/* Check if the word w is matched by the strings in pfx and sfx (the prefix
 * and the suffix from the line) or the pattern cp. In clp a cline list for
 * w is returned.
 * qu is non-zero if the words has to be quoted before processed any further.
 * bpl and bsl are used to report the positions where the brace-strings in
 * the prefix and the suffix have to be re-inserted if this match is inserted
 * in the line.
 * The return value is the string to use as a completion or NULL if the prefix
 * and the suffix don't match the word w. */

/**/
char *
comp_match(char *pfx, char *sfx, char *w, Patprog cp, Cline *clp, int qu,
	   Brinfo *bpl, int bcp, Brinfo *bsl, int bcs, int *exact)
{
    char *r = NULL;

    if (cp) {
	/* We have a globcomplete-like pattern, just use that. */
	int wl;

	r = w;
	if (!pattry(cp, r))
	    return NULL;
    
	r = (qu ? quotename(r, NULL) : dupstring(r));
	if (qu == 2 && r[0] == '\\' && r[1] == '~')
	    chuck(r);
	/* We still break it into parts here, trying to build a sensible
	 * cline list for these matches, too. */
	w = dupstring(w);
	wl = strlen(w);
	*clp = bld_parts(w, wl, wl, NULL);
	*exact = 0;
    } else {
	Cline pli, plil;
	int mpl, rpl, wl;

	w = (qu ? quotename(w, NULL) : dupstring(w));
	if (qu == 2 && w[0] == '\\' && w[1] == '~')
	    chuck(w);

	wl = strlen(w);

	/* Always try to match the prefix. */

	useqbr = qu;
	if ((mpl = match_str(pfx, w, bpl, bcp, &rpl, 0, 0, 0)) < 0)
	    return NULL;

	if (sfx && *sfx) {
	    int wpl = matchbufadded, msl, rsl;
	    VARARR(char, wpfx, wpl);
	    Cline mli, mlil;

	    /* We also have a suffix to match, so first save the
	     * contents of the global matching variables. */
	    memcpy(wpfx, matchbuf, wpl);
	    if (matchsubs) {
		Cline tmp = get_cline(NULL, 0, NULL, 0, NULL, 0, 0);

		tmp->prefix = matchsubs;
		if (matchlastpart)
		    matchlastpart->next = tmp;
		else
		    matchparts = tmp;
	    }
	    pli = matchparts;
	    plil = matchlastpart;

	    /* The try to match the suffix. */

	    if ((msl = match_str(sfx, w + mpl, bsl, bcs, &rsl, 1, 0, 0)) < 0) {
		free_cline(pli);

		return NULL;
	    }
	    /* Matched, so add the string in the middle and the saved
	     * string for the prefix, and build a combined cline list
	     * for the prefix and the suffix. */
	    if (matchsubs) {
		Cline tmp = get_cline(NULL, 0, NULL, 0, NULL, 0, CLF_SUF);

		tmp->suffix = matchsubs;
		if (matchlastpart)
		    matchlastpart->next = tmp;
		else
		    matchparts = tmp;
	    }
	    add_match_str(NULL, NULL, w + rpl, wl - rpl - rsl, 1);
	    add_match_str(NULL, NULL, wpfx, wpl, 1);

	    mli = bld_parts(w + rpl, wl - rpl - rsl,
			    (mpl - rpl) + (msl - rsl), &mlil);
	    mlil->flags |= CLF_MID;
	    mlil->slen = msl - rsl;
	    mlil->next = revert_cline(matchparts);

	    if (plil)
		plil->next = mli;
	    else
		pli = mli;
	} else {
	    /* Only a prefix, add the string and a part-cline for it. */
	    add_match_str(NULL, NULL, w + rpl, wl - rpl, 0);

	    add_match_part(NULL, NULL, NULL, 0, NULL, 0, w + rpl, wl - rpl,
			   mpl - rpl, 0);
	    pli = matchparts;
	}
	r = dupstring(matchbuf ? matchbuf : "");

	*clp = pli;

	/* Test if the string built is equal to the one from the line. */
	if (sfx && *sfx) {
	    int pl = strlen(pfx);

	    *exact = (!strncmp(pfx, w, pl) && !strcmp(sfx, w + pl));
	} else
	    *exact = !strcmp(pfx, w);
    }
    if (!qu)
	hasunqu = 1;

    return r;
}

/* This builds all the possible line patterns for the pattern pat in the
 * buffer line. Initially line is the same as lp, but during recursive
 * calls lp is incremented for storing successive characters. Whenever
 * a full possible string is build, we test if this line matches the
 * string given by wlen and word. The in argument contains the characters
 * to use for the correspondence classes, it was filled by a call to 
 * pattern_match() in the calling function.
 * The return value is the length of the string matched in the word, it
 * is zero if we couldn't build a line that matches the word. */

static int
bld_line(Cpattern pat, char *line, char *lp,
	 char *word, int wlen, unsigned char *in, int sfx)
{
    if (pat) {
	/* Still working on the pattern. */

	int i, l;
	unsigned char c = 0;

	/* Get the number of the character for a correspondence class
	 * if it has a correxponding class. */
	if (pat->equiv)
	    if ((c = *in))
		in++;

	/* Walk through the table in the pattern and try the characters
	 * that may appear in the current position. */
	for (i = 0; i < 256; i++)
	    if ((pat->equiv && c) ? (c == pat->tab[i]) : pat->tab[i]) {
		*lp = i;
		/* We stored the character, now call ourselves to build
		 * the rest. */
		if ((l = bld_line(pat->next, line, lp + 1, word, wlen,
				  in, sfx)))
		    return l;
	    }
    } else {
	/* We reached the end, i.e. the line string is fully build, now
	 * see if it matches the given word. */

	Cmlist ms;
	Cmatcher mp;
	int l = lp - line, t, rl = 0, ind, add;
	VARARR(unsigned char, ea, l + 1);

	/* Quick test if the strings are exactly the same. */
	if (l == wlen && !strncmp(line, word, l))
	    return l;

	if (sfx) {
	    line = lp; word += wlen;
	    ind = -1; add = -1;
	} else {
	    ind = 0; add = 1;
	}
	/* We loop through the whole line string built. */
	while (l && wlen) {
	    if (word[ind] == line[ind]) {
		/* The same character in both strings, skip over. */
		line += add; word += add;
		l--; wlen--; rl++;
	    } else {
		t = 0;
		for (ms = bmatchers; ms && !t; ms = ms->next) {
		    mp = ms->matcher;
		    if (!mp->flags && mp->wlen <= wlen && mp->llen <= l &&
			pattern_match(mp->line, (sfx ? line - mp->llen : line),
				      NULL, ea) &&
			pattern_match(mp->word, (sfx ? word - mp->wlen : word),
				      ea, NULL)) {
			/* Both the line and the word pattern matched,
			 * now skip over the matched portions. */
			if (sfx) {
			    line -= mp->llen; word -= mp->wlen;
			} else {
			    line += mp->llen; word += mp->wlen;
			}
			l -= mp->llen; wlen -= mp->wlen; rl += mp->wlen;
			t = 1;
		    }
		}
		if (!t)
		    /* Didn't match, give up. */
		    return 0;
	    }
	}
	if (!l)
	    /* Unmatched portion in the line built, return matched length. */
	    return rl;
    }
    return 0;
}

/* This builds a string that may be put on the line that fully matches the
 * given strings. The return value is NULL if no such string could be built
 * or that string in local static memory, dup it. */

static char *
join_strs(int la, char *sa, int lb, char *sb)
{
    static char *rs = NULL;
    static int rl = 0;

    VARARR(unsigned char, ea, (la > lb ? la : lb) + 1);
    Cmlist ms;
    Cmatcher mp;
    int t, bl, rr = rl;
    char *rp = rs;

    while (la && lb) {
	if (*sa != *sb) {
	    /* Different characters, try the matchers. */
	    for (t = 0, ms = bmatchers; ms && !t; ms = ms->next) {
		mp = ms->matcher;
		if (!mp->flags && mp->wlen > 0 && mp->llen > 0 &&
		    mp->wlen <= la && mp->wlen <= lb) {
		    /* The pattern has no anchors and the word
		     * pattern fits, try it. */
		    if ((t = pattern_match(mp->word, sa, NULL, ea)) ||
			pattern_match(mp->word, sb, NULL, ea)) {
			/* It matched one of the strings, t says which one. */
			VARARR(char, line, mp->llen + 1);
			char **ap, **bp;
			int *alp, *blp;

			if (t) {
			    ap = &sa; alp = &la;
			    bp = &sb; blp = &lb;
			} else {
			    ap = &sb; alp = &lb;
			    bp = &sa; blp = &la;
			}
			/* Now try to build a string that matches the other
			 * string. */
			if ((bl = bld_line(mp->line, line, line,
					   *bp, *blp, ea, 0))) {
			    /* Found one, put it into the return string. */
			    line[mp->llen] = '\0';
			    if (rr <= mp->llen) {
				char *or = rs;

				rs = realloc(rs, (rl += 20));
				rr += 20;
				rp += rs - or;
			    }
			    memcpy(rp, line, mp->llen);
			    rp += mp->llen; rr -= mp->llen;
			    *ap += mp->wlen; *alp -= mp->wlen;
			    *bp += bl; *blp -= bl;
			    t = 1;
			} else
			    t = 0;
		    }
		}
	    }
	    if (!t)
		break;
	} else {
	    /* Same character, just take it. */
	    if (rr <= 1) {
		char *or = rs;

		rs = realloc(rs, (rl += 20));
		rr += 20;
		rp += rs - or;
	    }
	    *rp++ = *sa; rr--;
	    sa++; sb++;
	    la--; lb--;
	}
    }
    if (la || lb)
	return NULL;

    *rp = '\0';

    return rs;
}

/* This compares the anchors stored in two top-level clines. */

static int
cmp_anchors(Cline o, Cline n, int join)
{
    int line = 0;
    char *j;

    /* First try the exact strings. */
    if ((!(o->flags & CLF_LINE) && o->wlen == n->wlen &&
	 (!o->word || !strncmp(o->word, n->word, o->wlen))) ||
	(line = ((!o->line && !n->line && !o->wlen && !n->wlen) ||
		 (o->llen == n->llen && o->line && n->line &&
		  !strncmp(o->line, n->line, o->llen))))) {
	if (line) {
	    o->flags |= CLF_LINE;
	    o->word = NULL;
	    n->wlen = 0;
	}
	return 1;
    }
    /* Didn't work, try to build a string matching both anchors. */
    if (join && !(o->flags & CLF_JOIN) && o->word && n->word &&
	(j = join_strs(o->wlen, o->word, n->wlen, n->word))) {
	o->flags |= CLF_JOIN;
	o->wlen = strlen(j);
	o->word = dupstring(j);

	return 2;
    }
    return 0;
}

/* Below is the code to join two cline lists. This struct is used to walk
 * through a sub-list. */

typedef struct cmdata *Cmdata;

struct cmdata {
    Cline cl, pcl;
    char *str, *astr;
    int len, alen, olen, line;
};

/* This is used to ensure that a cmdata struct contains usable data.
 * The return value is non-zero if we reached the end. */

static int
check_cmdata(Cmdata md, int sfx)
{
    /* We will use the str and len fields to contain the next sub-string
     * in the list. If len is zero, we have to use the next cline. */
    if (!md->len) {
	/* If there is none, we reached the end. */
	if (!md->cl)
	    return 1;

	/* Otherwise, get the string. Only the line-string or both.
	 * We also have to adjust the pointer if this is for a suffix. */
	if (md->cl->flags & CLF_LINE) {
	    md->line = 1;
	    md->len = md->cl->llen;
	    md->str = md->cl->line;
	} else {
	    md->line = 0;
	    md->len = md->olen = md->cl->wlen;
	    if ((md->str = md->cl->word) && sfx)
		md->str += md->len;
	    md->alen = md->cl->llen;
	    if ((md->astr = md->cl->line) && sfx)
		md->astr += md->alen;
	}
	md->pcl = md->cl;
	md->cl = md->cl->next;
    }
    return 0;
}

/* This puts the not-yet-matched portion back into the last cline and 
 * returns that. */

static Cline
undo_cmdata(Cmdata md, int sfx)
{
    Cline r = md->pcl;

    if (md->line) {
	r->word = NULL;
	r->wlen = 0;
	r->flags |= CLF_LINE;
	r->llen = md->len;
	r->line = md->str - (sfx ? md->len : 0);
    } else if (md->len != md->olen) {
	r->wlen = md->len;
	r->word = md->str - (sfx ? md->len : 0);
    }
    return r;
}

/* This tries to build a string matching a sub-string in a sub-cline
 * that could not be matched otherwise. */

static Cline
join_sub(Cmdata md, char *str, int len, int *mlen, int sfx, int join)
{
    if (!check_cmdata(md, sfx)) {
	char *ow = str, *nw = md->str;
	int ol = len, nl = md->len;
	Cmlist ms;
	Cmatcher mp;
	VARARR(unsigned char, ea, (ol > nl ? ol : nl) + 1);
	int t;

	if (sfx) {
	    ow += ol; nw += nl;
	}
	for (t = 0, ms = bmatchers; ms && !t; ms = ms->next) {
	    mp = ms->matcher;
	    /* We use only those patterns that match a non-empty
	     * string in both the line and the word and that have
	     * no anchors. */
	    if (!mp->flags && mp->wlen > 0 && mp->llen > 0) {
		/* We first test, if the old string matches already the
		 * new one. */
		if (mp->llen <= ol && mp->wlen <= nl &&
		    pattern_match(mp->line, ow - (sfx ? mp->llen : 0),
				  NULL, ea) &&
		    pattern_match(mp->word, nw - (sfx ? mp->wlen : 0),
				  ea, NULL)) {
		    /* It did, update the contents of the cmdata struct
		     * and return a cline for the matched part. */
		    if (sfx)
			md->str -= mp->wlen;
		    else
			md->str += mp->wlen;
		    md->len -= mp->wlen;
		    *mlen = mp->llen;

		    return get_cline(NULL, 0, ow - (sfx ? mp->llen : 0),
				     mp->llen, NULL, 0, 0);
		}
		/* Otherwise we will try to build a string that matches
		 * both strings. But try the pattern only if the word-
		 * pattern matches one of the strings. */
		if (join && mp->wlen <= ol && mp->wlen <= nl &&
		    ((t = pattern_match(mp->word, ow - (sfx ? mp->wlen : 0),
				       NULL, ea)) ||
		     pattern_match(mp->word, nw - (sfx ? mp->wlen : 0),
				   NULL, ea))) {
		    VARARR(char, line, mp->llen + 1);
		    int bl;

		    /* Then build all the possible lines and see
		     * if one of them matches the other string. */
		    if ((bl = bld_line(mp->line, line, line,
				       (t ? nw : ow), (t ? nl : ol),
				       ea, sfx))) {
			/* Yep, one of the lines matched the other
			 * string. */
			line[mp->llen] = '\0';

			if (t) {
			    ol = mp->wlen; nl = bl;
			} else {
			    ol = bl; nl = mp->wlen;
			}
			if (sfx)
			    md->str -= nl;
			else
			    md->str += nl;
			md->len -= nl;
			*mlen = ol;

			return get_cline(NULL, 0, dupstring(line), mp->llen,
					 NULL, 0, CLF_JOIN);
		    }
		}
	    }
	}
    }
    return NULL;
}

/* This is used to match a sub-string in a sub-cline. The length of the
 * matched portion is returned. This tests only for exact equality. */

static int
sub_match(Cmdata md, char *str, int len, int sfx)
{
    int ret = 0, l, ind, add;
    char *p, *q;

    if (sfx) {
	str += len;
	ind = -1; add = -1;
    } else {
	ind = 0; add = 1;
    }
    /* str and len describe the old string, in md we have the new one. */
    while (len) {
	if (check_cmdata(md, sfx))
	    return ret;

	for (l = 0, p = str, q = md->str;
	     l < len && l < md->len && p[ind] == q[ind];
	     l++, p += add, q += add);

	if (l) {
	    /* There was a common prefix, use it. */
	    md->len -= l; len -= l;
	    if (sfx) {
		md->str -= l; str -= l;
	    } else {
		md->str += l; str += l;
	    }
	    ret += l;
	} else if (md->line || md->len != md->olen || !md->astr)
	    return ret;
	else {
	    /* We still have the line string to try. */
	    md->line = 1;
	    md->len = md->alen;
	    md->str = md->astr;
	}
    }
    return ret;
}

/* This is used to build a common prefix or suffix sub-list. If requested
 * it returns the unmatched cline lists in orest and nrest. */

static void
join_psfx(Cline ot, Cline nt, Cline *orest, Cline *nrest, int sfx)
{
    Cline p = NULL, o, n;
    struct cmdata md, omd;
    char **sstr = NULL;
    int len, join = 0, line = 0, *slen = NULL;

    if (sfx) {
	o = ot->suffix; n = nt->suffix;
    } else {
	o = ot->prefix;	n = nt->prefix;
    }
    if (!o) {
	if (orest)
	    *orest = NULL;
	if (nrest)
	    *nrest = n;

	return;
    }
    if (!n) {
	if (sfx)
	    ot->suffix = NULL;
	else
	    ot->prefix = NULL;

	if (orest)
	    *orest = o;
	else
	    free_cline(o);
	if (nrest)
	    *nrest = NULL;
	return;
    }
    md.cl = n;
    md.len = 0;

    /* Walk through the old list. */
    while (o) {
	join = 0;
	memcpy(&omd, &md, sizeof(struct cmdata));

	/* We first get the length of the prefix equal in both strings. */
	if (o->flags & CLF_LINE) {
	    if ((len = sub_match(&md, o->line, o->llen, sfx)) != o->llen) {
		join = 1; line = 1; slen = &(o->llen); sstr = &(o->line);
	    }
	} else if ((len = sub_match(&md, o->word, o->wlen, sfx)) != o->wlen) {
	    if (o->line) {
		memcpy(&md, &omd, sizeof(struct cmdata));
		o->flags |= CLF_LINE | CLF_DIFF;

		continue;
	    }
	    join = 1; line = 0; slen = &(o->wlen); sstr = &(o->word);
	}
	if (join) {
	    /* There is a rest that is different in the two lists,
	     * we try to build a new cline matching both strings. */
	    Cline joinl;
	    int jlen;

	    if ((joinl = join_sub(&md, *sstr + len, *slen - len,
				  &jlen, sfx, !(o->flags & CLF_JOIN)))) {
		/* We have one, insert it into the list. */
		joinl->flags |= CLF_DIFF;
		if (len + jlen != *slen) {
		    Cline rest;

		    rest = get_cline(NULL, 0, *sstr + (sfx ? 0 : len + jlen),
				     *slen - len - jlen, NULL, 0, 0);

		    rest->next = o->next;
		    joinl->next = rest;
		} else
		    joinl->next = o->next;

		if (len) {
		    if (sfx)
			*sstr += *slen - len;
		    *slen = len;
		    o->next = joinl;
		} else {
		    o->next = NULL;
		    free_cline(o);
		    if (p)
			p->next = joinl;
		    else if (sfx)
			ot->suffix = joinl;
		    else
			ot->prefix = joinl;
		}
		o = joinl;
		join = 0;
	    }
	}
	if (join) {
	    /* We couldn't build a cline for a common string, so we
	     * cut the list here. */
	    if (len) {
		Cline r;

		if (orest) {
		    if (line)
			r = get_cline(o->line + len, *slen - len,
				      NULL, 0, NULL, 0, o->flags);
		    else
			r = get_cline(NULL, 0, o->word + len, *slen - len,
				      NULL, 0, o->flags);

		    r->next = o->next;
		    *orest = r;

		    *slen = len;
		    o->next = NULL;
		} else {
		    if (sfx)
			*sstr += *slen - len;
		    *slen = len;
		    free_cline(o->next);
		    o->next = NULL;
		}
	    } else {
		if (p)
		    p->next = NULL;
		else if (sfx)
		    ot->suffix = NULL;
		else
		    ot->prefix = NULL;

		if (orest)
		    *orest = o;
		else
		    free_cline(o);
	    }
	    if (!orest || !nrest)
		ot->flags |= CLF_MISS;

	    if (nrest)
		*nrest = undo_cmdata(&md, sfx);

	    return;
	}
	p = o;
	o = o->next;
    }
    if (md.len || md.cl)
	ot->flags |= CLF_MISS;
    if (orest)
	*orest = NULL;
    if (nrest)
	*nrest = undo_cmdata(&md, sfx);
}

/* This builds the common prefix and suffix for a mid-cline -- the one
 * describing the place where the prefix and the suffix meet. */

static void
join_mid(Cline o, Cline n)
{
    if (o->flags & CLF_JOIN) {
	/* The JOIN flag is set in the old cline struct if it was
	 * already joined with another one. In this case the suffix
	 * field contains the suffix from previous calls. */
	Cline nr;

	join_psfx(o, n, NULL, &nr, 0);

	n->suffix = revert_cline(nr);

	join_psfx(o, n, NULL, NULL, 1);
    } else {
	/* This is the first time for both structs, so the prefix field
	 * contains the whole sub-list. */
	Cline or, nr;

	o->flags |= CLF_JOIN;

	/* We let us give both rests and use them as the suffixes. */
	join_psfx(o, n, &or, &nr, 0);

	if (or)
	    or->llen = (o->slen > or->wlen ? or->wlen : o->slen);
	o->suffix = revert_cline(or);
	n->suffix = revert_cline(nr);

	join_psfx(o, n, NULL, NULL, 1);
    }
    n->suffix = NULL;
}

/* This turns the sequence of anchor cline structs from b to e into a
 * prefix sequence, puts it before the prefix of e and then tries to
 * join that with the prefix of a.
 * This is needed if some matches had a anchor match spec and others
 * didn't. */

static void
sub_join(Cline a, Cline b, Cline e, int anew)
{
    if (!e->suffix && a->prefix) {
	Cline op = e->prefix, n = NULL, *p = &n, t, ca;
	int min = 0, max = 0;

	for (; b != e; b = b->next) {
	    if ((*p = t = b->prefix)) {
		while (t->next)
		    t = t->next;
		p = &(t->next);
	    }
	    b->suffix = b->prefix = NULL;
	    b->flags &= ~CLF_SUF;
	    min += b->min;
	    max += b->max;
	    *p = b;
	    p = &(b->next);
	}
	*p = e->prefix;
	ca = a->prefix;

	while (n != op) {
	    e->prefix = cp_cline(n, 0);
	    a->prefix = cp_cline(ca, 0);

	    if (anew) {
		join_psfx(e, a, NULL, NULL, 0);
		if (e->prefix) {
		    e->min += min;
		    e->max += max;
		    break;
		}
	    } else {
		join_psfx(e, a, NULL, NULL, 0);
		if (a->prefix) {
		    a->min += min;
		    a->max += max;
		    break;
		}
	    }
	    min -= n->min;
	    max -= n->max;

	    n = n->next;
	}
    }
}

/* This simplifies the cline list given as the first argument so that
 * it also matches the second list. */

static Cline
join_clines(Cline o, Cline n)
{
    cline_setlens(n, 1);

    /* First time called, just return the new list. On further invocations
     * we will get it as the first argument. */
    if (!o)
	return n;
    else {
	Cline oo = o, nn = n, po = NULL, pn = NULL;

	/* Walk through the lists. */
	while (o && n) {
	    /* If one of them describes a new part and the other one does
	     * not, synchronise them by searching an old part in the
	     * other list. */
	    if ((o->flags & CLF_NEW) && !(n->flags & CLF_NEW)) {
		Cline t, tn;

		for (t = o; (tn = t->next) && (tn->flags & CLF_NEW); t = tn);
		if (tn && cmp_anchors(tn, n, 0)) {
		    sub_join(n, o, tn, 1);

		    if (po)
			po->next = tn;
		    else
			oo = tn;
		    t->next = NULL;
		    free_cline(o);
		    o = tn;
		    o->flags |= CLF_MISS;
		    continue;
		}
	    }
	    if (!(o->flags & CLF_NEW) && (n->flags & CLF_NEW)) {
		Cline t, tn;

		for (t = n; (tn = t->next) && (tn->flags & CLF_NEW); t = tn);
		if (tn && cmp_anchors(o, tn, 0)) {
		    sub_join(o, n, tn, 0);

		    n = tn;
		    o->flags |= CLF_MISS;
		    continue;
		}
	    }
	    /* Almost the same as above, but for the case that they
	     * describe different types of parts (prefix, suffix, or mid). */
	    if ((o->flags & (CLF_SUF | CLF_MID)) !=
		(n->flags & (CLF_SUF | CLF_MID))) {
		Cline t, tn;

		for (t = n;
		     (tn = t->next) &&
			 (tn->flags & (CLF_SUF | CLF_MID)) !=
			 (o->flags  & (CLF_SUF | CLF_MID));
		     t = tn);
		if (tn && cmp_anchors(o, tn, 1)) {
		    sub_join(o, n, tn, 0);

		    n = tn;
		    continue;
		}
		for (t = o;
		     (tn = t->next) &&
			 (tn->flags & (CLF_SUF | CLF_MID)) !=
			 (n->flags  & (CLF_SUF | CLF_MID));
		     t = tn);
		if (tn && cmp_anchors(tn, n, 1)) {
		    sub_join(n, o, tn, 1);
		    if (po)
			po->next = tn;
		    else
			oo = tn;
		    t->next = NULL;
		    free_cline(o);
		    o = tn;
		    continue;
		}
		if (o->flags & CLF_MID) {
		    o->flags = (o->flags & ~CLF_MID) | (n->flags & CLF_SUF);
		    if (n->flags & CLF_SUF) {
			free_cline(o->prefix);
			o->prefix = NULL;
		    } else {
			free_cline(o->suffix);
			o->suffix = NULL;
		    }
		}
		break;
	    }
	    /* Now see if they have matching anchors. If not, cut the list. */
	    if (!(o->flags & CLF_MID) && !cmp_anchors(o, n, 1)) {
		Cline t, tn;

		for (t = n; (tn = t->next) && !cmp_anchors(o, tn, 1); t = tn);

		if (tn) {
		    sub_join(o, n, tn, 0);

		    n = tn;
		    o->flags |= CLF_MISS;
		    continue;
		} else {
		    for (t = o; (tn = t->next) && !cmp_anchors(n, tn, 1);
			 t = tn);

		    if (tn) {
			sub_join(n, o, tn, 1);

			if (po)
			    po->next = tn;
			else
			    oo = tn;
			o = tn;
			o->flags |= CLF_MISS;
			continue;
		    } else {
			if (o->flags & CLF_SUF)
			    break;

			o->word = o->line = o->orig = NULL;
			o->wlen = 0;
			free_cline(o->next);
			o->next = NULL;
			o->flags |= CLF_MISS;
		    }
		}
	    }
	    /* Ok, they are equal, now copy the information about the
             * original string if needed, calculate minimum and maximum
	     * lengths, and join the sub-lists. */
	    if (!o->orig && !o->olen) {
		o->orig = n->orig;
		o->olen = n->olen;
	    }
	    if (n->min < o->min)
		o->min = n->min;
	    if (n->max > o->max)
		o->max = n->max;
	    if (o->flags & CLF_MID)
		join_mid(o, n);
	    else
		join_psfx(o, n, NULL, NULL, (o->flags & CLF_SUF));

	    po = o;
	    o = o->next;
	    pn = n;
	    n = n->next;
	}
	/* Free the rest of the old list. */
	if (o) {
	    if (po)
		po->next = NULL;
	    else
		oo = NULL;

	    free_cline(o);
	}
	free_cline(nn);

	return oo;
    }
}

/* This adds all the data we have for a match. */

/**/
Cmatch
add_match_data(int alt, char *str, Cline line,
	       char *ipre, char *ripre, char *isuf,
	       char *pre, char *prpre,
	       char *ppre, Cline pline,
	       char *psuf, Cline sline,
	       char *suf, int flags, int exact)
{
    Cmatch cm;
    Aminfo ai = (alt ? fainfo : ainfo);
    int palen, salen, qipl, ipl, pl, ppl, qisl, isl, psl;
    int sl, lpl, lsl, ml;

    palen = salen = qipl = ipl = pl = ppl = qisl = isl = psl = 0;

    DPUTS(!line, "BUG: add_match_data() without cline");

    cline_matched(line);
    if (pline)
	cline_matched(pline);
    if (sline)
	cline_matched(sline);

    /* If there is a path suffix, we build a cline list for it and
     * append it to the list for the match itself. */
    if (!sline && psuf)
	salen = (psl = strlen(psuf));
    if (isuf)
	salen += (isl = strlen(isuf));
    if (qisuf)
	salen += (qisl = strlen(qisuf));

    if (salen) {
	char *asuf = (char *) zhalloc(salen);
	Cline pp, p, s, sl = NULL;
	

	if (psl)
	    memcpy(asuf, psuf, psl);
	if (isl)
	    memcpy(asuf + psl, isuf, isl);
	if (qisl)
	    memcpy(asuf + psl + isl, qisuf, qisl);

	for (pp = NULL, p = line; p->next; pp = p, p = p->next);

	if (salen > qisl) {
	    s = bld_parts(asuf, salen - qisl, salen - qisl, &sl);

	    if (sline) {
		Cline sp;

		sline = cp_cline(sline, 1);

		for (sp = sline; sp->next; sp = sp->next);
		sp->next = s;
		s = sline;
	    }
	    if (!(p->flags & (CLF_SUF | CLF_MID)) &&
		!p->llen && !p->wlen && !p->olen) {
		if (p->prefix) {
		    Cline q;

		    for (q = p->prefix; q->next; q = q->next);
		    q->next = s->prefix;
		    s->prefix = p->prefix;
		    p->prefix = NULL;
		}
		s->flags |= (p->flags & CLF_MATCHED);
		free_cline(p);
		if (pp)
		    pp->next = s;
		else
		    line = s;
	    } else
		p->next = s;
	}
	if (qisl) {
	    Cline qsl = bld_parts(asuf + psl + isl, qisl, qisl, NULL);

	    qsl->flags |= CLF_SUF;
	    qsl->suffix = qsl->prefix;
	    qsl->prefix = NULL;
	    if (sl)
		sl->next = qsl;
	    else if (sline) {
		Cline sp;

		sline = cp_cline(sline, 1);

		for (sp = sline; sp->next; sp = sp->next);
		sp->next = qsl;
		p->next = sline;
	    } else
		p->next = qsl;
	}
    } else if (sline) {
	Cline p;

	for (p = line; p->next; p = p->next);
	p->next = cp_cline(sline, 1);
    }
    /* The prefix is handled differently because the completion code
     * is much more eager to insert the -P prefix than it is to insert
     * the -S suffix. */
    if (qipre)
	palen = (qipl = strlen(qipre));
    if (ipre)
	palen += (ipl = strlen(ipre));
    if (pre)
	palen += (pl = strlen(pre));
    if (!pline && ppre)
	palen += (ppl = strlen(ppre));

    if (pl) {
	if (ppl || pline) {
	    Cline lp, p;

	    if (pline)
		for (p = cp_cline(pline, 1), lp = p; lp->next; lp = lp->next);
	    else
		p = bld_parts(ppre, ppl, ppl, &lp);

	    if (lp->prefix && !(line->flags & (CLF_SUF | CLF_MID)) &&
		!p->llen && !p->wlen && !p->olen) {
		Cline lpp;

		for (lpp = lp->prefix; lpp->next; lpp = lpp->next);

		lpp->next = line->prefix;
		line->prefix = lp->prefix;
		lp->prefix = NULL;

		free_cline(lp);

		if (p != lp) {
		    Cline q;

		    for (q = p; q->next != lp; q = q->next);

		    q->next = line;
		    line = p;
		}
	    } else {
		lp->next = line;
		line = p;
	    }
	}
	if (pl) {
	    Cline lp, p = bld_parts(pre, pl, pl, &lp);

	    lp->next = line;
	    line = p;
	}
	if (ipl) {
	    Cline lp, p = bld_parts(ipre, ipl, ipl, &lp);

	    lp->next = line;
	    line = p;
	}
	if (qipl) {
	    Cline lp, p = bld_parts(qipre, qipl, qipl, &lp);

	    lp->next = line;
	    line = p;
	}
    } else if (palen || pline) {
	Cline p, lp;

	if (palen) {
	    char *apre = (char *) zhalloc(palen);

	    if (qipl)
		memcpy(apre, qipre, qipl);
	    if (ipl)
		memcpy(apre + qipl, ipre, ipl);
	    if (pl)
		memcpy(apre + qipl + ipl, pre, pl);
	    if (ppl)
		memcpy(apre + qipl + ipl + pl, ppre, ppl);

	    p = bld_parts(apre, palen, palen, &lp);

	    if (pline)
		for (lp->next = cp_cline(pline, 1); lp->next; lp = lp->next);
	} else
	    for (p = lp = cp_cline(pline, 1); lp->next; lp = lp->next);

	if (lp->prefix && !(line->flags & (CLF_SUF | CLF_MID)) &&
	    !p->llen && !p->wlen && !p->olen) {
	    Cline lpp;

	    for (lpp = lp->prefix; lpp->next; lpp = lpp->next);

	    lpp->next = line->prefix;
	    line->prefix = lp->prefix;
	    lp->prefix = NULL;

	    free_cline(lp);

	    if (p != lp) {
		Cline q;

		for (q = p; q->next != lp; q = q->next);

		q->next = line;
		line = p;
	    }
	} else {
	    lp->next = line;
	    line = p;
	}
    }
    /* Allocate and fill the match structure. */
    cm = (Cmatch) zhalloc(sizeof(struct cmatch));
    cm->str = str;
    cm->ppre = (ppre && *ppre ? ppre : NULL);
    cm->psuf = (psuf && *psuf ? psuf : NULL);
    cm->prpre = ((flags & CMF_FILE) && prpre && *prpre ? prpre : NULL);
    if (qipre && *qipre)
	cm->ipre = (ipre && *ipre ? dyncat(qipre, ipre) : dupstring(qipre));
    else
	cm->ipre = (ipre && *ipre ? ipre : NULL);
    cm->ripre = (ripre && *ripre ? ripre : NULL);
    if (qisuf && *qisuf)
	cm->isuf = (isuf && *isuf ? dyncat(isuf, qisuf) : dupstring(qisuf));
    else
	cm->isuf = (isuf && *isuf ? isuf : NULL);
    cm->pre = pre;
    cm->suf = suf;
    cm->flags = flags;
    if (nbrbeg) {
	int *p;
	Brinfo bp;

	cm->brpl = (int *) zhalloc(nbrbeg * sizeof(int));

	for (p = cm->brpl, bp = brbeg; bp; p++, bp = bp->next)
	    *p = bp->curpos;
    } else
	cm->brpl = NULL;
    if (nbrend) {
	int *p;
	Brinfo bp;

	cm->brsl = (int *) zhalloc(nbrend * sizeof(int));

	for (p = cm->brsl, bp = brend; bp; p++, bp = bp->next)
	    *p = bp->curpos;
    } else
	cm->brsl = NULL;
    cm->qipl = qipl;
    cm->qisl = qisl;
    cm->autoq = (autoq ? autoq : (inbackt ? '`' : '\0'));
    cm->rems = cm->remf = cm->disp = NULL;

    if ((lastprebr || lastpostbr) && !hasbrpsfx(cm, lastprebr, lastpostbr))
	return NULL;

    /* Then build the unambiguous cline list. */
    ai->line = join_clines(ai->line, line);

    mnum++;
    ai->count++;

    addlinknode((alt ? fmatches : matches), cm);

    newmatches = 1;

    /* One more match for this explanation. */
    if (curexpl) {
	if (alt)
	    curexpl->fcount++;
	else
	    curexpl->count++;
    }
    if (!ai->firstm)
	ai->firstm = cm;

    sl = strlen(str);
    lpl = (cm->ppre ? strlen(cm->ppre) : 0);
    lsl = (cm->psuf ? strlen(cm->psuf) : 0);
    ml = sl + lpl + lsl;

    if (ml < minmlen)
	minmlen = ml;
    if (ml > maxmlen)
	maxmlen = ml;

    /* Do we have an exact match? More than one? */
    if (exact) {
	if (!ai->exact) {
	    ai->exact = 1;
	    if (incompfunc) {
		/* If a completion widget is active, we make the exact
		 * string available in `compstate'. */

		char *e;

		zsfree(compexactstr);
		compexactstr = e = (char *) zalloc(ml + 1);
		if (cm->ppre) {
		    strcpy(e, cm->ppre);
		    e += lpl;
		}
		strcpy(e, str);
		e += sl;
		if (cm->psuf)
		    strcpy(e, cm->psuf);
		comp_setunsetptr(0, 0, CP_EXACTSTR, 0);
	    }
	    ai->exactm = cm;
	} else {
	    ai->exact = 2;
	    ai->exactm = NULL;
	    if (incompfunc)
		comp_setunsetptr(0, 0, 0, CP_EXACTSTR);
	}
    }
    return cm;
}

/* This stores the strings from the list in an array. */

/**/
void
set_list_array(char *name, LinkList l)
{
    char **a, **p;
    LinkNode n;

    a = (char **) zalloc((countlinknodes(l) + 1) * sizeof(char *));
    for (p = a, n = firstnode(l); n; incnode(n))
	*p++ = ztrdup((char *) getdata(n));
    *p = NULL;

    setaparam(name, a);
}

/* This is used by compadd to add a couple of matches. The arguments are
 * the strings given via options. The last argument is the array with
 * the matches. */

/**/
int
addmatches(Cadata dat, char **argv)
{
    char *s, *ms, *lipre = NULL, *lisuf = NULL, *lpre = NULL, *lsuf = NULL;
    char **aign = NULL, **dparr = NULL, oaq = autoq, *oppre = dat->ppre;
    char *oqp = qipre, *oqs = qisuf, qc, **disp = NULL;
    int lpl, lsl, pl, sl, bcp = 0, bcs = 0, bpadd = 0, bsadd = 0;
    int llpl = 0, llsl = 0, nm = mnum, gflags = 0, ohp = haspattern;
    int oisalt = 0, isalt, isexact, doadd, ois = instring, oib = inbackt;
    Cline lc = NULL, pline = NULL, sline = NULL;
    Cmatch cm;
    struct cmlist mst;
    Cmlist oms = mstack;
    Patprog cp = NULL;
    LinkList aparl = NULL, oparl = NULL, dparl = NULL;
    Brinfo bp, bpl = brbeg, obpl, bsl = brend, obsl;

    for (bp = brbeg; bp; bp = bp->next)
	bp->curpos = ((dat->aflags & CAF_QUOTE) ? bp->pos : bp->qpos);
    for (bp = brend; bp; bp = bp->next)
	bp->curpos = ((dat->aflags & CAF_QUOTE) ? bp->pos : bp->qpos);

    if (dat->flags & CMF_ISPAR)
	dat->flags |= parflags;
    if (compquote && (qc = *compquote)) {
	if (qc == '`') {
	    instring = 0;
	    inbackt = 0;
	    autoq = '\0';
	} else {
	    instring = (qc == '\'' ? 1 : 2);
	    inbackt = 0;
	    autoq = qc;
	}
    } else {
	instring = inbackt = 0;
	autoq = '\0';
    }
    qipre = ztrdup(compqiprefix ? compqiprefix : "");
    qisuf = ztrdup(compqisuffix ? compqisuffix : "");

    /* Switch back to the heap that was used when the completion widget
     * was invoked. */
    SWITCHHEAPS(compheap) {
	HEAPALLOC {
	    if ((doadd = (!dat->apar && !dat->opar && !dat->dpar)) &&
		(dat->aflags & CAF_MATCH))
		hasmatched = 1;
	    if (dat->apar)
		aparl = newlinklist();
	    if (dat->opar)
		oparl = newlinklist();
	    if (dat->dpar) {
		if (*(dat->dpar) == '(')
		    dparr = NULL;
		else if ((dparr = get_user_var(dat->dpar)) && !*dparr)
		    dparr = NULL;
		dparl = newlinklist();
	    }
	    if (dat->exp) {
		curexpl = (Cexpl) zhalloc(sizeof(struct cexpl));
		curexpl->count = curexpl->fcount = 0;
		curexpl->str = dupstring(dat->exp);
	    } else
		curexpl = NULL;

	    /* Store the matcher in our stack of matchers. */
	    if (dat->match) {
		mst.next = mstack;
		mst.matcher = dat->match;
		mstack = &mst;

		if (!mnum)
		    add_bmatchers(dat->match);

		addlinknode(matchers, dat->match);
		dat->match->refc++;
	    }
	    if (mnum && (mstack || bmatchers))
		update_bmatchers();

	    /* Get the suffixes to ignore. */
	    if (dat->ign)
		aign = get_user_var(dat->ign);
	    /* Get the display strings. */
	    if (dat->disp)
		if ((disp = get_user_var(dat->disp)))
		    disp--;
	    /* Get the contents of the completion variables if we have
	     * to perform matching. */
	    if (dat->aflags & CAF_MATCH) {
		lipre = dupstring(compiprefix);
		lisuf = dupstring(compisuffix);
		lpre = dupstring(compprefix);
		lsuf = dupstring(compsuffix);
		llpl = strlen(lpre);
		llsl = strlen(lsuf);
		/* Test if there is an existing -P prefix. */
		if (dat->pre && *dat->pre) {
		    char *dp = rembslash(dat->pre);

		    pl = pfxlen(dp, lpre);
		    llpl -= pl;
		    lpre += pl;
		}
	    }
	    /* Now duplicate the strings we have from the command line. */
	    if (dat->ipre)
		dat->ipre = (lipre ? dyncat(lipre, dat->ipre) :
			     dupstring(dat->ipre));
	    else if (lipre)
		dat->ipre = lipre;
	    if (dat->isuf)
		dat->isuf = (lisuf ? dyncat(lisuf, dat->isuf) :
			     dupstring(dat->isuf));
	    else if (lisuf)
		dat->isuf = lisuf;
	    if (dat->ppre) {
		if (!(dat->aflags & CAF_QUOTE)) {
		    dat->ppre = quotename(dat->ppre, NULL);
		    if ((dat->flags & CMF_FILE) &&
			dat->ppre[0] == '\\' && dat->ppre[1] == '~')
			chuck(dat->ppre);
		} else
		    dat->ppre = dupstring(dat->ppre);
		lpl = strlen(dat->ppre);
	    } else
		lpl = 0;
	    if (dat->psuf) {
		if (!(dat->aflags & CAF_QUOTE))
		    dat->psuf = quotename(dat->psuf, NULL);
		else
		    dat->psuf = dupstring(dat->psuf);
		lsl = strlen(dat->psuf);
	    } else
		lsl = 0;
	    if (dat->aflags & CAF_MATCH) {
		int ml;

		s = dat->ppre ? dat->ppre : "";
		if ((ml = match_str(lpre, s, &bpl, 0, NULL, 0, 0, 1)) >= 0) {
		    if (matchsubs) {
			Cline tmp = get_cline(NULL, 0, NULL, 0, NULL, 0, 0);

			tmp->prefix = matchsubs;
			if (matchlastpart)
			    matchlastpart->next = tmp;
			else
			    matchparts = tmp;
		    }
		    pline = matchparts;
		    lpre += ml;
		    bcp = ml;
		    bpadd = strlen(s) - ml;
		} else {
		    if (llpl <= lpl && strpfx(lpre, s))
			lpre = "";
		    else if (llpl > lpl && strpfx(s, lpre))
			lpre += lpl;
		    else
			*argv = NULL;
		    bcp = lpl;
		}

		s = dat->psuf ? dat->psuf : "";
		if ((ml = match_str(lsuf, s, &bsl, 0, NULL, 1, 0, 1)) >= 0) {
		    if (matchsubs) {
			Cline tmp = get_cline(NULL, 0, NULL, 0, NULL, 0, CLF_SUF);

			tmp->suffix = matchsubs;
			if (matchlastpart)
			    matchlastpart->next = tmp;
			else
			    matchparts = tmp;
		    }
		    sline = revert_cline(matchparts);
		    lsuf[llsl - ml] = '\0';
		    bcs = ml;
		    bsadd = strlen(s) - ml;
		} else {
		    if (llsl <= lsl && strsfx(lsuf, s))
			lsuf = "";
		    else if (llsl > lsl && strsfx(s, lsuf))
			lsuf[llsl - lsl] = '\0';
		    else
			*argv = NULL;
		    bcs = lsl;
		}
		if (comppatmatch && *comppatmatch) {
		    int is = (*comppatmatch == '*');
		    char *tmp = (char *) zhalloc(2 + llpl + llsl);

		    strcpy(tmp, lpre);
		    tmp[llpl] = 'x';
		    strcpy(tmp + llpl + is, lsuf);

		    tokenize(tmp);
		    remnulargs(tmp);
		    if (haswilds(tmp)) {
			if (is)
			    tmp[llpl] = Star;
			if ((cp = patcompile(tmp, 0, NULL)))
			    haspattern = 1;
		    }
		}
	    }
	    if (*argv) {
		if (dat->pre)
		    dat->pre = dupstring(dat->pre);
		if (dat->suf)
		    dat->suf = dupstring(dat->suf);
		if (!dat->prpre && (dat->prpre = oppre)) {
		    singsub(&(dat->prpre));
		    untokenize(dat->prpre);
		} else
		    dat->prpre = dupstring(dat->prpre);
		/* Select the group in which to store the matches. */
		gflags = (((dat->aflags & CAF_NOSORT ) ? CGF_NOSORT  : 0) |
			  ((dat->aflags & CAF_UNIQALL) ? CGF_UNIQALL : 0) |
			  ((dat->aflags & CAF_UNIQCON) ? CGF_UNIQCON : 0));
		if (dat->group) {
		    endcmgroup(NULL);
		    begcmgroup(dat->group, gflags);
		} else {
		    endcmgroup(NULL);
		    begcmgroup("default", 0);
		}
		if (dat->ylist) {
		    endcmgroup(NULL);
		    begcmgroup(NULL, gflags);
		}
		/* Select the set of matches. */
		oisalt = (dat->aflags & CAF_ALT);

		if (dat->remf) {
		    dat->remf = dupstring(dat->remf);
		    dat->rems = NULL;
		} else if (dat->rems)
		    dat->rems = dupstring(dat->rems);
	    }
	    /* Walk through the matches given. */
	    obpl = bpl;
	    obsl = bsl;
	    for (; (s = *argv); argv++) {
		bpl = obpl;
		bsl = obsl;
		if (disp) {
		    if (!*++disp)
			disp = NULL;
		}
		sl = strlen(s);
		isalt = oisalt;
		if ((!dat->psuf || !*(dat->psuf)) && aign) {
		    /* Do the suffix-test. If the match has one of the
		     * suffixes from ign, we put it in the alternate set. */
		    char **pt = aign;
		    int filell;

		    for (isalt = 0; !isalt && *pt; pt++)
			if ((filell = strlen(*pt)) < sl
			    && !strcmp(*pt, s + sl - filell))
			    isalt = 1;

		    if (isalt && !doadd) {
			if (dparr && !*++dparr)
			    dparr = NULL;
			continue;
		    }
		}
		if (!(dat->aflags & CAF_MATCH)) {
		    if (dat->aflags & CAF_QUOTE)
			ms = dupstring(s);
		    else
			sl = strlen(ms = quotename(s, NULL));
		    lc = bld_parts(ms, sl, -1, NULL);
		    isexact = 0;
		} else if (!(ms = comp_match(lpre, lsuf, s, cp, &lc,
					     (!(dat->aflags & CAF_QUOTE) ?
					      ((dat->ppre && dat->ppre) ||
					       !(dat->flags & CMF_FILE) ? 1 : 2) : 0),
					     &bpl, bcp, &bsl, bcs,
					     &isexact))) {
		    if (dparr && !*++dparr)
			dparr = NULL;
		    continue;
		}
		if (doadd) {
		    Brinfo bp;

		    for (bp = obpl; bp; bp = bp->next)
			bp->curpos += bpadd;
		    for (bp = obsl; bp; bp = bp->next)
			bp->curpos += bsadd;

		    if ((cm = add_match_data(isalt, ms, lc, dat->ipre, NULL,
					     dat->isuf, dat->pre, dat->prpre,
					     dat->ppre, pline,
					     dat->psuf, sline,
					     dat->suf, dat->flags, isexact))) {
			cm->rems = dat->rems;
			cm->remf = dat->remf;
			if (disp)
			    cm->disp = dupstring(*disp);
		    }
		} else {
		    if (dat->apar)
			addlinknode(aparl, ms);
		    if (dat->opar)
			addlinknode(oparl, s);
		    if (dat->dpar && dparr) {
			addlinknode(dparl, *dparr);
			if (!*++dparr)
			    dparr = NULL;
		    }
		    free_cline(lc);
		}
	    }
	    if (dat->apar)
		set_list_array(dat->apar, aparl);
	    if (dat->opar)
		set_list_array(dat->opar, oparl);
	    if (dat->dpar)
		set_list_array(dat->dpar, dparl);
	    if (dat->ylist) {
		if (dat->group) {
		    endcmgroup(get_user_var(dat->ylist));
		    begcmgroup(dat->group, gflags);
		    if (dat->exp)
			addexpl();
		} else {
		    if (dat->exp)
			addexpl();
		    endcmgroup(get_user_var(dat->ylist));
		    begcmgroup("default", 0);
		}
	    } else if (dat->exp)
		addexpl();
	} LASTALLOC;
    } SWITCHBACKHEAPS;

    /* We switched back to the current heap, now restore the stack of
     * matchers. */
    mstack = oms;

    instring = ois;
    inbackt = oib;
    autoq = oaq;
    zsfree(qipre);
    zsfree(qisuf);
    qipre = oqp;
    qisuf = oqs;

    if (mnum == nm)
	haspattern = ohp;

    return (mnum == nm);
}

/**/
static int
docompletion(char *s, int lst, int incmd)
{
    int ret = 0;

    HEAPALLOC {
	char *opm;
	LinkNode n;

	pushheap();

	ainfo = fainfo = NULL;
	matchers = newlinklist();

	hasunqu = 0;
	useline = (lst != COMP_LIST_COMPLETE);
	useexact = isset(RECEXACT);
	uselist = (useline ?
		   ((isset(AUTOLIST) && !isset(BASHAUTOLIST)) ? 
		    (isset(LISTAMBIGUOUS) ? 3 : 2) : 0) : 1);
	zsfree(comppatmatch);
	opm = comppatmatch = ztrdup(useglob ? "*" : "");
	zsfree(comppatinsert);
	comppatinsert = ztrdup("menu");
	zsfree(compforcelist);
	compforcelist = ztrdup("");
	haspattern = 0;
	complistmax = getiparam("LISTMAX");
	zsfree(complastprompt);
	complastprompt = ztrdup(((isset(ALWAYSLASTPROMPT) && zmult == 1) ||
				(unset(ALWAYSLASTPROMPT) && zmult != 1)) ?
				"yes" : "");
	movetoend = ((cs == we || isset(ALWAYSTOEND)) ? 2 : 1);
	showinglist = 0;
	hasmatched = 0;
	minmlen = 1000000;
	maxmlen = -1;

	/* Make sure we have the completion list and compctl. */
	if (makecomplist(s, incmd, lst)) {
	    /* Error condition: feeeeeeeeeeeeep(). */
	    cs = 0;
	    foredel(ll);
	    inststr(origline);
	    cs = origcs;
	    clearlist = 1;
	    ret = 1;
	    minfo.cur = NULL;
	    goto compend;
	}
	zsfree(lastprebr);
	zsfree(lastpostbr);
	lastprebr = lastpostbr = NULL;

	if (comppatmatch && *comppatmatch && comppatmatch != opm)
	    haspattern = 1;
	if (!useline && uselist) {
	    /* All this and the guy only wants to see the list, sigh. */
	    cs = 0;
	    foredel(ll);
	    inststr(origline);
	    cs = origcs;
	    showinglist = -2;
	} else if (useline == 2 && nmatches > 1) {
	    int first = 1, nm = nmatches;
	    Cmatch *mc;

	    menucmp = 1;
	    menuacc = 0;

	    for (minfo.group = amatches;
		 minfo.group && !(minfo.group)->mcount;
		 minfo.group = (minfo.group)->next);

	    mc = (minfo.group)->matches;

	    while (1) {
		if (!first)
		    acceptlast();
		first = 0;

		if (!--nm)
		    menucmp = 0;

		do_single(*mc);
		minfo.cur = mc;

		if (!*++(minfo.cur)) {
		    do {
			if (!(minfo.group = (minfo.group)->next))
			    break;
		    } while (!(minfo.group)->mcount);
		    if (!minfo.group)
			break;
		    minfo.cur = minfo.group->matches;
		}
		mc = minfo.cur;
	    }
	    menucmp = 0;
	    minfo.cur = NULL;

	    if (compforcelist && *compforcelist && uselist)
		showinglist = -2;
	    else
		invalidatelist();
	} else if (useline) {
	    /* We have matches. */
	    if (nmatches > 1) {
		/* There is more than one match. */
		ret = do_ambiguous();
	    } else if (nmatches == 1) {
		/* Only one match. */
		Cmgroup m = amatches;

		while (!m->mcount)
		    m = m->next;
		minfo.cur = NULL;
		minfo.asked = 0;
		do_single(m->matches[0]);
		if (compforcelist && *compforcelist) {
		    if (uselist)
			showinglist = -2;
		    else
			clearlist = 1;
		} else
		    invalidatelist();
	    }
	} else {
	    invalidatelist();
	    if (compforcelist && *compforcelist)
		clearlist = 1;
	    cs = 0;
	    foredel(ll);
	    inststr(origline);
	    cs = origcs;
	}
	/* Print the explanation strings if needed. */
	if (!showinglist && validlist && usemenu != 2 && nmatches != 1 &&
	    useline != 2 && (!oldlist || !listshown)) {
	    onlyexpl = 1;
	    showinglist = -2;
	}
      compend:
	for (n = firstnode(matchers); n; incnode(n))
	    freecmatcher((Cmatcher) getdata(n));

	ll = strlen((char *)line);
	if (cs > ll)
	    cs = ll;
	popheap();
    } LASTALLOC;
    return ret;
}

/* This calls the given function for new style completion. */

/**/
static void
callcompfunc(char *s, char *fn)
{
    List list;
    int lv = lastval;
    char buf[20];

    if ((list = getshfunc(fn)) != &dummy_list) {
	char **p, *tmp;
	int aadd = 0, usea = 1, icf = incompfunc, osc = sfcontext;
	unsigned int rset, kset;
	Param *ocrpms = comprpms, *ockpms = compkpms;

	comprpms = (Param *) zalloc(CP_REALPARAMS * sizeof(Param));
	compkpms = (Param *) zalloc(CP_KEYPARAMS * sizeof(Param));

	rset = CP_ALLREALS;
	kset = CP_ALLKEYS &
	    ~(CP_PARAMETER | CP_REDIRECT | CP_QUOTE | CP_QUOTING |
	      CP_EXACTSTR | CP_FORCELIST | CP_OLDLIST | CP_OLDINS |
	      (useglob ? 0 : CP_PATMATCH));
	zsfree(compvared);
	if (varedarg) {
	    compvared = ztrdup(varedarg);
	    kset |= CP_VARED;
	} else
	    compvared = ztrdup("");
	if (!*complastprompt)
	    kset &= ~CP_LASTPROMPT;
	zsfree(compcontext);
	zsfree(compparameter);
	zsfree(compredirect);
	compparameter = compredirect = "";
	if (ispar)
	    compcontext = (ispar == 2 ? "brace_parameter" : "parameter");
	else if (linwhat == IN_MATH) {
	    if (insubscr) {
		compcontext = "subscript";
		if (varname) {
		    compparameter = varname;
		    kset |= CP_PARAMETER;
		}
	    } else
		compcontext = "math";
	    usea = 0;
	} else if (lincmd) {
	    if (insubscr) {
		compcontext = "subscript";
		kset |= CP_PARAMETER;
	    } else
		compcontext = "command";
	} else if (linredir) {
	    compcontext = "redirect";
	    if (rdstr)
		compredirect = rdstr;
	    kset |= CP_REDIRECT;
	} else
	    switch (linwhat) {
	    case IN_ENV:
		compcontext = (linarr ? "array_value" : "value");
		compparameter = varname;
		kset |= CP_PARAMETER;
		if (!clwpos) {
		    clwpos = 1;
		    clwnum = 2;
		    zsfree(clwords[1]);
		    clwords[1] = ztrdup(s);
		    zsfree(clwords[2]);
		    clwords[2] = NULL;
		}
		aadd = 1;
		break;
	    case IN_COND:
		compcontext = "condition";
		break;
	    default:
		if (cmdstr)
		    compcontext = "command";
		else {
		    compcontext = "value";
		    kset |= CP_PARAMETER;
		    if (clwords[0])
			compparameter = clwords[0];
		    aadd = 1;
		}
	    }
	compcontext = ztrdup(compcontext);
	if (compwords)
	    freearray(compwords);
	if (usea && (!aadd || clwords[0])) {
	    char **q;

	    PERMALLOC {
		q = compwords = (char **)
		    zalloc((clwnum + 1) * sizeof(char *));
		for (p = clwords + aadd; *p; p++, q++) {
		    tmp = dupstring(*p);
		    untokenize(tmp);
		    *q = ztrdup(tmp);
		}
		*q = NULL;
	    } LASTALLOC;
	} else
	    compwords = (char **) zcalloc(sizeof(char *));

	compparameter = ztrdup(compparameter);
	compredirect = ztrdup(compredirect);
	zsfree(compquote);
	zsfree(compquoting);
	if (instring) {
	    if (instring == 1) {
		compquote = ztrdup("\'");
		compquoting = ztrdup("single");
	    } else {
		compquote = ztrdup("\"");
		compquoting = ztrdup("double");
	    }
	    kset |= CP_QUOTE | CP_QUOTING;
	} else if (inbackt) {
	    compquote = ztrdup("`");
	    compquoting = ztrdup("backtick");
	    kset |= CP_QUOTE | CP_QUOTING;
	} else {
	    compquote = ztrdup("");
	    compquoting = ztrdup("");
	}
	zsfree(compprefix);
	zsfree(compsuffix);
	if (unset(COMPLETEINWORD)) {
	    tmp = quotename(s, NULL);
	    untokenize(tmp);
	    compprefix = ztrdup(tmp);
	    compsuffix = ztrdup("");
	} else {
	    char *ss, sav;
	    
	    ss = s + offs;

	    sav = *ss;
	    *ss = '\0';
	    tmp = quotename(s, NULL);
	    untokenize(tmp);
	    compprefix = ztrdup(tmp);
	    *ss = sav;
	    ss = quotename(ss, NULL);
	    untokenize(ss);
	    compsuffix = ztrdup(ss);
	}
	zsfree(compiprefix);
	compiprefix = ztrdup("");
	zsfree(compisuffix);
	compisuffix = ztrdup("");
	zsfree(compqiprefix);
	compqiprefix = ztrdup(qipre ? qipre : "");
	zsfree(compqisuffix);
	compqisuffix = ztrdup(qisuf ? qisuf : "");
	compcurrent = (usea ? (clwpos + 1 - aadd) : 0);

	zsfree(complist);
	switch (uselist) {
	case 0: complist = ""; kset &= ~CP_LIST; break;
	case 1: complist = "list"; break;
	case 2: complist = "autolist"; break;
	case 3: complist = "ambiguous"; break;
	}
	complist = ztrdup(complist);
	zsfree(compinsert);
	if (useline) {
	    switch (usemenu) {
	    case 0: compinsert = "unambiguous"; break;
	    case 1: compinsert = "menu"; break;
	    case 2: compinsert = "automenu"; break;
	    }
	} else {
	    compinsert = "";
	    kset &= ~CP_INSERT;
	}
	compinsert = ztrdup(compinsert);
	if (useexact)
	    compexact = ztrdup("accept");
	else {
	    compexact = ztrdup("");
	    kset &= ~CP_EXACT;
	}
	zsfree(comptoend);
	if (movetoend == 1)
	    comptoend = ztrdup("single");
	else
	    comptoend = ztrdup("match");
	zsfree(compoldlist);
	zsfree(compoldins);
	if (hasoldlist && lastpermmnum) {
	    if (listshown)
		compoldlist = "shown";
	    else
		compoldlist = "yes";
	    kset |= CP_OLDLIST;
	    if (minfo.cur) {
		sprintf(buf, "%d", (*(minfo.cur))->gnum);
		compoldins = buf;
		kset |= CP_OLDINS;
	    } else
		compoldins = "";
	} else
	    compoldlist = compoldins = "";
	compoldlist = ztrdup(compoldlist);
	compoldins = ztrdup(compoldins);

	incompfunc = 1;
	startparamscope();
	makecompparamsptr();
	comp_setunsetptr(rset, (~rset & CP_ALLREALS),
			 kset, (~kset & CP_ALLKEYS));
	makezleparams(1);
	sfcontext = SFC_CWIDGET;
	NEWHEAPS(compheap) {
	    LinkList largs = NULL;
	    int olv = lastval;

	    if (*cfargs) {
		char **p = cfargs;

		largs = newlinklist();
		addlinknode(largs, dupstring(fn));
		while (*p)
		    addlinknode(largs, dupstring(*p++));
	    }
	    doshfunc(fn, list, largs, 0, 0);
	    cfret = lastval;
	    lastval = olv;
	} OLDHEAPS;
	sfcontext = osc;
	endparamscope();
	lastcmd = 0;
	incompfunc = icf;

	if (!complist)
	    uselist = 0;
	else if (!strncmp(complist, "list", 4))
	    uselist = 1;
	else if (!strncmp(complist, "auto", 4))
	    uselist = 2;
	else if (!strncmp(complist, "ambig", 5))
	    uselist = 3;
	else
	    uselist = 0;

	onlyexpl = (complist && strstr(complist, "expl"));

	if (!compinsert)
	    useline = 0;
	else if (!strcmp(compinsert, "unambig") ||
		 !strcmp(compinsert, "unambiguous"))
	    useline = 1, usemenu = 0;
	else if (!strcmp(compinsert, "menu"))
	    useline = 1, usemenu = 1;
	else if (!strcmp(compinsert, "auto") ||
		 !strcmp(compinsert, "automenu"))
	    useline = 1, usemenu = 2;
	else if (!strcmp(compinsert, "all"))
	    useline = 2, usemenu = 0;
	else if (idigit(*compinsert)) {
	    char *m;

	    useline = 1; usemenu = 3;
	    insmnum = atoi(compinsert);
	    if ((m = strchr(compinsert, ':'))) {
		insgroup = 1;
		insgnum = atoi(m + 1);
	    }
	    insspace = (compinsert[strlen(compinsert) - 1] == ' ');
	} else
	    useline = usemenu = 0;
	useexact = (compexact && !strcmp(compexact, "accept"));

	if (!comptoend || !*comptoend)
	    movetoend = 0;
	else if (!strcmp(comptoend, "single"))
	    movetoend = 1;
	else if (!strcmp(comptoend, "always"))
	    movetoend = 3;
	else
	    movetoend = 2;

	oldlist = (hasoldlist && compoldlist && !strcmp(compoldlist, "keep"));
	oldins = (hasoldlist && minfo.cur &&
		  compoldins && !strcmp(compoldins, "keep"));

	zfree(comprpms, CP_REALPARAMS * sizeof(Param));
	zfree(compkpms, CP_KEYPARAMS * sizeof(Param));
	comprpms = ocrpms;
	compkpms = ockpms;
    }
    lastval = lv;
}

/* Create the completion list.  This is called whenever some bit of   *
 * completion code needs the list.                                    *
 * Along with the list is maintained the prefixes/suffixes etc.  When *
 * any of this becomes invalid -- e.g. if some text is changed on the *
 * command line -- invalidatelist() should be called, to set          *
 * validlist to zero and free up the memory used.  This function      *
 * returns non-zero on error.                                         */

/**/
static int
makecomplist(char *s, int incmd, int lst)
{
    struct cmlist ms;
    Cmlist m;
    char *p, *os = s;
    int onm = nmatches, osi = movefd(0);

    /* Inside $... ? */
    if (compfunc && (p = check_param(s, 0, 0)))
	os = s = p;

    /* We build a copy of the list of matchers to use to make sure that this
     * works even if a shell function called from the completion code changes
     * the global matchers. */

    if ((m = cmatcher)) {
	Cmlist mm, *mp = &mm;
	int n;

	for (n = 0; m; m = m->next, n++) {
	    if (m->matcher) {
		*mp = (Cmlist) zhalloc(sizeof(struct cmlist));
		(*mp)->matcher = m->matcher;
		(*mp)->next = NULL;
		(*mp)->str = dupstring(m->str);
		mp = &((*mp)->next);
		addlinknode(matchers, m->matcher);
		m->matcher->refc++;
	    }
	}
	m = mm;
	compmatcher = 1;
	compmatchertot = n;
    } else
	compmatcher = 0;

    linwhat = inwhat;

    /* Walk through the global matchers. */
    for (;;) {
	bmatchers = NULL;
	zsfree(compmatcherstr);
	if (m) {
	    ms.next = NULL;
	    ms.matcher = m->matcher;
	    mstack = &ms;

	    /* Store the matchers used in the bmatchers list which is used
	     * when building new parts for the string to insert into the 
	     * line. */
	    add_bmatchers(m->matcher);
	    compmatcherstr = ztrdup(m->str);
	} else {
	    mstack = NULL;
	    compmatcherstr = ztrdup("");
	}
	ainfo = (Aminfo) hcalloc(sizeof(struct aminfo));
	fainfo = (Aminfo) hcalloc(sizeof(struct aminfo));

	freecl = NULL;

	if (!validlist)
	    lastambig = 0;
	amatches = NULL;
	mnum = 0;
	unambig_mnum = -1;
	isuf = NULL;
	insmnum = insgnum = 1;
	insgroup = oldlist = oldins = 0;
	begcmgroup("default", 0);
	menucmp = menuacc = newmatches = onlyexpl = 0;

	runhookdef(COMPCTLBEFOREHOOK, NULL);

	s = dupstring(os);
	if (compfunc)
	    callcompfunc(s, compfunc);
	else {
	    struct ccmakedat dat;

	    dat.str = s;
	    dat.incmd = incmd;
	    dat.lst = lst;
	    runhookdef(COMPCTLMAKEHOOK, (void *) &dat);
	}
	endcmgroup(NULL);

	runhookdef(COMPCTLAFTERHOOK,
		   (void *) ((amatches && !oldlist) ? 1L : 0L));

	if (oldlist) {
	    nmatches = onm;
	    validlist = 1;
	    amatches = lastmatches;
	    lmatches = lastlmatches;
	    if (pmatches) {
		freematches(pmatches);
		pmatches = NULL;
		hasperm = 0;
	    }
	    redup(osi, 0);

	    return 0;
	}
	PERMALLOC {
	    if (lastmatches) {
		freematches(lastmatches);
		lastmatches = NULL;
	    }
	    permmatches(1);
	    amatches = pmatches;
	    lastpermmnum = permmnum;
	    lastpermgnum = permgnum;
	} LASTALLOC;

	lastmatches = pmatches;
	lastlmatches = lmatches;
	pmatches = NULL;
	hasperm = 0;
	hasoldlist = 1;

	if (nmatches && !errflag) {
	    validlist = 1;

	    redup(osi, 0);

	    return 0;
	}
	if (!m || !(m = m->next))
	    break;

	errflag = 0;
	compmatcher++;
    }
    redup(osi, 0);
    return 1;
}

/* This should probably be moved into tokenize(). */

/**/
static char *
ctokenize(char *p)
{
    char *r = p;
    int bslash = 0;

    tokenize(p);

    for (p = r; *p; p++) {
	if (*p == '\\')
	    bslash = 1;
	else {
	    if (*p == '$' || *p == '{' || *p == '}') {
		if (bslash)
		    p[-1] = Bnull;
		else
		    *p = (*p == '$' ? String :
			  (*p == '{' ? Inbrace : Outbrace));
	    }
	    bslash = 0;
	}
    }
    return r;
}

/**/
char *
comp_str(int *ipl, int *pl, int untok)
{
    char *p = dupstring(compprefix);
    char *s = dupstring(compsuffix);
    char *ip = dupstring(compiprefix);
    char *str;
    int lp, ls, lip;

    if (!untok) {
	ctokenize(p);
	remnulargs(p);
	ctokenize(s);
	remnulargs(s);
    }
    lp = strlen(p);
    ls = strlen(s);
    lip = strlen(ip);
    str = zhalloc(lip + lp + ls + 1);
    strcpy(str, ip);
    strcat(str, p);
    strcat(str, s);

    if (ipl)
	*ipl = lip;
    if (pl)
	*pl = lp;

    return str;
}

/**/
int
set_comp_sep(void)
{
    int lip, lp;
    char *s = comp_str(&lip, &lp, 0);
    LinkList foo = newlinklist();
    LinkNode n;
    int owe = we, owb = wb, ocs = cs, swb, swe, scs, soffs, ne = noerrs;
    int tl, got = 0, i = 0, cur = -1, oll = ll, sl;
    int ois = instring, oib = inbackt, noffs = lip + lp;
    char *tmp, *p, *ns, *ol = (char *) line, sav, oaq = autoq, *qp, *qs;

    if (compisuffix)
	s = dyncat(s, compisuffix);
    untokenize(s);

    swb = swe = soffs = 0;
    ns = NULL;

    /* Put the string in the lexer buffer and call the lexer to *
     * get the words we have to expand.                        */
    zleparse = 1;
    addedx = 1;
    noerrs = 1;
    lexsave();
    tmp = (char *) zhalloc(tl = 3 + strlen(s));
    tmp[0] = ' ';
    memcpy(tmp + 1, s, noffs);
    tmp[(scs = cs = 1 + noffs)] = 'x';
    strcpy(tmp + 2 + noffs, s + noffs);
    tmp = rembslash(tmp);
    inpush(dupstrspace(tmp), 0, NULL);
    line = (unsigned char *) tmp;
    ll = tl - 1;
    strinbeg(0);
    noaliases = 1;
    do {
	ctxtlex();
	if (tok == LEXERR) {
	    int j;

	    if (!tokstr)
		break;
	    for (j = 0, p = tokstr; *p; p++)
		if (*p == Snull || *p == Dnull)
		    j++;
	    if (j & 1) {
		tok = STRING;
		if (p > tokstr && p[-1] == ' ')
		    p[-1] = '\0';
	    }
	}
	if (tok == ENDINPUT || tok == LEXERR)
	    break;
	if (tokstr && *tokstr)
	    addlinknode(foo, (p = ztrdup(tokstr)));
	else
	    p = NULL;
	if (!got && !zleparse) {
	    DPUTS(!p, "no current word in substr");
	    got = 1;
	    cur = i;
	    swb = wb - 1;
	    swe = we - 1;
	    soffs = cs - swb;
	    chuck(p + soffs);
	    ns = dupstring(p);
	}
	i++;
    } while (tok != ENDINPUT && tok != LEXERR);
    noaliases = 0;
    strinend();
    inpop();
    errflag = zleparse = 0;
    noerrs = ne;
    lexrestore();
    wb = owb;
    we = owe;
    cs = ocs;
    line = (unsigned char *) ol;
    ll = oll;
    if (cur < 0 || i < 1)
	return 1;
    owb = offs;
    offs = soffs;
    if ((p = check_param(ns, 0, 1))) {
	for (p = ns; *p; p++)
	    if (*p == Dnull)
		*p = '"';
	    else if (*p == Snull)
		*p = '\'';
    }
    offs = owb;
    if (*ns == Snull || *ns == Dnull) {
	instring = (*ns == Snull ? 1 : 2);
	inbackt = 0;
	swb++;
	if (ns[strlen(ns) - 1] == *ns && ns[1])
	    swe--;
	autoq = (*ns == Snull ? '\'' : '"');
    } else {
	instring = 0;
	autoq = '\0';
    }
    for (p = ns, i = swb; *p; p++, i++) {
	if (INULL(*p)) {
	    if (i < scs)
		soffs--;
	    if (p[1] || *p != Bnull) {
		if (*p == Bnull) {
		    if (scs == i + 1)
			scs++, soffs++;
		} else {
		    if (scs > i--)
			scs--;
		}
	    } else {
		if (scs == swe)
		    scs--;
	    }
	    chuck(p--);
	}
    }
    sav = s[(i = swb - 1)];
    s[i] = '\0';
    qp = tricat(qipre, rembslash(s), "");
    s[i] = sav;
    if (swe < swb)
	swe = swb;
    swe--;
    sl = strlen(s);
    if (swe > sl)
	swe = sl, ns[swe - swb + 1] = '\0';
    qs = tricat(rembslash(s + swe), qisuf, "");
    sl = strlen(ns);
    if (soffs > sl)
	soffs = sl;

    {
	int set = CP_QUOTE | CP_QUOTING, unset = 0;

	zsfree(compquote);
	zsfree(compquoting);
	if (instring == 2) {
	    compquote = "\"";
	    compquoting = "double";
	} else if (instring == 1) {
	    compquote = "'";
	    compquoting = "single";
	} else {
	    compquote = compquoting = "";
	    unset = set;
	    set = 0;
	}
	compquote = ztrdup(compquote);
	compquoting = ztrdup(compquoting);
	comp_setunsetptr(0, 0, set, unset);

	if (unset(COMPLETEINWORD)) {
	    untokenize(ns);
	    zsfree(compprefix);
	    compprefix = ztrdup(ns);
	    zsfree(compsuffix);
	    compsuffix = ztrdup("");
	} else {
	    char *ss, sav;
	    
	    ss = ns + soffs;

	    sav = *ss;
	    *ss = '\0';
	    untokenize(ns);
	    compprefix = ztrdup(ns);
	    *ss = sav;
	    untokenize(ss);
	    compsuffix = ztrdup(ss);
	}
	zsfree(compiprefix);
	compiprefix = ztrdup("");
	zsfree(compisuffix);
	compisuffix = ztrdup("");
	zsfree(compqiprefix);
	zsfree(compqisuffix);
	if (ois) {
	    compqiprefix = qp;
	    compqisuffix = qs;
	} else {
	    compqiprefix = ztrdup(quotename(qp, NULL));
	    zsfree(qp);
	    compqisuffix = ztrdup(quotename(qs, NULL));
	    zsfree(qs);
	}
	freearray(compwords);
	i = countlinknodes(foo);
	compwords = (char **) zalloc((i + 1) * sizeof(char *));
	for (n = firstnode(foo), i = 0; n; incnode(n), i++) {
	    p = compwords[i] = (char *) getdata(n);
	    untokenize(p);
	}
	compcurrent = cur + 1;
	compwords[i] = NULL;
    }
    autoq = oaq;
    instring = ois;
    inbackt = oib;

    return 0;
}

/* Invalidate the completion list. */

/**/
void
invalidatelist(void)
{
    if (showinglist == -2)
	listmatches();
    if (validlist) {
	freematches(lastmatches);
	lastmatches = NULL;
	hasoldlist = 0;
    }
    lastambig = menucmp = menuacc = validlist = showinglist = fromcomp = 0;
    listdat.valid = 0;
    if (listshown < 0)
	listshown = 0;
    minfo.cur = NULL;
    minfo.asked = 0;
    zsfree(minfo.prebr);
    zsfree(minfo.postbr);
    minfo.postbr = minfo.prebr = NULL;
    compwidget = NULL;
}

/* Get the words from a variable or a compctl -k list. */

/**/
char **
get_user_var(char *nam)
{
    if (!nam)
	return NULL;
    else if (*nam == '(') {
	/* It's a (...) list, not a parameter name. */
	char *ptr, *s, **uarr, **aptr;
	int count = 0, notempty = 0, brk = 0;
	LinkList arrlist = newlinklist();

	ptr = dupstring(nam);
	s = ptr + 1;
	while (*++ptr) {
	    if (*ptr == '\\' && ptr[1])
		chuck(ptr), notempty = 1;
	    else if (*ptr == ',' || inblank(*ptr) || *ptr == ')') {
		if (*ptr == ')')
		    brk++;
		if (notempty) {
		    *ptr = '\0';
		    count++;
		    if (*s == '\n')
			s++;
		    addlinknode(arrlist, s);
		}
		s = ptr + 1;
		notempty = 0;
	    } else {
		notempty = 1;
		if (*ptr == Meta)
		    ptr++;
	    }
	    if (brk)
		break;
	}
	if (!brk || !count)
	    return NULL;
	*ptr = '\0';
	aptr = uarr = (char **) zhalloc(sizeof(char *) * (count + 1));

	while ((*aptr++ = (char *)ugetnode(arrlist)));
	uarr[count] = NULL;
	return uarr;
    } else {
	/* Otherwise it should be a parameter name. */
	char **arr = NULL, *val;

	if ((arr = getaparam(nam)) || (arr = gethparam(nam)))
	    return (incompfunc ? arrdup(arr) : arr);

	if ((val = getsparam(nam))) {
	    arr = (char **) zhalloc(2*sizeof(char *));
	    arr[0] = (incompfunc ? dupstring(val) : val);
	    arr[1] = NULL;
	}
	return arr;
    }
}

/* This is strcmp with ignoring backslashes. */

/**/
static int
strbpcmp(char **aa, char **bb)
{
    char *a = *aa, *b = *bb;

    while (*a && *b) {
	if (*a == '\\')
	    a++;
	if (*b == '\\')
	    b++;
	if (*a != *b)
	    return (int)(*a - *b);
	if (*a)
	    a++;
	if (*b)
	    b++;
    }
    return (int)(*a - *b);
}

/* The comparison function for matches (used for sorting). */

static int
matchcmp(Cmatch *a, Cmatch *b)
{
    if ((*a)->disp) {
	if ((*b)->disp) {
	    if ((*a)->flags & CMF_DISPLINE) {
		if ((*b)->flags & CMF_DISPLINE)
		    return strcmp((*a)->disp, (*b)->disp);
		else
		    return -1;
	    } else {
		if ((*b)->flags & CMF_DISPLINE)
		    return 1;
		else
		    return strcmp((*a)->disp, (*b)->disp);
	    }
	}
	return -1;
    }
    if ((*b)->disp)
	return 1;

    return strbpcmp(&((*a)->str), &((*b)->str));
}

/* This tests, whether two matches are equal (would produce the same  *
 * strings on the command line). */

#define matchstreq(a, b) ((!(a) && !(b)) || ((a) && (b) && !strcmp((a), (b))))

static int
matcheq(Cmatch a, Cmatch b)
{
    return matchstreq(a->ipre, b->ipre) &&
	matchstreq(a->pre, b->pre) &&
	matchstreq(a->ppre, b->ppre) &&
	matchstreq(a->psuf, b->psuf) &&
	matchstreq(a->suf, b->suf) &&
	((!a->disp && !b->disp && matchstreq(a->str, b->str)) ||
	 (a->disp && b->disp && !strcmp(a->disp, b->disp) &&
	  matchstreq(a->str, b->str)));
}

/* Make an array from a linked list. The second argument says whether *
 * the array should be sorted. The third argument is used to return   *
 * the number of elements in the resulting array. The fourth argument *
 * is used to return the number of NOLIST elements. */

/**/
static Cmatch *
makearray(LinkList l, int type, int flags, int *np, int *nlp, int *llp)
{
    Cmatch *ap, *bp, *cp, *rp;
    LinkNode nod;
    int n, nl = 0, ll = 0;

    /* Build an array for the matches. */
    rp = ap = (Cmatch *) ncalloc(((n = countlinknodes(l)) + 1) *
				 sizeof(Cmatch));

    /* And copy them into it. */
    for (nod = firstnode(l); nod; incnode(nod))
	*ap++ = (Cmatch) getdata(nod);
    *ap = NULL;

    if (!type) {
	if (flags) {
	    char **ap, **bp, **cp;

	    /* Now sort the array (it contains strings). */
	    qsort((void *) rp, n, sizeof(char *),
		  (int (*) _((const void *, const void *)))strbpcmp);

	    /* And delete the ones that occur more than once. */
	    for (ap = cp = (char **) rp; *ap; ap++) {
		*cp++ = *ap;
		for (bp = ap; bp[1] && !strcmp(*ap, bp[1]); bp++, n--);
		ap = bp;
	    }
	    *cp = NULL;
	}
    } else {
	if (!(flags & CGF_NOSORT)) {
	    /* Now sort the array (it contains matches). */
	    qsort((void *) rp, n, sizeof(Cmatch),
		  (int (*) _((const void *, const void *)))matchcmp);

	    if (!(flags & CGF_UNIQCON)) {
		/* And delete the ones that occur more than once. */
		for (ap = cp = rp; *ap; ap++) {
		    *cp++ = *ap;
		    for (bp = ap; bp[1] && matcheq(*ap, bp[1]); bp++, n--);
		    ap = bp;
		    /* Mark those, that would show the same string in the list. */
		    for (; bp[1] && !(*ap)->disp && !(bp[1])->disp &&
			     !strcmp((*ap)->str, (bp[1])->str); bp++)
			(bp[1])->flags |= CMF_NOLIST;
		}
		*cp = NULL;
	    }
	    for (ap = rp; *ap; ap++) {
		if ((*ap)->disp && ((*ap)->flags & CMF_DISPLINE))
		    ll++;
		if ((*ap)->flags & CMF_NOLIST)
		    nl++;
	    }
	} else {
	    if (!(flags & CGF_UNIQALL) && !(flags & CGF_UNIQCON)) {
		for (ap = rp; *ap; ap++) {
		    for (bp = cp = ap + 1; *bp; bp++) {
			if (!matcheq(*ap, *bp))
			    *cp++ = *bp;
			else
			    n--;
		    }
		    *cp = NULL;
		}
	    } else if (!(flags & CGF_UNIQCON)) {
		for (ap = cp = rp; *ap; ap++) {
		    *cp++ = *ap;
		    for (bp = ap; bp[1] && matcheq(*ap, bp[1]); bp++, n--);
		    ap = bp;
		    for (; bp[1] && !(*ap)->disp && !(bp[1])->disp &&
			     !strcmp((*ap)->str, (bp[1])->str); bp++)
			(bp[1])->flags |= CMF_NOLIST;
		}
		*cp = NULL;
	    }
	    for (ap = rp; *ap; ap++) {
		if ((*ap)->disp && ((*ap)->flags & CMF_DISPLINE))
		    ll++;
		if ((*ap)->flags & CMF_NOLIST)
		    nl++;
	    }
	}
    }
    if (np)
	*np = n;
    if (nlp)
	*nlp = nl;
    if (llp)
	*llp = ll;
    return rp;
}

/* This begins a new group of matches. */

/**/
static void
begcmgroup(char *n, int flags)
{
    if (n) {
	Cmgroup p = amatches;

	while (p) {
	    if (p->name &&
		flags == (p->flags & (CGF_NOSORT|CGF_UNIQALL|CGF_UNIQCON)) &&
		!strcmp(n, p->name)) {
		mgroup = p;

		expls = p->lexpls;
		matches = p->lmatches;
		fmatches = p->lfmatches;
		allccs = p->lallccs;

		return;
	    }
	    p = p->next;
	}
    }
    mgroup = (Cmgroup) zhalloc(sizeof(struct cmgroup));
    mgroup->name = dupstring(n);
    mgroup->lcount = mgroup->llcount = mgroup->mcount = 0;
    mgroup->flags = flags;
    mgroup->matches = NULL;
    mgroup->ylist = NULL;
    mgroup->expls = NULL;

    mgroup->lexpls = expls = newlinklist();
    mgroup->lmatches = matches = newlinklist();
    mgroup->lfmatches = fmatches = newlinklist();

    mgroup->lallccs = allccs = ((flags & CGF_NOSORT) ? NULL : newlinklist());

    mgroup->next = amatches;
    amatches = mgroup;
}

/* End the current group for now. */

/**/
static void
endcmgroup(char **ylist)
{
    mgroup->ylist = ylist;
}

/* Add an explanation string to the current group, joining duplicates. */

/**/
static void
addexpl(void)
{
    LinkNode n;
    Cexpl e;

    for (n = firstnode(expls); n; incnode(n)) {
	e = (Cexpl) getdata(n);
	if (!strcmp(curexpl->str, e->str)) {
	    e->count += curexpl->count;
	    e->fcount += curexpl->fcount;

	    return;
	}
    }
    addlinknode(expls, curexpl);
    newmatches = 1;
}

/* This duplicates one match. */

/**/
static Cmatch
dupmatch(Cmatch m, int nbeg, int nend)
{
    Cmatch r;

    r = (Cmatch) ncalloc(sizeof(struct cmatch));

    r->str = ztrdup(m->str);
    r->ipre = ztrdup(m->ipre);
    r->ripre = ztrdup(m->ripre);
    r->isuf = ztrdup(m->isuf);
    r->ppre = ztrdup(m->ppre);
    r->psuf = ztrdup(m->psuf);
    r->prpre = ztrdup(m->prpre);
    r->pre = ztrdup(m->pre);
    r->suf = ztrdup(m->suf);
    r->flags = m->flags;
    if (nbeg) {
	int *p, *q, i;

	r->brpl = (int *) zalloc(nbeg * sizeof(int));

	for (p = r->brpl, q = m->brpl, i = nbeg; i--; p++, q++)
	    *p = *q;
    } else
	r->brpl = NULL;
    if (nend) {
	int *p, *q, i;

	r->brsl = (int *) zalloc(nend * sizeof(int));

	for (p = r->brsl, q = m->brsl, i = nend; i--; p++, q++)
	    *p = *q;
    } else
	r->brsl = NULL;
    r->rems = ztrdup(m->rems);
    r->remf = ztrdup(m->remf);
    r->autoq = m->autoq;
    r->qipl = m->qipl;
    r->qisl = m->qisl;
    r->disp = dupstring(m->disp);

    return r;
}

/* This duplicates all groups of matches. */

/**/
static int
permmatches(int last)
{
    Cmgroup g = amatches, n;
    Cmatch *p, *q;
    Cexpl *ep, *eq, e, o;
    LinkList mlist;
    static int fi = 0;
    int nn, nl, ll, gn = 1, mn = 1, rn;

    if (pmatches && !newmatches)
	return fi;

    newmatches = fi = 0;

    if (pmatches)
	freematches(pmatches);

    pmatches = lmatches = NULL;
    nmatches = smatches = 0;

    if (!ainfo->count) {
	if (last)
	    ainfo = fainfo;
	fi = 1;
    }
    while (g) {
	HEAPALLOC {
	    if (empty(g->lmatches))
		/* We have no matches, try ignoring fignore. */
		mlist = g->lfmatches;
	    else
		mlist = g->lmatches;

	    g->matches = makearray(mlist, 1, g->flags, &nn, &nl, &ll);
	    g->mcount = nn;
	    if ((g->lcount = nn - nl) < 0)
		g->lcount = 0;
	    g->llcount = ll;
	    if (g->ylist) {
		g->lcount = arrlen(g->ylist);
		smatches = 2;
	    }
	    g->expls = (Cexpl *) makearray(g->lexpls, 0, 0, &(g->ecount),
					   NULL, NULL);

	    g->ccount = 0;
	} LASTALLOC;

	nmatches += g->mcount;
	smatches += g->lcount;

	n = (Cmgroup) ncalloc(sizeof(struct cmgroup));

	if (!lmatches)
	    lmatches = n;
	if (pmatches)
	    pmatches->prev = n;
	n->next = pmatches;
	pmatches = n;
	n->prev = 0;
	n->num = gn++;

	n->flags = g->flags;
	n->mcount = g->mcount;
	n->matches = p = (Cmatch *) ncalloc((n->mcount + 1) *
					    sizeof(Cmatch));
	for (q = g->matches; *q; q++, p++)
	    *p = dupmatch(*q, nbrbeg, nbrend);
	*p = NULL;

	n->lcount = g->lcount;
	n->llcount = g->llcount;
	if (g->ylist)
	    n->ylist = arrdup(g->ylist);
	else
	    n->ylist = NULL;

	if ((n->ecount = g->ecount)) {
	    n->expls = ep = (Cexpl *) ncalloc((n->ecount + 1) *
					      sizeof(Cexpl));
	    for (eq = g->expls; (o = *eq); eq++, ep++) {
		*ep = e = (Cexpl) ncalloc(sizeof(struct cexpl));
		e->count = (fi ? o->fcount : o->count);
		e->str = ztrdup(o->str);
	    }
	    *ep = NULL;
	} else
	    n->expls = NULL;

	n->widths = NULL;

	g = g->next;
    }
    for (g = pmatches; g; g = g->next) {
	g->nbrbeg = nbrbeg;
	g->nbrend = nbrend;
	for (rn = 1, q = g->matches; *q; q++) {
	    (*q)->rnum = rn++;
	    (*q)->gnum = mn++;
	}
    }
    hasperm = 1;
    permmnum = mn - 1;
    permgnum = gn - 1;

    return fi;
}

/* Return the real number of matches. */

/**/
zlong
num_matches(int normal)
{
    int alt;

    PERMALLOC {
	alt = permmatches(0);
    } LASTALLOC;

    if (normal)
	return (alt ? 0 : nmatches);
    else
	return (alt ? nmatches : 0);
}

/* Return the number of screen lines needed for the list. */

/**/
zlong
list_lines(void)
{
    Cmgroup oam;

    PERMALLOC {
	permmatches(0);
    } LASTALLOC;

    oam = amatches;
    amatches = pmatches;
    listdat.valid = 0;
    calclist();
    listdat.valid = 0;
    amatches = oam;

    return listdat.nlines;
}

/**/
void
comp_list(char *v)
{
    zsfree(complist);
    complist = ztrdup(v);

    onlyexpl = (v && strstr(v, "expl"));
}

/**/

/* This frees one match. */

/**/
static void
freematch(Cmatch m, int nbeg, int nend)
{
    if (!m) return;

    zsfree(m->str);
    zsfree(m->ipre);
    zsfree(m->ripre);
    zsfree(m->isuf);
    zsfree(m->ppre);
    zsfree(m->psuf);
    zsfree(m->pre);
    zsfree(m->suf);
    zsfree(m->prpre);
    zsfree(m->rems);
    zsfree(m->remf);
    zsfree(m->disp);
    zfree(m->brpl, nbeg * sizeof(int));
    zfree(m->brsl, nend * sizeof(int));

    zfree(m, sizeof(m));
}

/* This frees the groups of matches. */

/**/
void
freematches(Cmgroup g)
{
    Cmgroup n;
    Cmatch *m;
    Cexpl *e;

    while (g) {
	n = g->next;
	
	for (m = g->matches; *m; m++)
	    freematch(*m, g->nbrbeg, g->nbrend);

	if (g->ylist)
	    freearray(g->ylist);

	if ((e = g->expls)) {
	    while (*e) {
		zsfree((*e)->str);
		free(*e);
		e++;
	    }
	    free(g->expls);
	}
	free(g);

	g = n;
    }
}

/* Insert the given string into the command line.  If move is non-zero, *
 * the cursor position is changed and len is the length of the string   *
 * to insert (if it is -1, the length is calculated here).              *
 * The last argument says if we should quote the string.                */

/**/
static int
inststrlen(char *str, int move, int len)
{
    if (!len || !str)
	return 0;
    if (len == -1)
	len = strlen(str);
    spaceinline(len);
    strncpy((char *)(line + cs), str, len);
    if (move)
	cs += len;
    return len;
}

/* This cuts the cline list before the stuff that isn't worth
 * inserting in the line. */

static Cline
cut_cline(Cline l)
{
    Cline q, p, e = NULL, maxp = NULL;
    int sum = 0, max = 0, tmp, ls = 0;

    /* If no match was added with matching, we don't really know
     * which parts of the unambiguous string are worth keeping,
     * so for now we keep everything (in the hope that this
     * produces a string containing at least everything that was 
     * originally on the line). */

    if (!hasmatched) {
	cline_setlens(l, 0);
	return l;
    }
    e = l = cp_cline(l, 0);

    /* First, search the last struct for which we have something on
     * the line. Anything before that is kept. */

    for (q = NULL, p = l; p; p = p->next) {
	if (p->orig || p->olen || !(p->flags & CLF_NEW))
	    e = p->next;
	if (!p->suffix && (p->wlen || p->llen || p->prefix))
	    q = p;
    }
    if (!e && q && !q->orig && !q->olen && (q->flags & CLF_MISS) &&
	!(q->flags & CLF_MATCHED) && (q->word ? q->wlen : q->llen) < 3) {
	q->word = q->line = NULL;
	q->wlen = q->llen = 0;
    }
    /* Then keep all structs without missing characters. */

    while (e && !(e->flags & CLF_MISS))
	e = e->next;

    if (e) {
	/* Then we see if there is another struct with missing
	 * characters. If not, we keep the whole list. */

	for (p = e->next; p && !(p->flags & CLF_MISS); p = p->next);

	if (p) {
	    for (p = e; p; p = p->next) {
		if (!(p->flags & CLF_MISS))
		    sum += p->max;
		else {
		    tmp = cline_sublen(p);
		    if (tmp > 2 && tmp > ((p->max + p->min) >> 1))
			sum += tmp - (p->max - tmp);
		    else if (tmp < p->min)
			sum -= (((p->max + p->min) >> 1) - tmp) << (tmp < 2);
		}
		if (sum > max) {
		    max = sum;
		    maxp = p;
		}
	    }
	    if (max)
		e = maxp;
	    else {
		int len = 0;

		cline_setlens(l, 0);
		ls = 1;

		for (p = e; p; p = p->next)
		    len += p->max;

		if (len > ((minmlen << 1) / 3))
		    return l;
	    }
	    e->line = e->word = NULL;
	    e->llen = e->wlen = e->olen = 0;
	    e->next = NULL;
	}
    }
    if (!ls)
	cline_setlens(l, 0);

    return l;
}

/* This builds the unambiguous string. If ins is non-zero, it is
 * immediatly inserted in the line. Otherwise csp is used to return
 * the relative cursor position in the string returned. */

static char *
cline_str(Cline l, int ins, int *csp)
{
    Cline s;
    int ocs = cs, ncs, pcs, scs, pm, pmax, pmm, sm, smax, smm, d, dm, mid;
    int i, j, li = 0, cbr;
    Brinfo brp, brs;

    l = cut_cline(l);

    pmm = smm = dm = 0;
    pm = pmax = sm = smax = d = mid = cbr = -1;

    /* Get the information about the brace beginning and end we have
     * to re-insert. */
    if (ins) {
	Brinfo bp;
	int olen = we - wb;

	if ((brp = brbeg)) {
	    for (bp = brbeg; bp; bp = bp->next) {
		bp->curpos = (hasunqu ? bp->pos : bp->qpos);
		olen -= strlen(bp->str);
	    }
	}
	if ((brs = lastbrend)) {
	    for (bp = brend; bp; bp = bp->next)
		olen -= strlen(bp->str);

	    for (bp = brend; bp; bp = bp->next)
		bp->curpos = olen - (hasunqu ? bp->pos : bp->qpos);
	}
	while (brp && !brp->curpos) {
	    inststrlen(brp->str, 1, -1);
	    brp = brp->next;
	}
	while (brs && !brs->curpos) {
	    if (cbr < 0)
		cbr = cs;
	    inststrlen(brs->str, 1, -1);
	    brs = brs->prev;
	}
    }
    /* Walk through the top-level cline list. */
    while (l) {
	/* Insert the original string if no prefix. */
	if (l->olen && !(l->flags & CLF_SUF) && !l->prefix) {
	    pcs = cs + l->olen;
	    inststrlen(l->orig, 1, l->olen);
	} else {
	    /* Otherwise insert the prefix. */
	    for (s = l->prefix; s; s = s->next) {
		pcs = cs + s->llen;
		if (s->flags & CLF_LINE)
		    inststrlen(s->line, 1, s->llen);
		else
		    inststrlen(s->word, 1, s->wlen);
		scs = cs;

		if ((s->flags & CLF_DIFF) && (!dm || (s->flags & CLF_MATCHED))) {
		    d = cs; dm = s->flags & CLF_MATCHED;
		}
		li += s->llen;
	    }
	}
	if (ins) {
	    int ocs, bl;

	    while (brp && li >= brp->curpos) {
		ocs = cs;
		bl = strlen(brp->str);
		cs = pcs - (li - brp->curpos);
		inststrlen(brp->str, 1, bl);
		cs = ocs + bl;
		pcs += bl;
		scs += bl;
		brp = brp->next;
	    }
	}
	/* Remember the position if this is the first prefix with
	 * missing characters. */
	if ((l->flags & CLF_MISS) && !(l->flags & CLF_SUF) &&
	    ((pmax < (l->min - l->max) && (!pmm || (l->flags & CLF_MATCHED))) ||
	     ((l->flags & CLF_MATCHED) && !pmm))) {
	    pm = cs; pmax = l->min - l->max; pmm = l->flags & CLF_MATCHED;
	}
	if (ins) {
	    int ocs, bl;

	    while (brs && li >= brs->curpos) {
		ocs = cs;
		bl = strlen(brs->str);
		cs = scs - (li - brs->curpos);
		if (cbr < 0)
		    cbr = cs;
		inststrlen(brs->str, 1, bl);
		cs = ocs + bl;
		pcs += bl;
		brs = brs->prev;
	    }
	}
	pcs = cs;
	/* Insert the anchor. */
	if (l->flags & CLF_LINE)
	    inststrlen(l->line, 1, l->llen);
	else
	    inststrlen(l->word, 1, l->wlen);
	scs = cs;
	if (ins) {
	    int ocs, bl;

	    li += l->llen;

	    while (brp && li >= brp->curpos) {
		ocs = cs;
		bl = strlen(brp->str);
		cs = pcs + l->llen - (li - brp->curpos);
		inststrlen(brp->str, 1, bl);
		cs = ocs + bl;
		pcs += bl;
		scs += bl;
		brp = brp->next;
	    }
	}
	/* Remember the cursor position for suffixes and mids. */
	if (l->flags & CLF_MISS) {
	    if (l->flags & CLF_MID)
		mid = cs;
	    else if ((l->flags & CLF_SUF) && 
		     ((smax < (l->min - l->max) &&
		       (!smm || (l->flags & CLF_MATCHED))) ||
		      ((l->flags & CLF_MATCHED) && !smm))) {
		sm = cs; smax = l->min - l->max; smm = l->flags & CLF_MATCHED;
	    }
	}
	if (ins) {
	    int ocs, bl;

	    while (brs && li >= brs->curpos) {
		ocs = cs;
		bl = strlen(brs->str);
		cs = scs - (li - brs->curpos);
		if (cbr < 0)
		    cbr = cs;
		inststrlen(brs->str, 1, bl);
		cs = ocs + bl;
		pcs += bl;
		brs = brs->prev;
	    }
	}
	/* And now insert the suffix or the original string. */
	if (l->olen && (l->flags & CLF_SUF) && !l->suffix) {
	    pcs = cs;
	    inststrlen(l->orig, 1, l->olen);
	    if (ins) {
		int ocs, bl;

		li += l->olen;

		while (brp && li >= brp->curpos) {
		    ocs = cs;
		    bl = strlen(brp->str);
		    cs = pcs + l->olen - (li - brp->curpos);
		    inststrlen(brp->str, 1, bl);
		    cs = ocs + bl;
		    pcs += bl;
		    brp = brp->next;
		}
		while (brs && li >= brs->curpos) {
		    ocs = cs;
		    bl = strlen(brs->str);
		    cs = pcs + l->olen - (li - brs->curpos);
		    if (cbr < 0)
			cbr = cs;
		    inststrlen(brs->str, 1, bl);
		    cs = ocs + bl;
		    pcs += bl;
		    brs = brs->prev;
		}
	    }
	} else {
	    Cline js = NULL;

	    for (j = -1, i = 0, s = l->suffix; s; s = s->next) {
		if (j < 0 && (s->flags & CLF_DIFF))
		    j = i, js = s;
		pcs = cs;
		if (s->flags & CLF_LINE) {
		    inststrlen(s->line, 0, s->llen);
		    i += s->llen; scs = cs + s->llen;
		} else {
		    inststrlen(s->word, 0, s->wlen);
		    i += s->wlen; scs = cs + s->wlen;
		}
		if (ins) {
		    int ocs, bl;

		    li += s->llen;

		    while (brp && li >= brp->curpos) {
			ocs = cs;
			bl = strlen(brp->str);
			cs = pcs + (li - brp->curpos);
			inststrlen(brp->str, 1, bl);
			cs = ocs + bl;
			pcs += bl;
			scs += bl;
			brp = brp->next;
		    }
		    while (brs && li >= brs->curpos) {
			ocs = cs;
			bl = strlen(brs->str);
			cs = scs - (li - brs->curpos);
			if (cbr < 0)
			    cbr = cs;
			inststrlen(brs->str, 1, bl);
			cs = ocs + bl;
			pcs += bl;
			brs = brs->prev;
		    }
		}
	    }
	    cs += i;
	    if (j >= 0 && (!dm || (js->flags & CLF_MATCHED))) {
		d = cs - j; dm = js->flags & CLF_MATCHED;
	    }
	}
	l = l->next;
    }
    if (ins) {
	int ocs = cs;

	for (; brp; brp = brp->next)
	    inststrlen(brp->str, 1, -1);
	for (; brs; brs = brs->prev) {
	    if (cbr < 0)
		cbr = cs;
	    inststrlen(brs->str, 1, -1);
	}
	if (mid >= ocs)
	    mid += cs - ocs;
	if (pm >= ocs)
	    pm += cs - ocs;
	if (sm >= ocs)
	    sm += cs - ocs;
	if (d >= ocs)
	    d += cs - ocs;
    }
    /* This calculates the new cursor position. If we had a mid cline
     * with missing characters, we take this, otherwise if we have a
     * prefix with missing characters, we take that, the same for a
     * suffix, and finally a place where the matches differ. */
    ncs = (cbr >= 0 ? cbr :
	   (mid >= 0 ? mid :
	    (pm >= 0 ? pm : (sm >= 0 ? sm : (d >= 0 ? d : cs)))));

    if (!ins) {
	/* We always inserted the string in the line. If that was not
	 * requested, we copy it and remove from the line. */
	char *r = zalloc((i = cs - ocs) + 1);

	memcpy(r, (char *) (line + ocs), i);
	r[i] = '\0';
	cs = ocs;
	foredel(i);

	*csp = ncs - ocs;

	return r;
    }
    lastend = cs;
    cs = ncs;

    return NULL;
}

/* This is a utility function using the function above to allow access
 * to the unambiguous string and cursor position via compstate. */

/**/
char *
unambig_data(int *cp)
{
    static char *scache = NULL;
    static int ccache;

    if (mnum && ainfo) {
	if (mnum != unambig_mnum) {
	    zsfree(scache);
	    scache = cline_str((ainfo->count ? ainfo->line : fainfo->line),
			       0, &ccache);
	}
    } else if (mnum != unambig_mnum || !ainfo || !scache) {
	zsfree(scache);
	scache = ztrdup("");
	ccache = 0;
    }
    unambig_mnum = mnum;
    if (cp)
	*cp = ccache + 1;

    return scache;
}

/* Insert the given match. This returns the number of characters inserted.
 * scs is used to return the position where a automatically created suffix
 * has to be inserted. */

/**/
static int
instmatch(Cmatch m, int *scs)
{
    int l, r = 0, ocs, a = cs, brb = 0, bradd, *brpos;
    Brinfo bp;

    zsfree(lastprebr);
    zsfree(lastpostbr);
    lastprebr = lastpostbr = NULL;

    /* Ignored prefix. */
    if (m->ipre) {
	char *p = m->ipre + (menuacc ? m->qipl : 0);

	inststrlen(p, 1, (l = strlen(p)));
	r += l;
    }
    /* -P prefix. */
    if (m->pre) {
	inststrlen(m->pre, 1, (l = strlen(m->pre)));
	r += l;
    }
    /* Path prefix. */
    if (m->ppre) {
	inststrlen(m->ppre, 1, (l = strlen(m->ppre)));
	r += l;
    }
    /* The string itself. */
    inststrlen(m->str, 1, (l = strlen(m->str)));
    r += l;
    ocs = cs;
    /* Re-insert the brace beginnings, if any. */
    if (brbeg) {
	int pcs = cs;

	l = 0;
	for (bp = brbeg, brpos = m->brpl,
		 bradd = (m->pre ? strlen(m->pre) : 0);
	     bp; bp = bp->next, brpos++) {
	    cs = a + *brpos + bradd;
	    pcs = cs;
	    l = strlen(bp->str);
	    bradd += l;
	    brpcs = cs;
	    inststrlen(bp->str, 1, l);
	    r += l;
	    ocs += l;
	}
	lastprebr = (char *) zalloc(pcs - a + 1);
	memcpy(lastprebr, (char *) line + a, pcs - a);
	lastprebr[pcs - a] = '\0';
	cs = ocs;
    }
    /* Path suffix. */
    if (m->psuf) {
	inststrlen(m->psuf, 1, (l = strlen(m->psuf)));
	r += l;
    }
    /* Re-insert the brace end. */
    if (brend) {
	a = cs;
	for (bp = brend, brpos = m->brsl, bradd = 0; bp; bp = bp->next, brpos++) {
	    cs = a - *brpos;
	    ocs = brscs = cs;
	    l = strlen(bp->str);
	    bradd += l;
	    inststrlen(bp->str, 1, l);
	    brb = cs;
	    r += l;
	}
	cs = a + bradd;
	if (scs)
	    *scs = ocs;
    } else {
	brscs = -1;

	if (scs)
	    *scs = cs;
    }
    /* -S suffix */
    if (m->suf) {
	inststrlen(m->suf, 1, (l = strlen(m->suf)));
	r += l;
    }
    /* ignored suffix */
    if (m->isuf) {
	inststrlen(m->isuf, 1, (l = strlen(m->isuf)));
	r += l;
    }
    if (brend) {
	lastpostbr = (char *) zalloc(cs - brb + 1);
	memcpy(lastpostbr, (char *) line + brb, cs - brb);
	lastpostbr[cs - brb] = '\0';
    }
    lastend = cs;
    cs = ocs;

    return r;
}

/* Check if the match has the given prefix/suffix before/after the
 * braces. */

/**/
int
hasbrpsfx(Cmatch m, char *pre, char *suf)
{
    char *op = lastprebr, *os = lastpostbr;
    VARARR(char, oline, ll);
    int oll = ll, ocs = cs, ole = lastend, opcs = brpcs, oscs = brscs, ret;

    memcpy(oline, line, ll);

    lastprebr = lastpostbr = NULL;

    instmatch(m, NULL);

    cs = 0;
    foredel(ll);
    spaceinline(oll);
    memcpy(line, oline, oll);
    cs = ocs;
    lastend = ole;
    brpcs = opcs;
    brscs = oscs;

    ret = (((!pre && !lastprebr) ||
	    (pre && lastprebr && !strcmp(pre, lastprebr))) &&
	   ((!suf && !lastpostbr) ||
	    (suf && lastpostbr && !strcmp(suf, lastpostbr))));

    zsfree(lastprebr);
    zsfree(lastpostbr);
    lastprebr = op;
    lastpostbr = os;

    return ret;
}

/* Handle the case were we found more than one match. */

/**/
static int
do_ambiguous(void)
{
    int ret = 0;

    menucmp = menuacc = 0;

    /* If we have to insert the first match, call do_single().  This is *
     * how REC_EXACT takes effect.  We effectively turn the ambiguous   *
     * completion into an unambiguous one.                              */
    if (ainfo && ainfo->exact == 1 && useexact && !(fromcomp & FC_LINE)) {
	minfo.cur = NULL;
	do_single(ainfo->exactm);
	invalidatelist();
	return ret;
    }
    /* Setting lastambig here means that the completion is ambiguous and *
     * AUTO_MENU might want to start a menu completion next time round,  *
     * but this might be overridden below if we can complete an          *
     * unambiguous prefix.                                               */
    lastambig = 1;

    if (usemenu || (haspattern && comppatinsert &&
		    !strcmp(comppatinsert, "menu"))) {
	/* We are in a position to start using menu completion due to one  *
	 * of the menu completion options, or due to the menu-complete-    *
	 * word command, or due to using GLOB_COMPLETE which does menu-    *
	 * style completion regardless of the setting of the normal menu   *
	 * completion options.                                             */
	do_ambig_menu();
    } else if (ainfo) {
	int atend = (cs == we), la, eq, tcs;

	minfo.cur = NULL;
	minfo.asked = 0;

	fixsuffix();

	/* First remove the old string from the line. */
	cs = wb;
	foredel(we - wb);

	/* Now get the unambiguous string and insert it into the line. */
	cline_str(ainfo->line, 1, NULL);
	if (eparq) {
	    tcs = cs;
	    cs = lastend;
	    for (eq = eparq; eq; eq--)
		inststrlen("\"", 0, 1);
	    cs = tcs;
	}
	/* la is non-zero if listambiguous may be used. Copying and
	 * comparing the line looks like BFI but it is the easiest
	 * solution. Really. */
	la = (ll != origll || strncmp(origline, (char *) line, ll));

	/* If REC_EXACT and AUTO_MENU are set and what we inserted is an  *
	 * exact match, we want menu completion the next time round       *
	 * so we set fromcomp, to ensure that the word on the line is not *
	 * taken as an exact match. Also we remember if we just moved the *
	 * cursor into the word.                                          */
	fromcomp = ((isset(AUTOMENU) ? FC_LINE : 0) |
		    ((atend && cs != lastend) ? FC_INWORD : 0));

	/* Probably move the cursor to the end. */
	if (movetoend == 3)
	    cs = lastend;

	/* If the LIST_AMBIGUOUS option (meaning roughly `show a list only *
	 * if the completion is completely ambiguous') is set, and some    *
	 * prefix was inserted, return now, bypassing the list-displaying  *
	 * code.  On the way, invalidate the list and note that we don't   *
	 * want to enter an AUTO_MENU imediately.                          */
	if (uselist == 3 && la) {
	    int fc = fromcomp;

	    invalidatelist();
	    fromcomp = fc;
	    lastambig = 0;
	    clearlist = 1;
	    return ret;
	}
    } else
	return ret;

    /* At this point, we might want a completion listing.  Show the listing *
     * if it is needed.                                                     */
    if (isset(LISTBEEP))
	ret = 1;

    if (uselist && (usemenu != 2 || (!listshown && !oldlist)) &&
	((!showinglist && (!listshown || !oldlist)) ||
	 (usemenu == 3 && !oldlist)) &&
	(smatches >= 2 || (compforcelist && *compforcelist)))
	showinglist = -2;

    return ret;
}

/* This is a stat that ignores backslashes in the filename.  The `ls' *
 * parameter says if we have to do lstat() or stat().  I think this   *
 * should instead be done by use of a general function to expand a    *
 * filename (stripping backslashes), combined with the actual         *
 * (l)stat().                                                         */

/**/
int
ztat(char *nam, struct stat *buf, int ls)
{
    char b[PATH_MAX], *p;

    for (p = b; p < b + sizeof(b) - 1 && *nam; nam++)
	if (*nam == '\\' && nam[1])
	    *p++ = *++nam;
	else
	    *p++ = *nam;
    *p = '\0';

    return ls ? lstat(b, buf) : stat(b, buf);
}

/* Insert a single match in the command line. */

/**/
void
do_single(Cmatch m)
{
    int l, sr = 0, scs;
    int havesuff = 0;
    int partest = (m->ripre || ((m->flags & CMF_ISPAR) && parpre));
    char *str = m->str, *ppre = m->ppre, *psuf = m->psuf, *prpre = m->prpre;

    if (!prpre) prpre = "";
    if (!ppre) ppre = "";
    if (!psuf) psuf = "";

    fixsuffix();

    if (!minfo.cur) {
	/* We are currently not in a menu-completion, *
	 * so set the position variables.             */
	minfo.pos = wb;
	minfo.we = (movetoend >= 2 || (movetoend == 1 && !menucmp));
	minfo.end = we;
    }
    /* If we are already in a menu-completion or if we have done a *
     * glob completion, we have to delete some of the stuff on the *
     * command line.                                               */
    if (minfo.cur)
	l = minfo.len + minfo.insc;
    else
	l = we - wb;

    minfo.insc = 0;
    cs = minfo.pos;
    foredel(l);

    /* And then we insert the new string. */
    minfo.len = instmatch(m, &scs);
    minfo.end = cs;
    cs = minfo.pos + minfo.len;

    if (m->suf) {
	havesuff = 1;
	minfo.insc = ztrlen(m->suf);
	minfo.len -= minfo.insc;
	if (minfo.we) {
	    minfo.end += minfo.insc;
	    if (m->flags & CMF_REMOVE) {
		makesuffixstr(m->remf, m->rems, minfo.insc);
		if (minfo.insc == 1)
		    suffixlen[STOUC(m->suf[0])] = 1;
	    }
	}
    } else {
	/* There is no user-specified suffix, *
	 * so generate one automagically.     */
	cs = scs;
	if (partest && (m->flags & CMF_PARBR)) {
	    int pq;

	    /*{{*/
	    /* Completing a parameter in braces.  Add a removable `}' suffix. */
	    cs += eparq;
	    for (pq = parq; pq; pq--)
		inststrlen("\"", 1, 1);
	    minfo.insc += parq;
	    inststrlen("}", 1, 1);
	    minfo.insc++;
	    if (minfo.we)
		minfo.end += minfo.insc;
	    if (m->flags & CMF_PARNEST)
		havesuff = 1;
	}
	if (((m->flags & CMF_FILE) || (partest && isset(AUTOPARAMSLASH))) &&
	    cs > 0 && line[cs - 1] != '/') {
	    /* If we have a filename or we completed a parameter name      *
	     * and AUTO_PARAM_SLASH is set, lets see if it is a directory. *
	     * If it is, we append a slash.                                */
	    struct stat buf;
	    char *p;
	    int t = 0;

	    if (m->ipre && m->ipre[0] == '~' && !m->ipre[1])
		t = 1;
	    else {
		/* Build the path name. */
		if (partest && !*psuf && !(m->flags & CMF_PARNEST)) {
		    int ne = noerrs;

		    p = (char *) zhalloc(strlen((m->flags & CMF_ISPAR) ?
						parpre : m->ripre) +
					 strlen(str) + 2);
		    sprintf(p, "%s%s%c",
			    ((m->flags & CMF_ISPAR) ? parpre : m->ripre), str,
			    ((m->flags & CMF_PARBR) ? Outbrace : '\0'));
		    noerrs = 1;
		    parsestr(p);
		    singsub(&p);
		    errflag = 0;
		    noerrs = ne;
		} else {
		    p = (char *) zhalloc(strlen(prpre) + strlen(str) +
				 strlen(psuf) + 3);
		    sprintf(p, "%s%s%s", ((prpre && *prpre) ?
					  prpre : "./"), str, psuf);
		}
		/* And do the stat. */
		t = (!(sr = ztat(p, &buf, 0)) && S_ISDIR(buf.st_mode));
	    }
	    if (t) {
		/* It is a directory, so add the slash. */
		havesuff = 1;
		inststrlen("/", 1, 1);
		minfo.insc++;
		if (minfo.we)
		    minfo.end++;
		if (!menucmp || minfo.we) {
		    if (m->remf || m->rems)
			makesuffixstr(m->remf, m->rems, 1);
		    else if (isset(AUTOREMOVESLASH)) {
			makesuffix(1);
			suffixlen['/'] = 1;
		    }
		}
	    }
	}
	if (!minfo.insc)
	    cs = minfo.pos + minfo.len - m->qisl;
    }
    /* If completing in a brace expansion... */
    if (brbeg) {
	if (havesuff) {
	    /*{{*/
	    /* If a suffix was added, and is removable, let *
	     * `,' and `}' remove it.                       */
	    if (isset(AUTOPARAMKEYS))
		suffixlen[','] = suffixlen['}'] = suffixlen[256];
	} else if (!menucmp) {
	    /*{{*/
	    /* Otherwise, add a `,' suffix, and let `}' remove it. */
	    cs = scs;
	    havesuff = 1;
	    inststrlen(",", 1, 1);
	    minfo.insc++;
	    makesuffix(1);
	    if ((!menucmp || minfo.we) && isset(AUTOPARAMKEYS))
		suffixlen[','] = suffixlen['}'] = 1;
	}
    } else if (!havesuff && (!(m->flags & CMF_FILE) || !sr)) {
	/* If we didn't add a suffix, add a space, unless we are *
	 * doing menu completion or we are completing files and  *
	 * the string doesn't name an existing file.             */
	if (m->autoq && (!m->isuf || m->isuf[0] != m->autoq)) {
	    inststrlen(&(m->autoq), 1, 1);
	    minfo.insc++;
	}
	if (!menucmp && (usemenu != 3 || insspace)) {
	    inststrlen(" ", 1, 1);
	    minfo.insc++;
	    if (minfo.we)
		makesuffix(1);
	}
    }
    if (minfo.we && partest && isset(AUTOPARAMKEYS))
	makeparamsuffix(((m->flags & CMF_PARBR) ? 1 : 0), minfo.insc - parq);

    if ((menucmp && !minfo.we) || !movetoend) {
	cs = minfo.end;
	if (cs + m->qisl == lastend)
	    cs += minfo.insc;
    }
    {
	Cmatch *om = minfo.cur;
	struct chdata dat;

	dat.matches = amatches;
	dat.num = nmatches;
	dat.cur = m;

	if (menucmp)
	    minfo.cur = &m;
	runhookdef(INSERTMATCHHOOK, (void *) &dat);
	minfo.cur = om;
    }
}

/* This maps the value in v into the range [0,m-1], decrementing v
 * if it is non-negative and making negative values count backwards. */

static int
comp_mod(int v, int m)
{
    if (v >= 0)
	v--;
    if (v >= 0)
	return v % m;
    else {
	while (v < 0)
	    v += m;
	return v;
    }
}

/* This handles the beginning of menu-completion. */

/**/
static void
do_ambig_menu(void)
{
    Cmatch *mc;

    if (usemenu != 3) {
	menucmp = 1;
	menuacc = 0;
	minfo.cur = NULL;
    } else {
	if (oldlist) {
	    if (oldins && minfo.cur)
		acceptlast();
	} else
	    minfo.cur = NULL;
    }
    if (insgroup) {
	insgnum = comp_mod(insgnum, lastpermgnum);
	for (minfo.group = amatches;
	     minfo.group && (minfo.group)->num != insgnum + 1;
	     minfo.group = (minfo.group)->next);
	if (!minfo.group || !(minfo.group)->mcount) {
	    minfo.cur = NULL;
	    minfo.asked = 0;
	    return;
	}
	insmnum = comp_mod(insmnum, (minfo.group)->mcount);
    } else {
	insmnum = comp_mod(insmnum, lastpermmnum);
	for (minfo.group = amatches;
	     minfo.group && (minfo.group)->mcount <= insmnum;
	     minfo.group = (minfo.group)->next)
	    insmnum -= (minfo.group)->mcount;
	if (!minfo.group) {
	    minfo.cur = NULL;
	    minfo.asked = 0;
	    return;
	}
    }
    mc = (minfo.group)->matches + insmnum;
    do_single(*mc);
    minfo.cur = mc;
}

/* Return the length of the common prefix of s and t. */

/**/
int
pfxlen(char *s, char *t)
{
    int i = 0;

    while (*s && *s == *t)
	s++, t++, i++;
    return i;
}

/* Return the length of the common suffix of s and t. */

#if 0
static int
sfxlen(char *s, char *t)
{
    if (*s && *t) {
	int i = 0;
	char *s2 = s + strlen(s) - 1, *t2 = t + strlen(t) - 1;

	while (s2 >= s && t2 >= t && *s2 == *t2)
	    s2--, t2--, i++;

	return i;
    } else
	return 0;
}
#endif

/* This is used to print the explanation string. *
 * It returns the number of lines printed.       */

/**/
int
printfmt(char *fmt, int n, int dopr, int doesc)
{
    char *p = fmt, nc[DIGBUFSIZE];
    int l = 0, cc = 0, b = 0, s = 0, u = 0, m;

    for (; *p; p++) {
	/* Handle the `%' stuff (%% == %, %n == <number of matches>). */
	if (doesc && *p == '%') {
	    if (*++p) {
		m = 0;
		switch (*p) {
		case '%':
		    if (dopr)
			putc('%', shout);
		    cc++;
		    break;
		case 'n':
		    sprintf(nc, "%d", n);
		    if (dopr)
			fprintf(shout, nc);
		    cc += strlen(nc);
		    break;
		case 'B':
		    b = 1;
		    if (dopr)
			tcout(TCBOLDFACEBEG);
		    break;
		case 'b':
		    b = 0; m = 1;
		    if (dopr)
			tcout(TCALLATTRSOFF);
		    break;
		case 'S':
		    s = 1;
		    if (dopr)
			tcout(TCSTANDOUTBEG);
		    break;
		case 's':
		    s = 0; m = 1;
		    if (dopr)
			tcout(TCSTANDOUTEND);
		    break;
		case 'U':
		    u = 1;
		    if (dopr)
			tcout(TCUNDERLINEBEG);
		    break;
		case 'u':
		    u = 0; m = 1;
		    if (dopr)
			tcout(TCUNDERLINEEND);
		    break;
		case '{':
		    for (p++; *p && (*p != '%' || p[1] != '}'); p++, cc++)
			if (dopr)
			    putc(*p, shout);
		    if (*p)
			p++;
		    else
			p--;
		    break;
		}
		if (dopr && m) {
		    if (b)
			tcout(TCBOLDFACEBEG);
		    if (s)
			tcout(TCSTANDOUTBEG);
		    if (u)
			tcout(TCUNDERLINEBEG);
		}
	    } else
		break;
	} else {
	    cc++;
	    if (*p == '\n') {
		if (dopr) {
		    if (tccan(TCCLEAREOL))
			tcout(TCCLEAREOL);
		    else {
			int s = columns - 1 - (cc % columns);

			while (s-- > 0)
			    putc(' ', shout);
		    }
		}
		l += 1 + (cc / columns);
		cc = 0;
	    }
	    if (dopr)
		putc(*p, shout);
	}
    }
    if (dopr) {
	if (tccan(TCCLEAREOL))
	    tcout(TCCLEAREOL);
	else {
	    int s = columns - 1 - (cc % columns);

	    while (s-- > 0)
		putc(' ', shout);
	}
    }
    return l + (cc / columns);
}

/* This skips over matches that are not to be listed. */

/**/
Cmatch *
skipnolist(Cmatch *p)
{
    while (*p && (((*p)->flags & (CMF_NOLIST | CMF_HIDE)) ||
		  ((*p)->disp && ((*p)->flags & (CMF_DISPLINE | CMF_HIDE)))))
	p++;

    return p;
}

/* List the matches.  Note that the list entries are metafied. */

/**/
void
listmatches(void)
{
    struct chdata dat;

#ifdef DEBUG
    /* Sanity check */
    if (!validlist) {
	showmsg("BUG: listmatches called with bogus list");
	return;
    }
#endif

    dat.matches = amatches;
    dat.num = nmatches;
    dat.cur = NULL;
    runhookdef(LISTMATCHESHOOK, (void *) &dat);
}

/**/
void
calclist(void)
{
    Cmgroup g;
    Cmatch *p, m;
    Cexpl *e;
    int hidden = 0, nlist = 0, nlines = 0, add = 2 + isset(LISTTYPES);
    int max = 0, i;
    VARARR(int, mlens, nmatches + 1);

    if (listdat.valid && onlyexpl == listdat.onlyexpl &&
	menuacc == listdat.menuacc &&
	lines == listdat.lines && columns == listdat.columns)
	return;

    for (g = amatches; g; g = g->next) {
	char **pp = g->ylist;
	int nl = 0, l, glong = 1, gshort = columns, ndisp = 0, totl = 0;

	if (!onlyexpl && pp) {
	    /* We have an ylist, lets see, if it contains newlines. */
	    hidden = 1;
	    while (!nl && *pp)
		nl = !!strchr(*pp++, '\n');

	    pp = g->ylist;
	    if (nl || !pp[1]) {
		/* Yup, there are newlines, count lines. */
		char *nlptr, *sptr;

		g->flags |= CGF_LINES;
		hidden = 1;
		while ((sptr = *pp)) {
		    while (sptr && *sptr) {
			nlines += (nlptr = strchr(sptr, '\n'))
			    ? 1 + (nlptr-sptr)/columns
			    : strlen(sptr)/columns;
			sptr = nlptr ? nlptr+1 : NULL;
		    }
		    nlines++;
		    pp++;
		}
		nlines--;
	    } else {
		while (*pp) {
		    l = strlen(*pp);
		    ndisp++;
		    if (l > glong)
			glong = l;
		    if (l < gshort)
			gshort = l;
		    totl += l;
		    nlist++;
		    pp++;
		}
	    }
	} else if (!onlyexpl) {
	    for (p = g->matches; (m = *p); p++) {
		if (menuacc && !hasbrpsfx(m, minfo.prebr, minfo.postbr)) {
		    m->flags |= CMF_HIDE;
		    continue;
		}
		m->flags &= ~CMF_HIDE;

		if (m->disp) {
		    if (m->flags & CMF_DISPLINE) {
			nlines += 1 + printfmt(m->disp, 0, 0, 0);
			g->flags |= CGF_HASDL;
		    } else {
			l = niceztrlen(m->disp);
			ndisp++;
			if (l > glong)
			    glong = l;
			if (l < gshort)
			    gshort = l;
			totl += l;
			mlens[m->gnum] = l;
		    }
		    nlist++;
		} else if (!(m->flags & CMF_NOLIST)) {
		    l = niceztrlen(m->str);
		    ndisp++;
		    if (l > glong)
			glong = l;
		    if (l < gshort)
			gshort = l;
		    totl += l;
		    mlens[m->gnum] = l;
		    nlist++;
		} else
		    hidden = 1;
	    }
	}
	if ((e = g->expls)) {
	    while (*e) {
		if ((*e)->count)
		    nlines += 1 + printfmt((*e)->str, (*e)->count, 0, 1);
		e++;
	    }
	}
	g->totl = totl + (ndisp * add);
	g->dcount = ndisp;
	g->width = glong + add;
	g->shortest = gshort + add;
	if ((g->cols = columns / g->width) > g->dcount)
	    g->cols = g->dcount;
	if (g->cols) {
	    i = g->cols * g->width - add;
	    if (i > max)
		max = i;
	}
    }
    if (!onlyexpl) {
	for (g = amatches; g; g = g->next) {
	    char **pp;
	    int glines = 0;

	    zfree(g->widths, 0);
	    g->widths = NULL;

	    if ((pp = g->ylist)) {
		if (!(g->flags & CGF_LINES)) {
		    if (g->cols) {
			glines += (arrlen(pp) + g->cols - 1) / g->cols;
			if (g->cols > 1)
			    g->width += (max - (g->width * g->cols - add)) / g->cols;
		    } else {
			g->cols = 1;
			g->width = 1;
			
			while (*pp)
			    glines += 1 + (strlen(*pp++) / columns);
		    }
		}
	    } else {
		if (g->cols) {
		    glines += (g->dcount + g->cols - 1) / g->cols;
		    if (g->cols > 1)
			g->width += (max - (g->width * g->cols - add)) / g->cols;
		} else if (!(g->flags & CGF_LINES)) {
		    g->cols = 1;
		    g->width = 0;
		    
		    for (p = g->matches; (m = *p); p++)
			if (!(m->flags & CMF_HIDE)) {
			    if (m->disp) {
				if (!(m->flags & CMF_DISPLINE))
				    glines += 1 + (mlens[m->gnum] / columns);
			    } else if (!(m->flags & CMF_NOLIST))
				glines += 1 + ((1 + mlens[m->gnum]) / columns);
			}
		}
	    }
	    g->lins = glines;
	    nlines += glines;
	}
    }
    if (!onlyexpl && isset(LISTPACKED)) {
	char **pp;
	int *ws, tlines, tline, tcols, maxlen, nth, width;

	for (g = amatches; g; g = g->next) {
	    ws = g->widths = (int *) zalloc(columns * sizeof(int));
	    memset(ws, 0, columns * sizeof(int));
	    tlines = g->lins;
	    tcols = g->cols;
	    width = 0;

	    if ((pp = g->ylist)) {
		if (!(g->flags & CGF_LINES)) {
		    int yl = arrlen(pp), i;
		    VARARR(int, ylens, yl);

		    for (i = 0; *pp; i++, pp++)
			ylens[i] = strlen(*pp) + add;

		    if (isset(LISTROWSFIRST)) {
			int count, tcol, first, maxlines = 0, llines;

			for (tcols = columns / g->shortest; tcols > g->cols;
			     tcols--) {
			    for (nth = first = maxlen = width = maxlines =
				     llines = tcol = 0,
				     count = g->dcount;
				 count > 0; count--) {
				if (ylens[nth] > maxlen)
				    maxlen = ylens[nth];
				nth += tcols;
				tlines++;
				if (nth >= g->dcount) {
				    if ((width += maxlen) >= columns)
					break;
				    ws[tcol++] = maxlen;
				    maxlen = 0;
				    nth = ++first;
				    if (llines > maxlines)
					maxlines = llines;
				    llines = 0;
				}
			    }
			    if (nth < yl) {
				ws[tcol++] = maxlen;
				width += maxlen;
			    }
			    if (!count && width < columns)
				break;
			}
			if (tcols > g->cols)
			    tlines = maxlines;
		    } else {
			for (tlines = ((g->totl + columns) / columns);
			     tlines < g->lins; tlines++) {
			    for (pp = g->ylist, nth = tline = width =
				     maxlen = tcols = 0;
				 *pp; nth++, pp++) {
				if (ylens[nth] > maxlen)
				    maxlen = ylens[nth];
				if (++tline == tlines) {
				    if ((width += maxlen) >= columns)
					break;
				    ws[tcols++] = maxlen;
				    maxlen = tline = 0;
				}
			    }
			    if (tline) {
				ws[tcols++] = maxlen;
				width += maxlen;
			    }
			    if (nth == yl && width < columns)
				break;
			}
		    }
		}
	    } else if (g->width) {
		if (isset(LISTROWSFIRST)) {
		    int addlen, count, tcol, maxlines = 0, llines, i;
		    Cmatch *first;

		    for (tcols = columns / g->shortest; tcols > g->cols;
			 tcols--) {
			p = first = skipnolist(g->matches);
			for (maxlen = width = maxlines = llines = tcol = 0,
				 count = g->dcount;
			     count > 0; count--) {
			    m = *p;
			    addlen = mlens[m->gnum] + add;
			    if (addlen > maxlen)
				maxlen = addlen;
			    for (i = tcols; i && *p; i--)
				p = skipnolist(p + 1);

			    llines++;
			    if (!*p) {
				if (llines > maxlines)
				    maxlines = llines;
				llines = 0;

				if ((width += maxlen) >= columns)
				    break;
				ws[tcol++] = maxlen;
				maxlen = 0;

				p = first = skipnolist(first + 1);
			    }
			}
			if (tlines) {
			    ws[tcol++] = maxlen;
			    width += maxlen;
			}
			if (!count && width < columns)
			    break;
		    }
		    if (tcols > g->cols)
			tlines = maxlines;
		} else {
		    int addlen;

		    for (tlines = ((g->totl + columns) / columns);
			 tlines < g->lins; tlines++) {
			for (p = g->matches, nth = tline = width =
				 maxlen = tcols = 0;
			     (m = *p); p++, nth++) {
			    if (!(m->flags &
				  (m->disp ? (CMF_DISPLINE | CMF_HIDE) :
				   (CMF_NOLIST | CMF_HIDE)))) {
				addlen = mlens[m->gnum] + add;
				if (addlen > maxlen)
				    maxlen = addlen;
				if (++tline == tlines) {
				    if ((width += maxlen) >= columns)
					break;
				    ws[tcols++] = maxlen;
				    maxlen = tline = 0;
				}
			    }
			}
			if (tline) {
			    ws[tcols++] = maxlen;
			    width += maxlen;
			}
			if (nth == g->dcount && width < columns)
			    break;
		    }
		}
	    }
	    if (tlines == g->lins) {
		zfree(ws, columns * sizeof(int));
		g->widths = NULL;
	    } else {
		nlines += tlines - g->lins;
		g->lins = tlines;
		g->cols = tcols;
		g->totl = width;
		width -= add;
		if (width > max)
		    max = width;
	    }
	}
	for (g = amatches; g; g = g->next) {
	    if (g->widths) {
		int *p, a = (max - g->totl + add) / g->cols;

		for (i = g->cols, p = g->widths; i; i--, p++)
		    *p += a;
	    } else if (g->width && g->cols > 1)
		g->width += (max - (g->width * g->cols - add)) / g->cols;
	}
    }
    listdat.valid = 1;
    listdat.hidden = hidden;
    listdat.nlist = nlist;
    listdat.nlines = nlines;
    listdat.menuacc = menuacc;
    listdat.onlyexpl = onlyexpl;
    listdat.columns = columns;
    listdat.lines = lines;
}

/**/
int asklist(void)
{
    /* Set the cursor below the prompt. */
    trashzle();
    showinglist = listshown = 0;

    clearflag = (isset(USEZLE) && !termflags &&
		 complastprompt && *complastprompt);

    /* Maybe we have to ask if the user wants to see the list. */
    if ((!minfo.cur || !minfo.asked) &&
	((complistmax && listdat.nlist > complistmax) ||
	 (!complistmax && listdat.nlines >= lines))) {
	int qup;
	zsetterm();
	qup = printfmt("zsh: do you wish to see all %n possibilities? ",
		       listdat.nlist, 1, 1);
	fflush(shout);
	if (getzlequery() != 'y') {
	    if (clearflag) {
		putc('\r', shout);
		tcmultout(TCUP, TCMULTUP, qup);
		if (tccan(TCCLEAREOD))
		    tcout(TCCLEAREOD);
		tcmultout(TCUP, TCMULTUP, nlnct);
	    } else
		putc('\n', shout);
	    if (minfo.cur)
		minfo.asked = 2;
	    return 1;
	}
	if (clearflag) {
	    putc('\r', shout);
	    tcmultout(TCUP, TCMULTUP, qup);
	    if (tccan(TCCLEAREOD))
		tcout(TCCLEAREOD);
	} else
	    putc('\n', shout);
	settyinfo(&shttyinfo);
	if (minfo.cur)
	    minfo.asked = 1;
    }
    return 0;
}

/**/
int
printlist(int over, CLPrintFunc printm)
{
    Cmgroup g;
    Cmatch *p, m;
    Cexpl *e;
    int pnl = 0, cl = (over ? listdat.nlines : -1);
    int mc = 0, ml = 0, printed = 0;

    if (cl < 2) {
	cl = -1;
	if (tccan(TCCLEAREOD))
	    tcout(TCCLEAREOD);
    }
    g = amatches;
    while (g) {
	char **pp = g->ylist;

	if ((e = g->expls)) {
	    int l;

	    while (*e) {
		if ((*e)->count) {
		    if (pnl) {
			putc('\n', shout);
			pnl = 0;
			ml++;
			if (cl >= 0 && --cl <= 1) {
			    cl = -1;
			    if (tccan(TCCLEAREOD))
				tcout(TCCLEAREOD);
			}
		    }
		    l = printfmt((*e)->str, (*e)->count, 1, 1);
		    ml += l;
		    if (cl >= 0 && (cl -= l) <= 1) {
			cl = -1;
			if (tccan(TCCLEAREOD))
			    tcout(TCCLEAREOD);
		    }
		    pnl = 1;
		}
		e++;
	    }
	}
	if (!listdat.onlyexpl && pp && *pp) {
	    if (pnl) {
		putc('\n', shout);
		pnl = 0;
		ml++;
		if (cl >= 0 && --cl <= 1) {
		    cl = -1;
		    if (tccan(TCCLEAREOD))
			tcout(TCCLEAREOD);
		}
	    }
	    if (g->flags & CGF_LINES) {
		while (*pp) {
		    zputs(*pp, shout);
		    if (*++pp)
			putc('\n', shout);
		}
	    } else {
		int n = g->lcount, nl, nc, i, a;
		char **pq;

		nl = nc = g->lins;

		while (n && nl--) {
		    i = g->cols;
		    mc = 0;
		    pq = pp;
		    while (n && i--) {
			if (pq - g->ylist >= g->lcount)
			    break;
			zputs(*pq, shout);
			if (i) {
			    a = (g->widths ? g->widths[mc] : g->width) -
				strlen(*pq);
			    while (a--)
				putc(' ', shout);
			}
			pq += (isset(LISTROWSFIRST) ? 1 : nc);
			mc++;
			n--;
		    }
		    if (n) {
			putc('\n', shout);
			ml++;
			if (cl >= 0 && --cl <= 1) {
			    cl = -1;
			    if (tccan(TCCLEAREOD))
				tcout(TCCLEAREOD);
			}
		    }
		    pp += (isset(LISTROWSFIRST) ? g->cols : 1);
		}
	    }
	} else if (!listdat.onlyexpl && g->lcount) {
	    int n = g->dcount, nl, nc, i, j, wid;
	    Cmatch *q;

	    nl = nc = g->lins;

	    if (g->flags & CGF_HASDL) {
		for (p = g->matches; (m = *p); p++)
		    if (m->disp && (m->flags & CMF_DISPLINE)) {
			if (pnl) {
			    putc('\n', shout);
			    pnl = 0;
			    ml++;
			    if (cl >= 0 && --cl <= 1) {
				cl = -1;
				if (tccan(TCCLEAREOD))
				    tcout(TCCLEAREOD);
			    }
			}
			printed++;
			printm(g, p, 0, ml, 1, 0, NULL, NULL);
			pnl = 1;
		    }
	    }
	    if (n && pnl) {
		putc('\n', shout);
		pnl = 0;
		ml++;
		if (cl >= 0 && --cl <= 1) {
		    cl = -1;
		    if (tccan(TCCLEAREOD))
			tcout(TCCLEAREOD);
		}
	    }
	    for (p = skipnolist(g->matches); n && nl--;) {
		i = g->cols;
		mc = 0;
		q = p;
		while (n && i--) {
		    wid = (g->widths ? g->widths[mc] : g->width);
		    if (!(m = *q)) {
			printm(g, NULL, mc, ml, (!i), wid, NULL, NULL);
			break;
		    }
		    if (!m->disp && (m->flags & CMF_FILE)) {
			struct stat buf;
			char *pb;

			pb = (char *) zhalloc((m->prpre ? strlen(m->prpre) : 0) +
					     3 + strlen(m->str));
			sprintf(pb, "%s%s", (m->prpre ? m->prpre : "./"),
				m->str);

			if (ztat(pb, &buf, 1))
			    printm(g, q, mc, ml, (!i), wid, NULL, NULL);
			else
			    printm(g, q, mc, ml, (!i), wid, pb, &buf);
		    } else
			printm(g, q, mc, ml, (!i), wid, NULL, NULL);

		    printed++;

		    if (--n)
			for (j = (isset(LISTROWSFIRST) ? 1 : nc); j && *q; j--)
			    q = skipnolist(q + 1);
		    mc++;
		}
		while (i-- > 0)
		    printm(g, NULL, mc++, ml, (!i),
			   (g->widths ? g->widths[mc] : g->width), NULL, NULL);

		if (n) {
		    putc('\n', shout);
		    ml++;
		    if (cl >= 0 && --cl <= 1) {
			cl = -1;
			if (tccan(TCCLEAREOD))
			    tcout(TCCLEAREOD);
		    }
		    if (nl)
			for (j = (isset(LISTROWSFIRST) ? g->cols : 1); j && *p; j--)
			    p = skipnolist(p + 1);
		}
	    }
	}
	if (g->lcount)
	    pnl = 1;
	g = g->next;
    }
    if (clearflag) {
	/* Move the cursor up to the prompt, if always_last_prompt *
	 * is set and all that...                                  */
	if ((ml = listdat.nlines + nlnct - 1) < lines) {
	    tcmultout(TCUP, TCMULTUP, ml);
	    showinglist = -1;
	} else
	    clearflag = 0, putc('\n', shout);
    } else
	putc('\n', shout);
    listshown = (clearflag ? 1 : -1);

    return printed;
}

static void
iprintm(Cmgroup g, Cmatch *mp, int mc, int ml, int lastc, int width,
	char *path, struct stat *buf)
{
    Cmatch m;
    int len = 0;

    if (!mp)
	return;

    m = *mp;
    if (m->disp) {
	if (m->flags & CMF_DISPLINE) {
	    printfmt(m->disp, 0, 1, 0);
	    return;
	}
	nicezputs(m->disp, shout);
	len = niceztrlen(m->disp);
    } else {
	nicezputs(m->str, shout);
	len = niceztrlen(m->str);

	if (isset(LISTTYPES)) {
	    if (buf)
		putc(file_type(buf->st_mode), shout);
	    len++;
	}
    }
    if (!lastc) {
	len = width - len;

	while (len-- > 0)
	    putc(' ', shout);
    }
}

/**/
int
ilistmatches(Hookdef dummy, Chdata dat)
{
    calclist();

    if (!listdat.nlines) {
	showinglist = listshown = 0;
	return 1;
    }
    if (asklist())
	return 0;

    printlist(0, iprintm);

    return 0;
}

/* This is used to print expansions. */

/**/
int
listlist(LinkList l)
{
    struct cmgroup dg;
    int vl = validlist, sm = smatches, nm = nmatches;
    char *oclp = complastprompt;
    Cmgroup am = amatches;

    if (listshown)
	showagain = 1;

    complastprompt = ((zmult == 1) == !!isset(ALWAYSLASTPROMPT) ? "yes" : NULL);
    smatches = 1;
    validlist = 1;
    memset(&dg, 0, sizeof(struct cmgroup));
    dg.ylist = (char **) makearray(l, 0, 1, &(dg.lcount), NULL, NULL);
    nmatches = dg.lcount;
    amatches = &dg;
    ilistmatches(NULL, NULL);
    amatches = am;

    validlist = vl;
    smatches = sm;
    nmatches = nm;
    complastprompt = oclp;

    return !dg.lcount;
}

/* Expand the history references. */

/**/
int
doexpandhist(void)
{
    unsigned char *ol;
    int oll, ocs, ne = noerrs, err;

    DPUTS(useheap, "BUG: useheap in doexpandhist()");
    HEAPALLOC {
	pushheap();
	metafy_line();
	oll = ll;
	ocs = cs;
	ol = (unsigned char *)dupstring((char *)line);
	expanding = 1;
	excs = cs;
	ll = cs = 0;
	lexsave();
	/* We push ol as it will remain unchanged */
	inpush((char *) ol, 0, NULL);
	strinbeg(1);
	noaliases = 1;
	noerrs = 1;
	exlast = inbufct;
	do {
	    ctxtlex();
	} while (tok != ENDINPUT && tok != LEXERR);
	while (!lexstop)
	    hgetc();
	/* We have to save errflags because it's reset in lexrestore. Since  *
	 * noerrs was set to 1 errflag is true if there was a habort() which *
	 * means that the expanded string is unusable.                       */
	err = errflag;
	noerrs = ne;
	noaliases = 0;
	strinend();
	inpop();
	zleparse = 0;
	lexrestore();
	expanding = 0;

	if (!err) {
	    cs = excs;
	    if (strcmp((char *)line, (char *)ol)) {
		unmetafy_line();
		/* For vi mode -- reset the beginning-of-insertion pointer   *
		 * to the beginning of the line.  This seems a little silly, *
		 * if we are, for example, expanding "exec !!".              */
		if (viinsbegin > findbol())
		    viinsbegin = findbol();
		popheap();
		LASTALLOC_RETURN 1;
	    }
	}

	strcpy((char *)line, (char *)ol);
	ll = oll;
	cs = ocs;
	unmetafy_line();

	popheap();
    } LASTALLOC;
    return 0;
}

/**/
int
magicspace(char **args)
{
    int ret;
    c = ' ';
    if (!(ret = selfinsert(args)))
	doexpandhist();
    return ret;
}

/**/
int
expandhistory(char **args)
{
    if (!doexpandhist())
	return 1;
    return 0;
}

static int cmdwb, cmdwe;

/**/
static char *
getcurcmd(void)
{
    int curlincmd;
    char *s = NULL;

    DPUTS(useheap, "BUG: useheap in getcurcmd()");
    HEAPALLOC {
	zleparse = 2;
	lexsave();
	metafy_line();
	inpush(dupstrspace((char *) line), 0, NULL);
	unmetafy_line();
	strinbeg(1);
	pushheap();
	do {
	    curlincmd = incmdpos;
	    ctxtlex();
	    if (tok == ENDINPUT || tok == LEXERR)
		break;
	    if (tok == STRING && curlincmd) {
		zsfree(s);
		s = ztrdup(tokstr);
		cmdwb = ll - wordbeg;
		cmdwe = ll + 1 - inbufct;
	    }
	}
	while (tok != ENDINPUT && tok != LEXERR && zleparse);
	popheap();
	strinend();
	inpop();
	errflag = zleparse = 0;
	lexrestore();
    } LASTALLOC;
    return s;
}

/**/
int
processcmd(char **args)
{
    char *s;
    int m = zmult;

    s = getcurcmd();
    if (!s)
	return 1;
    zmult = 1;
    pushline(zlenoargs);
    zmult = m;
    inststr(bindk->nam);
    inststr(" ");
    untokenize(s);
    HEAPALLOC {
	inststr(quotename(s, NULL));
    } LASTALLOC;
    zsfree(s);
    done = 1;
    return 0;
}

/**/
int
expandcmdpath(char **args)
{
    int oldcs = cs, na = noaliases;
    char *s, *str;

    noaliases = 1;
    s = getcurcmd();
    noaliases = na;
    if (!s || cmdwb < 0 || cmdwe < cmdwb)
	return 1;
    str = findcmd(s, 1);
    zsfree(s);
    if (!str)
	return 1;
    cs = cmdwb;
    foredel(cmdwe - cmdwb);
    spaceinline(strlen(str));
    strncpy((char *)line + cs, str, strlen(str));
    cs = oldcs;
    if (cs >= cmdwe - 1)
	cs += cmdwe - cmdwb + strlen(str);
    if (cs > ll)
	cs = ll;
    return 0;
}

/* Extra function added by AR Iano-Fletcher. */
/* This is a expand/complete in the vein of wash. */

/**/
int
expandorcompleteprefix(char **args)
{
    int ret;

    comppref = 1;
    ret = expandorcomplete(args);
    comppref = 0;
    return ret;
}
