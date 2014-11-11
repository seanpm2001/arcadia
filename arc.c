#include "arc.h"

char *error_string[] = { "", "Syntax error", "Symbol not bound", "Wrong number of arguments", "Wrong type", "File error" };
int stack_capacity = 0;
int stack_size = 0;
atom *stack = NULL;
struct pair *pair_head = NULL;
struct str *str_head = NULL;
int alloc_count = 0;
atom sym_table = { T_NIL };
const atom nil = { T_NIL };
atom code_expr = { T_NIL };
atom env; /* the global environment */
atom sym_t, sym_quote, sym_assign, sym_fn, sym_if, sym_mac, sym_apply, sym_while, sym_cons, sym_sym, sym_fn, sym_string, sym_num;

void stack_add(atom a) {
	if (!(a.type == T_CONS
		|| a.type == T_CLOSURE
		|| a.type == T_MACRO
		|| a.type == T_STRING))
		return;
	stack_size++;
	if (stack_size > stack_capacity) {
		stack_capacity = stack_size * 2;
		stack = realloc(stack, stack_capacity * sizeof(atom));
	}
	stack[stack_size - 1] = a;
}

void consider_gc() {
	if (alloc_count > 10000) {
		gc();
		alloc_count = 0;
	}
}

atom cons(atom car_val, atom cdr_val)
{
	struct pair *a;
	atom p;

	alloc_count++;
	consider_gc();

	a = malloc(sizeof(struct pair));
	if (a == NULL) {
		puts("Not enough memory.");
		exit(1);
	}
	a->mark = 0;
	a->next = pair_head;
	pair_head = a;

	p.type = T_CONS;
	p.value.pair = a;

	car(p) = car_val;
	cdr(p) = cdr_val;

	stack_add(p);

	return p;
}

void gc_mark(atom root)
{
	struct pair *a;
	struct str *as;

	switch (root.type) {
	case T_CONS:
	case T_CLOSURE:
	case T_MACRO:
		a = root.value.pair;
		if (a->mark) return;
		a->mark = 1;
		gc_mark(car(root));
		gc_mark(cdr(root));
		break;
	case T_STRING:
		as = root.value.str;
		if (as->mark) return;
		as->mark = 1;
		break;
	default:
		return;
	}
}

void gc()
{
	struct pair *a, **p;
	struct str *as, **ps;

	gc_mark(sym_table);
	gc_mark(code_expr);

	/* mark atoms in the stack */
	int i;
	for (i = 0; i < stack_size; i++) {
		gc_mark(stack[i]);
	}

	/* Free unmarked allocations */
	p = &pair_head;
	while (*p != NULL) {
		a = *p;
		if (!a->mark) {
			*p = a->next;
			free(a);
		}
		else {
			p = &a->next;
			a->mark = 0; /* clear mark */
		}
	}

	/* Free unmarked string allocations */
	ps = &str_head;
	while (*ps != NULL) {
		as = *ps;
		if (!as->mark) {
			*ps = as->next;
			free(as->value);
			free(as);
		}
		else {
			ps = &as->next;
			as->mark = 0; /* clear mark */
		}
	}
}


atom make_number(double x)
{
	atom a;
	a.type = T_NUM;
	a.value.number = x;
	return a;
}

atom make_sym(const char *s)
{
	atom a, p;

	p = sym_table;
	while (!no(p)) {
		a = car(p);
		if (strcmp(a.value.symbol, s) == 0)
			return a;
		p = cdr(p);
	}

	a.type = T_SYMBOL;
	a.value.symbol = (char*)strdup(s);
	sym_table = cons(a, sym_table);
	return a;
}

atom make_builtin(builtin fn)
{
	atom a;
	a.type = T_BUILTIN;
	a.value.builtin = fn;
	return a;
}

error make_closure(atom env, atom args, atom body, atom *result)
{
	atom p;

	if (!listp(body))
		return ERROR_SYNTAX;

	/* Check argument names are all symbols */
	p = args;
	while (!no(p)) {
		if (p.type == T_SYMBOL)
			break;
		else if (p.type != T_CONS
			|| car(p).type != T_SYMBOL)
			return ERROR_TYPE;
		p = cdr(p);
	}

	*result = cons(env, cons(args, body));
	result->type = T_CLOSURE;

	return ERROR_OK;
}

atom make_string(char *x)
{
	atom a;
	struct str *s;
	alloc_count++;
	consider_gc();
	s = a.value.str = malloc(sizeof(struct str));
	s->value = x;
	s->mark = 0;
	s->next = str_head;
	str_head = s;

	a.type = T_STRING;
	stack_add(a);
	return a;
}

void print_expr(atom atom)
{
	switch (atom.type) {
	case T_NIL:
		printf("nil");
		break;
	case T_CONS:
		putchar('(');
		print_expr(car(atom));
		atom = cdr(atom);
		while (!no(atom)) {
			if (atom.type == T_CONS) {
				putchar(' ');
				print_expr(car(atom));
				atom = cdr(atom);
			}
			else {
				printf(" . ");
				print_expr(atom);
				break;
			}
		}
		putchar(')');
		break;
	case T_SYMBOL:
		printf("%s", atom.value.symbol);
		break;
	case T_NUM:
		printf("%.16g", atom.value.number);
		break;
	case T_BUILTIN:
		printf("#<builtin:%p>", atom.value.builtin);
		break;
	case T_CLOSURE:
		printf("(closure ");
		print_expr(cdr(atom));
		putchar(')');
		break;
	case T_STRING:
		printf("\"%s\"", atom.value.str->value);
		break;
	case T_MACRO:
		printf("(macro ");
		print_expr(cdr(atom));
		putchar(')');
		break;
	default:
		printf("(unknown type)");
		break;
	}
}

