/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <config.h>

#include "e-book-query.h"

#include <stdarg.h>
#include <string.h>

typedef enum {
	E_BOOK_QUERY_TYPE_AND,
	E_BOOK_QUERY_TYPE_OR,
	E_BOOK_QUERY_TYPE_NOT,
	E_BOOK_QUERY_TYPE_FIELD_EXISTS,
	E_BOOK_QUERY_TYPE_FIELD_TEST,
} EBookQueryType;

struct EBookQuery {
	EBookQueryType type;
	int ref_count;

	union {
		struct {
			guint          nqs;
			EBookQuery   **qs;
		} andor;

		struct {
			EBookQuery    *q;
		} not;

		struct {
			EBookQueryTest test;
			EContactField  field;
			char          *value;
		} field_test;

		struct {
			EContactField  field;
		} exist;
	} query;
};

static EBookQuery *
conjoin (EBookQueryType type, int nqs, EBookQuery **qs, gboolean unref)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);
	int i;

	ret->type = type;
	ret->query.andor.nqs = nqs;
	ret->query.andor.qs = g_new (EBookQuery *, nqs);
	for (i = 0; i < nqs; i++) {
		ret->query.andor.qs[i] = qs[i];
		if (!unref)
			e_book_query_ref (qs[i]);
	}

	return ret;
}

EBookQuery *
e_book_query_and (int nqs, EBookQuery **qs, gboolean unref)
{
	return conjoin (E_BOOK_QUERY_TYPE_AND, nqs, qs, unref);
}

EBookQuery *
e_book_query_or (int nqs, EBookQuery **qs, gboolean unref)
{
	return conjoin (E_BOOK_QUERY_TYPE_OR, nqs, qs, unref);
}

static EBookQuery *
conjoinv (EBookQueryType type, EBookQuery *q, va_list ap)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);
	GPtrArray *qs;

	qs = g_ptr_array_new ();
	while (q) {
		g_ptr_array_add (qs, q);
		q = va_arg (ap, EBookQuery *);
	}
	va_end (ap);

	ret->type = type;
	ret->query.andor.nqs = qs->len;
	ret->query.andor.qs = (EBookQuery **)qs->pdata;
	g_ptr_array_free (qs, FALSE);

	return ret;
}

EBookQuery *
e_book_query_andv (EBookQuery *q, ...)
{
	va_list ap;

	va_start (ap, q);
	return conjoinv (E_BOOK_QUERY_TYPE_AND, q, ap);
}

EBookQuery *
e_book_query_orv (EBookQuery *q, ...)
{
	va_list ap;

	va_start (ap, q);
	return conjoinv (E_BOOK_QUERY_TYPE_OR, q, ap);
}

EBookQuery *
e_book_query_not (EBookQuery *q, gboolean unref)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);

	ret->type = E_BOOK_QUERY_TYPE_NOT;
	ret->query.not.q = q;
	if (!unref)
		e_book_query_ref (q);

	return ret;
}

EBookQuery *
e_book_query_field_test (EContactField field,
			 EBookQueryTest test,
			 const char *value)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);

	ret->type = E_BOOK_QUERY_TYPE_FIELD_TEST;
	ret->query.field_test.field = field;
	ret->query.field_test.test = test;
	ret->query.field_test.value = g_strdup (value);

	return ret;
}

EBookQuery *
e_book_query_field_exists (EContactField field)
{
	EBookQuery *ret = g_new0 (EBookQuery, 1);

	ret->type = E_BOOK_QUERY_TYPE_FIELD_EXISTS;
	ret->query.exist.field = field;

	return ret;
}

void
e_book_query_unref (EBookQuery *q)
{
	int i;

	if (q->ref_count--)
		return;

	switch (q->type) {
	case E_BOOK_QUERY_TYPE_AND:
	case E_BOOK_QUERY_TYPE_OR:
		for (i = 0; i < q->query.andor.nqs; i++)
			e_book_query_unref (q->query.andor.qs[i]);
		g_free (q->query.andor.qs);
		break;

	case E_BOOK_QUERY_TYPE_NOT:
		e_book_query_unref (q->query.not.q);
		break;

	case E_BOOK_QUERY_TYPE_FIELD_TEST:
		g_free (q->query.field_test.value);
		break;

	default:
		break;
	}

	g_free (q);
}

void
e_book_query_ref (EBookQuery *q)
{
	q->ref_count++;
}


EBookQuery*
e_book_query_from_string  (const char *sexp)
{
}

char*
e_book_query_to_string    (EBookQuery *q)
{
	GString *str = g_string_new ("(");
	int i;
	char *s;

	switch (q->type) {
	case E_BOOK_QUERY_TYPE_AND:
		g_string_append (str, "and ");
		for (i = 0; i < q->query.andor.nqs; i ++) {
			s = e_book_query_to_string (q->query.andor.qs[i]);
			g_string_append (str, s);
			g_free (s);
			g_string_append_c (str, ' ');
		}
		break;
	case E_BOOK_QUERY_TYPE_OR:
		g_string_append (str, "or ");
		for (i = 0; i < q->query.andor.nqs; i ++) {
			s = e_book_query_to_string (q->query.andor.qs[i]);
			g_string_append (str, s);
			g_free (s);
			g_string_append_c (str, ' ');
		}
		break;
	case E_BOOK_QUERY_TYPE_NOT:
		s = e_book_query_to_string (q->query.not.q);
		g_string_append_printf (str, "not %s", s);
		g_free (s);
		break;
	case E_BOOK_QUERY_TYPE_FIELD_EXISTS:
		g_string_append_printf (str, "exists \"%s\"", e_contact_field_name (q->query.exist.field));
		break;
	case E_BOOK_QUERY_TYPE_FIELD_TEST:
		switch (q->query.field_test.test) {
		case E_BOOK_QUERY_IS: s = "is"; break;
		case E_BOOK_QUERY_CONTAINS: s = "contains"; break;
		case E_BOOK_QUERY_BEGINS_WITH: s = "beginswith"; break;
		case E_BOOK_QUERY_ENDS_WITH: s = "endswith"; break;
		}

		/* XXX need to escape q->query.field_test.value */
		g_string_append_printf (str, "%s \"%s\" \"%s\"",
					s,
					e_contact_field_name (q->query.field_test.field),
					q->query.field_test.value);
		break;
	}

	g_string_append (str, ")");

	return g_string_free (str, FALSE);
}