void pr(atom atom)
{
	switch (atom.type) {
	case T_NIL:
		printf("nil");
		break;
	case T_CONS:
		putchar('(');
		print_expr(car(atom));
		atom = cdr(atom);
		while (!no(atom)) {
			if (atom.type == T_CONS) {
				putchar(' ');
				print_expr(car(atom));
				atom = cdr(atom);
			}
			else {
				printf(" . ");
				print_expr(atom);
				break;
			}
		}
		putchar(')');
		break;
	case T_SYMBOL:
		printf("%s", atom.value.symbol);
		break;
	case T_NUM:
		printf("%.16g", atom.value.number);
		break;
	case T_BUILTIN:
		printf("#<builtin:%p>", atom.value.builtin);
		break;
	case T_CLOSURE:
		printf("(closure ");
		print_expr(cdr(atom));
		putchar(')');
		break;
	case T_STRING:
		printf("%s", atom.value.str->value);
		break;
	case T_MACRO:
		printf("(macro ");
		print_expr(cdr(atom));
		putchar(')');
		break;
	default:
		printf("(unknown type)");
		break;
	}
}

error lex(const char *str, const char **start, const char **end)
{
	const char *ws = " \t\r\n";
	const char *delim = "() \t\r\n;";
	const char *prefix = "()'`";
start:
	str += strspn(str, ws);

	if (str[0] == '\0') {
		*start = *end = NULL;
		return ERROR_SYNTAX;
	}

	*start = str;

	if (strchr(prefix, str[0]) != NULL)
		*end = str + 1;
	else if (str[0] == ',')
		*end = str + (str[1] == '@' ? 2 : 1);
	else if (str[0] == '"') {
		str++;
		while (*str != '"' && *str != 0) {
			str++;
		}
		*end = str + 1;
	}
	else if (str[0] == ';') { /* end-of-line comment */
		str += strcspn(str, "\n");
		goto start;
	}
	else
		*end = str + strcspn(str, delim);

	return ERROR_OK;
}

error parse_simple(const char *start, const char *end, atom *result)
{
	char *buf, *p;

	/* Is it an integer? */
	double val = strtod(start, &p);
	if (p == end) {
		result->type = T_NUM;
		result->value.number = val;
		return ERROR_OK;
	}
	else if (start[0] == '"') {
		result->type = T_STRING;
		char *s = (char*)malloc(end - start - 1);
		memcpy(s, start + 1, end - start);
		s[end - start - 2] = 0;
		*result = make_string(s);
		return ERROR_OK;
	}

	/* NIL or symbol */
	buf = malloc(end - start + 1);
	memcpy(buf, start, end - start);
	buf[end - start] = 0;

	if (strcmp(buf, "nil") == 0)
		*result = nil;
	else
		*result = make_sym(buf);

	free(buf);

	return ERROR_OK;
}

error read_list(const char *start, const char **end, atom *result)
{
	atom p;

	*end = start;
	p = *result = nil;

	for (;;) {
		const char *token;
		atom item;
		error err;

		err = lex(*end, &token, end);
		if (err)
			return err;

		if (token[0] == ')')
			return ERROR_OK;

		if (token[0] == '.' && *end - token == 1) {
			/* Improper list */
			if (no(p))
				return ERROR_SYNTAX;

			err = read_expr(*end, end, &item);
			if (err)
				return err;

			cdr(p) = item;

			/* Read the closing ')' */
			err = lex(*end, &token, end);
			if (!err && token[0] != ')')
				err = ERROR_SYNTAX;

			return err;
		}

		err = read_expr(token, end, &item);
		if (err)
			return err;

		if (no(p)) {
			/* First item */
			*result = cons(item, nil);
			p = *result;
		}
		else {
			cdr(p) = cons(item, nil);
			p = cdr(p);
		}
	}
}

error read_expr(const char *input, const char **end, atom *result)
{
	const char *token;
	error err;

	err = lex(input, &token, end);
	if (err)
		return err;

	if (token[0] == '(')
		return read_list(*end, end, result);
	else if (token[0] == ')')
		return ERROR_SYNTAX;
	else if (token[0] == '\'') {
		*result = cons(make_sym("quote"), cons(nil, nil));
		return read_expr(*end, end, &car(cdr(*result)));
	}
	else if (token[0] == '`') {
		*result = cons(make_sym("quasiquote"), cons(nil, nil));
		return read_expr(*end, end, &car(cdr(*result)));
	}
	else if (token[0] == ',') {
		*result = cons(make_sym(
			token[1] == '@' ? "unquote-splicing" : "unquote"),
			cons(nil, nil));
		return read_expr(*end, end, &car(cdr(*result)));
	}
	else
		return parse_simple(token, *end, result);
}

#ifndef READLINE
char *readline(char *prompt) {
	size_t size = 80;
	/* The size is extended by the input with the value of the provisional */
	char *str;
	int ch;
	size_t len = 0;
	printf(prompt);
	str = malloc(sizeof(char)* size); /* size is start size */
	if (!str) return NULL;
	while (EOF != (ch = fgetc(stdin)) && ch != '\n') {
		str[len++] = ch;
		if (len == size){
			str = realloc(str, sizeof(char)*(size *= 2));
			if (!str) return NULL;
		}
	}
	if (ch == EOF && len == 0) return NULL;
	str[len++] = '\0';

	return realloc(str, sizeof(char)*len);
}
#endif /* READLINE */

atom env_create(atom parent)
{
	return cons(parent, nil);
}

error env_get(atom env, atom symbol, atom *result)
{
	atom parent = car(env);
	atom bs = cdr(env);

	while (!no(bs)) {
		atom b = car(bs);
		if (car(b).value.symbol == symbol.value.symbol) {
			*result = cdr(b);
			return ERROR_OK;
		}
		bs = cdr(bs);
	}

	if (no(parent)) {
		/*printf("%s: ", symbol.value.symbol);*/
		return ERROR_UNBOUND;
	}

	return env_get(parent, symbol, result);
}

error env_assign(atom env, atom symbol, atom value)
{
	atom bs = cdr(env);
	atom b = nil;

	while (!no(bs)) {
		b = car(bs);
		if (car(b).value.symbol == symbol.value.symbol) {
			cdr(b) = value;
			return ERROR_OK;
		}
		bs = cdr(bs);
	}

	b = cons(symbol, value);
	cdr(env) = cons(b, cdr(env));

	return ERROR_OK;
}

error env_assign_eq(atom env, atom symbol, atom value) {
	atom env_origin = env;
	while (!no(env)) {
		atom bs = cdr(env);

		while (!no(bs)) {
			atom b = car(bs);
			if (car(b).value.symbol == symbol.value.symbol) {
				cdr(b) = value;
				return ERROR_OK;
			}
			bs = cdr(bs);
		}
		env = car(env);
	}

	return env_assign(env_origin, symbol, value);
}

int listp(atom expr)
{
	while (!no(expr)) {
		if (expr.type != T_CONS)
			return 0;
		expr = cdr(expr);
	}
	return 1;
}

long len(atom xs) {
	atom a = xs;
	long ret = 0;
	if (!listp(xs)) return 0;
	while (!no(a)) {
		ret++;
		a = cdr(a);
	}
	return ret;
}

atom copy_list(atom list)
{
	atom a, p;

	if (no(list))
		return nil;

	a = cons(car(list), nil);
	p = a;
	list = cdr(list);

	while (!no(list)) {
		cdr(p) = cons(car(list), nil);
		p = cdr(p);
		list = cdr(list);
		if (list.type != T_CONS) { /* improper list */
			p = list;
			break;
		}
	}

	return a;
}

error apply(atom fn, atom args, atom *result)
{
	atom env, arg_names, body;

	if (fn.type == T_BUILTIN)
		return (*fn.value.builtin)(args, result);
	else if (fn.type == T_CLOSURE) {
		env = env_create(car(fn));
		arg_names = car(cdr(fn));
		body = cdr(cdr(fn));

		/* Bind the arguments */
		while (!no(arg_names)) {
			if (arg_names.type == T_SYMBOL) {
				env_assign(env, arg_names, args);
				args = nil;
				break;
			}

			if (no(args))
				return ERROR_ARGS;
			env_assign(env, car(arg_names), car(args));
			arg_names = cdr(arg_names);
			args = cdr(args);
		}
		if (!no(args))
			return ERROR_ARGS;

		/* Evaluate the body */
		while (!no(body)) {
			error err = eval_expr(car(body), env, result);
			if (err)
				return err;
			body = cdr(body);
		}

		return ERROR_OK;
	}
	else if (fn.type == T_STRING) { /* implicit indexing for string */
		if (len(args) != 1) return ERROR_ARGS;
		long index = (long)(car(args)).value.number;
		*result = make_number(fn.value.str->value[index]);
		return ERROR_OK;
	}
	else if (fn.type == T_CONS && listp(fn)) { /* implicit indexing for list */
		if (len(args) != 1) return ERROR_ARGS;
		long index = (long)(car(args)).value.number;
		atom a = fn;
		long i;
		for (i = 0; i < index; i++) {
			a = cdr(a);
			if (no(a)) {
				*result = nil;
				return ERROR_OK;
			}
		}
		*result = car(a);
		return ERROR_OK;
	}
	else {
		return ERROR_TYPE;
	}
}

error builtin_car(atom args, atom *result)
{
	if (no(args) || !no(cdr(args)))
		return ERROR_ARGS;

	if (no(car(args)))
		*result = nil;
	else if (car(args).type != T_CONS)
		return ERROR_TYPE;
	else
		*result = car(car(args));

	return ERROR_OK;
}

error builtin_cdr(atom args, atom *result)
{
	if (no(args) || !no(cdr(args)))
		return ERROR_ARGS;

	if (no(car(args)))
		*result = nil;
	else if (car(args).type != T_CONS)
		return ERROR_TYPE;
	else
		*result = cdr(car(args));

	return ERROR_OK;
}

error builtin_cons(atom args, atom *result)
{
	if (no(args) || no(cdr(args)) || !no(cdr(cdr(args))))
		return ERROR_ARGS;

	*result = cons(car(args), car(cdr(args)));

	return ERROR_OK;
}

error builtin_add(atom args, atom *result)
{
	atom acc = make_number(0);
	atom a, a2;
	if (!listp(args)) return ERROR_ARGS;

	a = args;
	while (!no(a)) {
		a2 = car(a);
		if (a2.type != T_NUM) return ERROR_TYPE;
		acc.value.number += a2.value.number;
		a = cdr(a);
	}
	*result = acc;
	return ERROR_OK;
}

error builtin_subtract(atom args, atom *result)
{
	atom acc;
	atom a, a2;
	if (!listp(args)) return ERROR_ARGS;
	if (no(args)) { /* 0 argument */
		*result = make_number(0);
		return ERROR_OK;
	}
	if (no(cdr(args))) { /* 1 argument */
		if (car(args).type != T_NUM) return ERROR_TYPE;
		*result = make_number(-car(args).value.number);
		return ERROR_OK;
	}
	a2 = car(args);
	if (a2.type != T_NUM) return ERROR_TYPE;
	acc = make_number(a2.value.number);
	a = cdr(args);
	while (!no(a)) {
		a2 = car(a);
		if (a2.type != T_NUM) return ERROR_TYPE;
		acc.value.number -= a2.value.number;
		a = cdr(a);
	}
	*result = acc;
	return ERROR_OK;
}

error builtin_multiply(atom args, atom *result)
{
	atom acc = make_number(1);
	atom a, a2;
	if (!listp(args)) return ERROR_ARGS;

	a = args;
	while (!no(a)) {
		a2 = car(a);
		if (a2.type != T_NUM) return ERROR_TYPE;
		acc.value.number *= a2.value.number;
		a = cdr(a);
	}
	*result = acc;
	return ERROR_OK;
}

error builtin_divide(atom args, atom *result)
{
	atom acc;
	atom a, a2;
	if (!listp(args)) return ERROR_ARGS;
	if (no(args)) { /* 0 argument */
		*result = make_number(1.0);
		return ERROR_OK;
	}
	if (no(cdr(args))) { /* 1 argument */
		if (car(args).type != T_NUM) return ERROR_TYPE;
		*result = make_number(1.0 / car(args).value.number);
		return ERROR_OK;
	}
	a2 = car(args);
	if (a2.type != T_NUM) return ERROR_TYPE;
	acc = make_number(a2.value.number);
	a = cdr(args);
	while (!no(a)) {
		a2 = car(a);
		if (a2.type != T_NUM) return ERROR_TYPE;
		acc.value.number /= a2.value.number;
		a = cdr(a);
	}
	*result = acc;
	return ERROR_OK;
}

error builtin_less(atom args, atom *result)
{
	atom a, b;
	if (no(args) || no(cdr(args)) || !no(cdr(cdr(args)))) return ERROR_ARGS;
	a = car(args);
	b = car(cdr(args));
	if (a.type != T_NUM || b.type != T_NUM) return ERROR_TYPE;
	*result = (a.value.number < b.value.number) ? sym_t : nil;
	return ERROR_OK;
}

error builtin_apply(atom args, atom *result)
{
	atom fn;

	if (no(args) || no(cdr(args)) || !no(cdr(cdr(args))))
		return ERROR_ARGS;

	fn = car(args);
	args = car(cdr(args));

	if (!listp(args))
		return ERROR_SYNTAX;

	return apply(fn, args, result);
}

error builtin_is(atom args, atom *result)
{
	atom a, b;
	int eq = 0;

	if (no(args) || no(cdr(args)) || !no(cdr(cdr(args))))
		return ERROR_ARGS;

	a = car(args);
	b = car(cdr(args));

	if (a.type == b.type) {
		switch (a.type) {
		case T_NIL:
			eq = 1;
			break;
		case T_CONS:
		case T_CLOSURE:
		case T_MACRO:
			eq = (a.value.pair == b.value.pair);
			break;
		case T_SYMBOL:
			eq = (a.value.symbol == b.value.symbol);
			break;
		case T_NUM:
			eq = (a.value.number == b.value.number);
			break;
		case T_BUILTIN:
			eq = (a.value.builtin == b.value.builtin);
			break;
		case T_STRING:
			eq = strcmp(a.value.str->value, b.value.str->value) == 0;
			break;
		default:
			/* impossible */
			break;
		}
	}

	*result = eq ? sym_t : nil;
	return ERROR_OK;
}

error builtin_scar(atom args, atom *result) {
	atom place = car(args), value;
	if (place.type != T_CONS) return ERROR_TYPE;
	value = car(cdr(args));
	place.value.pair->car = value;
	*result = value;
	return ERROR_OK;
}

error builtin_scdr(atom args, atom *result) {
	atom place = car(args), value;
	if (place.type != T_CONS) return ERROR_TYPE;
	value = car(cdr(args));
	place.value.pair->cdr = value;
	*result = value;
	return ERROR_OK;
}

error builtin_mod(atom args, atom *result) {
	atom dividend = car(args);
	atom divisor = car(cdr(args));
	double r = fmod(dividend.value.number, divisor.value.number);
	if (dividend.value.number * divisor.value.number < 0 && r != 0) r += divisor.value.number;
	*result = make_number(r);
	return ERROR_OK;
}

error builtin_type(atom args, atom *result) {
	atom x = car(args);
	switch (x.type) {
	case T_CONS: *result = sym_cons; break;
	case T_SYMBOL:
	case T_NIL: *result = sym_sym; break;
	case T_BUILTIN:
	case T_CLOSURE: *result = sym_fn; break;
	case T_STRING: *result = sym_string; break;
	case T_NUM: *result = sym_num; break;
	case T_MACRO: *result = sym_mac; break;
	default: *result = nil; break; /* impossible */
	}
	return ERROR_OK;
}

error builtin_string_sref(atom args, atom *result) {
	atom index, obj, value;
	if (len(args) != 3) return ERROR_ARGS;
	index = car(cdr(cdr(args)));
	obj = car(args);
	if (obj.type != T_STRING) return ERROR_TYPE;
	value = car(cdr(args));
	obj.value.str->value[(long)index.value.number] = (char)value.value.number;
	*result = make_number(value.value.number);
	return ERROR_OK;
}

error builtin_pr(atom args, atom *result) {
	if (no(args)) {
		*result = nil;
		return ERROR_OK;
	}
	*result = car(args);
	while (!no(args)) {
		pr(car(args));
		args = cdr(args);
	}
	return ERROR_OK;
}

error builtin_writeb(atom args, atom *result) {
	if (len(args) != 1) return ERROR_ARGS;
	putchar((int)car(args).value.number);
	*result = nil;
	return ERROR_OK;
}

error builtin_expt(atom args, atom *result) {
	atom a, b;
	if (len(args) != 2) return ERROR_ARGS;
	a = car(args);
	b = car(cdr(args));
	*result = make_number(pow(a.value.number, b.value.number));
	return ERROR_OK;
}

error builtin_log(atom args, atom *result) {
	atom a;
	if (len(args) != 1) return ERROR_ARGS;
	a = car(args);
	*result = make_number(log(a.value.number));
	return ERROR_OK;
}

error builtin_sqrt(atom args, atom *result) {
	atom a;
	if (len(args) != 1) return ERROR_ARGS;
	a = car(args);
	*result = make_number(sqrt(a.value.number));
	return ERROR_OK;
}

error builtin_readline(atom args, atom *result) {
	if (len(args) != 0) return ERROR_ARGS;
	char *str = readline("");
	if (str == NULL) *result = nil; else *result = make_string(str);
	return ERROR_OK;
}

error builtin_quit(atom args, atom *result) {
	if (len(args) != 0) return ERROR_ARGS;
	exit(0);
}

double rand_double() {
	return (double)rand() / ((double)RAND_MAX + 1.0);
}

error builtin_rand(atom args, atom *result) {
	long alen = len(args);
	if (alen == 0) *result = make_number(rand_double());
	else if (alen == 1) *result = make_number(floor(rand_double() * car(args).value.number));
	else return ERROR_ARGS;
	return ERROR_OK;
}

error builtin_read(atom args, atom *result) {
	long alen = len(args);
	char *s;
	if (alen == 0) {
		s = readline("");
		const char *buf = s;
		error err = read_expr(buf, &buf, result);
		free(s);
		return err;
	}
	else if (alen == 1) {
		s = car(args).value.str->value;
		const char *buf = s;
		error err = read_expr(buf, &buf, result);
		return err;
	}
	else return ERROR_ARGS;
	return ERROR_OK;
}

error builtin_macex(atom args, atom *result) {
	long alen = len(args);
	if (alen == 1) {
		error err = macex(car(args), result);
		return err;
	}
	else return ERROR_ARGS;
	return ERROR_OK;
}

error builtin_string(atom args, atom *result) {
	char *s = str_new();
	while (!no(args)) {
		char *a = to_string(car(args));
		strcat_alloc(&s, a);
		free(a);
		args = cdr(args);
	}
	*result = make_string(s);
	return ERROR_OK;
}

error builtin_sym(atom args, atom *result) {
	long alen = len(args);
	if (alen == 1) {
		*result = make_sym(to_string(car(args)));
		return ERROR_OK;
	}
	else return ERROR_ARGS;
}

error builtin_system(atom args, atom *result) {
	long alen = len(args);
	if (alen == 1) {
		atom a = car(args);
		if (a.type != T_STRING) return ERROR_TYPE;
		*result = make_number(system(car(args).value.str->value));
		return ERROR_OK;
	}
	else return ERROR_ARGS;
}

error builtin_eval(atom args, atom *result) {
	if (len(args) == 1) return macex_eval(car(args), result);
	else return ERROR_ARGS;
}

error builtin_load(atom args, atom *result) {
	if (len(args) == 1) {
		atom a = car(args);
		if (a.type != T_STRING) return ERROR_TYPE;
		*result = nil;
		return arc_load_file(a.value.str->value);
	}
	else return ERROR_ARGS;
}

error builtin_int(atom args, atom *result) {
	if (len(args) == 1) {
		atom a = car(args);
		switch (a.type) {
		case T_STRING:
			*result = make_number(round(atof(a.value.str->value)));
			break;
		case T_SYMBOL:
			*result = make_number(round(atof(a.value.symbol)));
			break;
		case T_NUM:
			*result = make_number(round(a.value.number));
			break;
		default:
			return ERROR_TYPE;
		}
		return ERROR_OK;
	}
	else return ERROR_ARGS;
}

error builtin_trunc(atom args, atom *result) {
	if (len(args) == 1) {
		atom a = car(args);
		if (a.type != T_NUM) return ERROR_TYPE;
		*result = make_number(trunc(a.value.number));
		return ERROR_OK;
	}
	else return ERROR_ARGS;
}

error builtin_sin(atom args, atom *result) {
	if (len(args) == 1) {
		atom a = car(args);
		if (a.type != T_NUM) return ERROR_TYPE;
		*result = make_number(sin(a.value.number));
		return ERROR_OK;
	}
	else return ERROR_ARGS;
}

error builtin_cos(atom args, atom *result) {
	if (len(args) == 1) {
		atom a = car(args);
		if (a.type != T_NUM) return ERROR_TYPE;
		*result = make_number(cos(a.value.number));
		return ERROR_OK;
	}
	else return ERROR_ARGS;
}

error builtin_tan(atom args, atom *result) {
	if (len(args) == 1) {
		atom a = car(args);
		if (a.type != T_NUM) return ERROR_TYPE;
		*result = make_number(tan(a.value.number));
		return ERROR_OK;
	}
	else return ERROR_ARGS;
}

/* end builtin */

char *strcat_alloc(char **dst, char *src) {
	size_t len = strlen(*dst) + strlen(src);
	*dst = realloc(*dst, (len + 1) * sizeof(char));
	strcat(*dst, src);
	return *dst;
}

char *str_new() {
	char *s = malloc(1 * sizeof(char));
	s[0] = 0;
	return s;
}

char *to_string(atom atom) {
	char *s = str_new();
	char buf[80];
	switch (atom.type) {
	case T_NIL:
		strcat_alloc(&s, "nil");
		break;
	case T_CONS:
		strcat_alloc(&s, "(");
		strcat_alloc(&s, to_string(car(atom)));
		atom = cdr(atom);
		while (!no(atom)) {
			if (atom.type == T_CONS) {
				strcat_alloc(&s, " ");
				strcat_alloc(&s, to_string(car(atom)));
				atom = cdr(atom);
			}
			else {
				strcat_alloc(&s, " . ");
				strcat_alloc(&s, to_string(atom));
				break;
			}
		}
		strcat_alloc(&s, ")");
		break;
	case T_SYMBOL:
	case T_STRING:
		strcat_alloc(&s, atom.value.str->value);
		break;
	case T_NUM:
		sprintf(buf, "%.16g", atom.value.number);
		strcat_alloc(&s, buf);
		break;
	case T_BUILTIN:
		sprintf(buf, "#<builtin:%p>", atom.value.builtin);
		strcat_alloc(&s, buf);
		break;
	case T_CLOSURE:
		strcat_alloc(&s, "(closure ");
		strcat_alloc(&s, to_string(cdr(atom)));
		strcat_alloc(&s, ")");
		break;
	case T_MACRO:
		strcat_alloc(&s, "(macro ");
		strcat_alloc(&s, to_string(cdr(atom)));
		strcat_alloc(&s, ")");
		break;
	default:
		strcat_alloc(&s, "(unknown type)");
		break;
	}
	return s;
}

char *slurp(const char *path)
{
	FILE *file;
	char *buf;
	long len;

	file = fopen(path, "rb");
	if (!file) {
		/* printf("Reading %s failed.\n", path); */
		return NULL;
	}
	fseek(file, 0, SEEK_END);
	len = ftell(file);
	if (len < 0) return NULL;
	fseek(file, 0, SEEK_SET);

	buf = (char *)malloc(len + 1);
	if (!buf)
		return NULL;

	fread(buf, 1, len, file);
	buf[len] = 0;
	fclose(file);

	return buf;
}

/* compile-time macro */
error macex(atom expr, atom *result) {
	error err = ERROR_OK;
	int ss = stack_size; /* save stack point */

	stack_add(expr);
	stack_add(env);

	if (expr.type == T_SYMBOL) {
		*result = expr;
		stack_restore(ss);
		stack_add(*result);
		return ERROR_OK;
	}
	else if (expr.type != T_CONS) {
		*result = expr;
		stack_restore(ss);
		stack_add(*result);
		return ERROR_OK;
	}
	else if (!listp(expr)) {
		*result = expr;
		stack_restore(ss);
		stack_add(*result);
		return ERROR_OK;
	}
	else {
		atom op = car(expr);
		atom args = cdr(expr);

		if (op.type == T_SYMBOL) {
			/* Handle special forms */

			if (op.value.symbol == sym_quote.value.symbol) {
				if (no(args) || !no(cdr(args))) {
					stack_restore(ss);
					return ERROR_ARGS;
				}

				*result = expr;
				stack_restore(ss);
				stack_add(*result);
				return ERROR_OK;
			}
			else if (op.value.symbol == sym_mac.value.symbol) { /* (mac name (arg ...) body) */
				atom name, macro;

				if (no(args) || no(cdr(args)) || no(cdr(cdr(args)))) {
					stack_restore(ss);
					return ERROR_ARGS;
				}

				name = car(args);
				if (name.type != T_SYMBOL) {
					stack_restore(ss);
					return ERROR_TYPE;
				}

				err = make_closure(env, car(cdr(args)), cdr(cdr(args)), &macro);
				if (!err) {
					macro.type = T_MACRO;
					*result = cons(sym_quote, cons(car(args), nil));
					err = env_assign(env, name, macro);
					stack_restore(ss);
					stack_add(*result);
					return err;
				}
				else {
					stack_restore(ss);
					return err;
				}
			}
		}

		/* Is it a macro? */
		if (op.type == T_SYMBOL && !env_get(env, op, result) && result->type == T_MACRO) {
			/* Evaluate operator */
			err = eval_expr(op, env, &op);
			if (err) {
				stack_restore(ss);
				stack_add(*result);
				return err;
			}

			op.type = T_CLOSURE;
			atom result2;
			err = apply(op, args, &result2);
			if (err) {
				stack_restore(ss);
				return err;
			}
			stack_add(result2);
			err = macex(result2, result); /* recursive */
			if (err) {
				stack_restore(ss);
				return err;
			}
			stack_restore(ss);
			stack_add(*result);
			return ERROR_OK;
		}
		else {
			/* preprocess elements */
			atom expr2 = copy_list(expr);
			atom p = expr2;
			while (!no(p)) {
				err = macex(car(p), &car(p));
				if (err) {
					stack_restore(ss);
					stack_add(*result);
					return err;
				}
				p = cdr(p);
			}
			*result = expr2;
			stack_restore(ss);
			stack_add(*result);
			return ERROR_OK;
		}
	}
}

error macex_eval(atom expr, atom *result) {
	atom expr2;
	error err = macex(expr, &expr2);
	if (err) return err;
	/*printf("expanded: ");
	print_expr(expr2);
	puts("");*/
	return eval_expr(expr2, env, result);
}

error arc_load_file(const char *path)
{
	char *text;

	/* printf("Reading %s...\n", path); */
	text = slurp(path);
	if (text) {
		const char *p = text;
		atom expr;
		while (read_expr(p, &p, &expr) == ERROR_OK) {
			atom result;
			error err = macex_eval(expr, &result);
			if (err) {
				print_error(err);
				printf("error in expression:\n\t");
				print_expr(expr);
				putchar('\n');
			}
			/*else {
			print_expr(result);
			putchar(' ');
			}*/
		}
		/*puts("");*/
		free(text);
		return ERROR_OK;
	}
	else {
		return ERROR_FILE;
	}
}

void stack_restore(int saved_size) {
	stack_size = saved_size;
	/* if there is waste of memory, realloc */
	if (stack_size < stack_capacity / 4) {
		stack_capacity /= 2;
		stack = realloc(stack, stack_capacity * sizeof(atom));
	}
}

error eval_expr(atom expr, atom env, atom *result)
{
	error err = ERROR_OK;
	int ss = stack_size; /* save stack point */

	stack_add(expr);
	stack_add(env);

	if (expr.type == T_SYMBOL) {
		err = env_get(env, expr, result);
		stack_restore(ss);
		stack_add(*result);
		return err;
	}
	else if (expr.type != T_CONS) {
		*result = expr;
		stack_restore(ss);
		stack_add(*result);
		return ERROR_OK;
	}
	else if (!listp(expr)) {
		stack_restore(ss);
		return ERROR_SYNTAX;
	}
	else {
		atom op = car(expr);
		atom args = cdr(expr);

		if (op.type == T_SYMBOL) {
			/* Handle special forms */

			if (op.value.symbol == sym_quote.value.symbol) {
				if (no(args) || !no(cdr(args))) {
					stack_restore(ss);
					return ERROR_ARGS;
				}

				*result = car(args);
				stack_restore(ss);
				stack_add(*result);
				return ERROR_OK;
			}
			else if (op.value.symbol == sym_assign.value.symbol) {
				atom sym;
				if (no(args) || no(cdr(args))) {
					stack_restore(ss);
					return ERROR_ARGS;
				}

				sym = car(args);
				if (sym.type == T_SYMBOL) {
					atom val;
					err = eval_expr(car(cdr(args)), env, &val);
					if (err) {
						stack_restore(ss);
						stack_add(*result);
						return err;
					}

					*result = val;
					err = env_assign_eq(env, sym, val);
					stack_restore(ss);
					stack_add(*result);
					return err;
				}
				else {
					stack_restore(ss);
					return ERROR_TYPE;
				}
			}
			else if (op.value.symbol == sym_fn.value.symbol) {
				if (no(args) || no(cdr(args))) {
					stack_restore(ss);
					return ERROR_ARGS;
				}
				err = make_closure(env, car(args), cdr(args), result);
				stack_restore(ss);
				stack_add(*result);
				return err;
			}
			else if (op.value.symbol == sym_if.value.symbol) {
				atom cond;
				while (!no(args)) {
					err = eval_expr(car(args), env, &cond);
					if (err) {
						stack_restore(ss);
						stack_add(*result);
						return err;
					}
					if (no(cdr(args))) {
						*result = cond;
						stack_restore(ss);
						stack_add(*result);
						return ERROR_OK;
					}
					if (!no(cond)) {
						err = eval_expr(car(cdr(args)), env, result);
						stack_restore(ss);
						stack_add(*result);
						return err;
					}
					args = cdr(cdr(args));
				}
				*result = nil;
				stack_restore(ss);
				stack_add(*result);
				return ERROR_OK;
			}
			else if (op.value.symbol == sym_mac.value.symbol) { /* (mac name (arg ...) body) */
				atom name, macro;

				if (no(args) || no(cdr(args)) || no(cdr(cdr(args)))) {
					stack_restore(ss);
					return ERROR_ARGS;
				}

				name = car(args);
				if (name.type != T_SYMBOL) {
					stack_restore(ss);
					return ERROR_TYPE;
				}

				err = make_closure(env, car(cdr(args)), cdr(cdr(args)), &macro);
				if (!err) {
					macro.type = T_MACRO;
					*result = name;
					err = env_assign(env, name, macro);
					stack_restore(ss);
					stack_add(*result);
					return err;
				}
				else {
					stack_restore(ss);
					stack_add(*result);
					return err;
				}
			}
			else if (op.value.symbol == sym_while.value.symbol) {
				atom pred;
				if (no(args)) {
					stack_restore(ss);
					return ERROR_ARGS;
				}
				pred = car(args);
				int ss2 = stack_size;
				while (err = eval_expr(pred, env, result), !no(*result)) {
					if (err) {
						stack_restore(ss);
						return err;
					}
					atom e = cdr(args);
					while (!no(e)) {
						err = eval_expr(car(e), env, result);
						if (err) {
							stack_restore(ss);
							return err;
						}
						e = cdr(e);
					}
					stack_restore(ss2);
				}
				stack_restore(ss);
				stack_add(*result);
				return ERROR_OK;
			}
		}

		/* Evaluate operator */
		err = eval_expr(op, env, &op);
		if (err) {
			stack_restore(ss);
			stack_add(*result);
			return err;
		}

		/* Is it a macro? */
		if (op.type == T_MACRO) {
			atom expansion;
			op.type = T_CLOSURE;
			err = apply(op, args, &expansion);
			stack_add(expansion);
			if (err) {
				stack_restore(ss);
				stack_add(*result);
				return err;
			}
			err = eval_expr(expansion, env, result);
			stack_restore(ss);
			stack_add(*result);
			return err;
		}

		/* Evaulate arguments */
		args = copy_list(args);
		atom p = args;
		while (!no(p)) {
			err = eval_expr(car(p), env, &car(p));
			if (err) {
				stack_restore(ss);
				stack_add(*result);
				return err;
			}

			p = cdr(p);
		}
		err = apply(op, args, result);
		stack_restore(ss);
		stack_add(*result);
		return err;
	}
}

void arc_init(char *file_path) {
	srand((unsigned int)time(0));
	env = env_create(nil);

	/* Set up the initial environment */
	sym_t = make_sym("t");
	sym_quote = make_sym("quote");
	sym_assign = make_sym("assign");
	sym_fn = make_sym("fn");
	sym_if = make_sym("if");
	sym_mac = make_sym("mac");
	sym_apply = make_sym("apply");
	sym_while = make_sym("while");
	sym_cons = make_sym("cons");
	sym_sym = make_sym("sym");
	sym_string = make_sym("string");
	sym_num = make_sym("num");

	env_assign(env, make_sym("car"), make_builtin(builtin_car));
	env_assign(env, make_sym("cdr"), make_builtin(builtin_cdr));
	env_assign(env, make_sym("cons"), make_builtin(builtin_cons));
	env_assign(env, make_sym("+"), make_builtin(builtin_add));
	env_assign(env, make_sym("-"), make_builtin(builtin_subtract));
	env_assign(env, make_sym("*"), make_builtin(builtin_multiply));
	env_assign(env, make_sym("/"), make_builtin(builtin_divide));
	env_assign(env, sym_t, sym_t);
	env_assign(env, make_sym("<"), make_builtin(builtin_less));
	env_assign(env, make_sym("apply"), make_builtin(builtin_apply));
	env_assign(env, make_sym("is"), make_builtin(builtin_is));
	env_assign(env, make_sym("scar"), make_builtin(builtin_scar));
	env_assign(env, make_sym("scdr"), make_builtin(builtin_scdr));
	env_assign(env, make_sym("mod"), make_builtin(builtin_mod));
	env_assign(env, make_sym("type"), make_builtin(builtin_type));
	env_assign(env, make_sym("string-sref"), make_builtin(builtin_string_sref));
	env_assign(env, make_sym("pr"), make_builtin(builtin_pr));
	env_assign(env, make_sym("writeb"), make_builtin(builtin_writeb));
	env_assign(env, make_sym("expt"), make_builtin(builtin_expt));
	env_assign(env, make_sym("log"), make_builtin(builtin_log));
	env_assign(env, make_sym("sqrt"), make_builtin(builtin_sqrt));
	env_assign(env, make_sym("readline"), make_builtin(builtin_readline));
	env_assign(env, make_sym("quit"), make_builtin(builtin_quit));
	env_assign(env, make_sym("rand"), make_builtin(builtin_rand));
	env_assign(env, make_sym("read"), make_builtin(builtin_read));
	env_assign(env, make_sym("macex"), make_builtin(builtin_macex));
	env_assign(env, make_sym("string"), make_builtin(builtin_string));
	env_assign(env, make_sym("sym"), make_builtin(builtin_sym));
	env_assign(env, make_sym("system"), make_builtin(builtin_system));
	env_assign(env, make_sym("eval"), make_builtin(builtin_eval));
	env_assign(env, make_sym("load"), make_builtin(builtin_load));
	env_assign(env, make_sym("int"), make_builtin(builtin_int));
	env_assign(env, make_sym("trunc"), make_builtin(builtin_trunc));
	env_assign(env, make_sym("sin"), make_builtin(builtin_sin));
	env_assign(env, make_sym("cos"), make_builtin(builtin_cos));
	env_assign(env, make_sym("tan"), make_builtin(builtin_tan));

	char *dir_path = get_dir_path(file_path);
	char *lib = malloc((strlen(dir_path) + 1) * sizeof(char));
	strcpy(lib, dir_path);
	strcat_alloc(&lib, "library.arc");
	if (arc_load_file(lib) != ERROR_OK) {
		strcpy(lib, dir_path);
		strcat_alloc(&lib, "../library.arc");
		arc_load_file(lib);
	}
	free(lib);
	free(dir_path);
}

void print_env() {
	/* print the environment */
	puts("Environment:");
	atom a = cdr(env);
	while (!no(a)) {
		atom env_pair = car(a);
		printf(" %s", car(env_pair).value.symbol);
		a = cdr(a);
	}
	puts("");
}

char *get_dir_path(char *file_path) {
	size_t len = strlen(file_path);
	long i = len - 1;
	for (; i >= 0; i--) {
		char c = file_path[i];
		if (c == '\\' || c == '/') {
			break;
		}
	}
	size_t len2 = i + 1;
	char *r = malloc((len2 + 1) * sizeof(char));
	memcpy(r, file_path, len2);
	r[len2] = 0;
	return r;
}

void print_error(error e) {
	puts(error_string[e]);
}