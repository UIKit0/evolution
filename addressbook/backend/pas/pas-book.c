/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * pas-book.c
 *
 * Copyright 2000, Ximian, Inc.
 */

#include <config.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-arg.h>
#include "e-util/e-list.h"
#include "ebook/e-card.h"
#include "pas-book-view.h"
#include "pas-backend.h"
#include "pas-backend-card-sexp.h"
#include "pas-marshal.h"

static BonoboObjectClass *pas_book_parent_class;
POA_GNOME_Evolution_Addressbook_Book__vepv pas_book_vepv;

enum {
	REQUEST,
	LAST_SIGNAL
};

static guint pas_book_signals [LAST_SIGNAL];

struct _PASBookPrivate {
	PASBackend             *backend;
	GNOME_Evolution_Addressbook_BookListener listener;
};

static void
impl_GNOME_Evolution_Addressbook_Book_getVCard (PortableServer_Servant servant,
						const CORBA_char *id,
						CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASRequest req;

	printf ("impl_GNOME_Evolution_Addressbook_Book_getVCard\n");

	req.op              = GetVCard;
	req.get_vcard.id    = id;

	g_signal_emit (book, pas_book_signals [REQUEST], 0, &req);
}

static void
impl_GNOME_Evolution_Addressbook_Book_getCardList (PortableServer_Servant servant,
						   const CORBA_char *query,
						   CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASRequest *req = g_new (PASRequest, 1);

	printf ("impl_GNOME_Evolution_Addressbook_Book_getCardList\n");

	req->op                  = GetCardList;
	req->get_card_list.query = g_strdup (query);

	g_signal_emit (book, pas_book_signals [REQUEST], 0, req);
}

static void
impl_GNOME_Evolution_Addressbook_Book_authenticateUser (PortableServer_Servant servant,
							const char* user,
							const char* passwd,
							const char* auth_method,
							CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASRequest req;

	req.op                    = AuthenticateUser;
	req.auth_user.user        = user;
	req.auth_user.passwd      = passwd;
	req.auth_user.auth_method = auth_method;

	g_signal_emit (book, pas_book_signals [REQUEST], 0, &req);
}

static void
impl_GNOME_Evolution_Addressbook_Book_addCard (PortableServer_Servant servant,
					       const CORBA_char *vcard,
					       CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASRequest req;

	req.op           = CreateCard;
	req.create.vcard = vcard;

	g_signal_emit (book, pas_book_signals [REQUEST], 0, &req);
}

static void
impl_GNOME_Evolution_Addressbook_Book_removeCards (PortableServer_Servant servant,
						   const GNOME_Evolution_Addressbook_CardIdList *ids,
						   CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASRequest req;
	int i;
	
	req.op           = RemoveCards;
	req.remove.ids   = NULL;

	for (i = 0; i < ids->_length; i ++) {
		req.remove.ids = g_list_append (req.remove.ids, ids->_buffer[i]);
	}

	g_signal_emit (book, pas_book_signals [REQUEST], 0, &req);

	g_list_free (req.remove.ids);
}

static void
impl_GNOME_Evolution_Addressbook_Book_modifyCard (PortableServer_Servant servant,
						  const CORBA_char *vcard,
						  CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASRequest req;

	req.op           = ModifyCard;
	req.modify.vcard = vcard;

	g_signal_emit (book, pas_book_signals [REQUEST], 0, &req);
}

static void
impl_GNOME_Evolution_Addressbook_Book_getBookView (PortableServer_Servant servant,
				   const GNOME_Evolution_Addressbook_BookViewListener listener,
				   const CORBA_char *search,
				   const GNOME_Evolution_Addressbook_stringlist* requested_fields,
				   const CORBA_long max_results,
				   CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASBackendCardSExp *card_sexp;
	PASBookView *view;
	EList *views;

	g_warning ("impl_GNOME_Evolution_Addressbook_Book_getBookView (%s)\n", search);

	/* we handle this entirely here, since it doesn't require any
	   backend involvement now that we have pas_book_view_start to
	   actually kick off the search. */

	card_sexp = pas_backend_card_sexp_new (search);
	if (!card_sexp) {
		/* XXX this needs to be an invalid query error of some sort*/
		pas_book_respond_get_book_view (book, GNOME_Evolution_Addressbook_CardNotFound, NULL);
		return;
	}

	view = pas_book_view_new (pas_book_get_backend (book), listener, search, card_sexp);

	if (!view) {
		g_object_unref (card_sexp);
		/* XXX this needs to be an invalid query error of some sort*/
		pas_book_respond_get_book_view (book, GNOME_Evolution_Addressbook_CardNotFound, NULL);
		return;
	}

	views = pas_backend_get_book_views (book->priv->backend);
	e_list_append (views, view);
	g_object_unref (view); /* since the e_list_append will end up reffing it */
	g_object_unref (views);
	pas_book_respond_get_book_view (book, GNOME_Evolution_Addressbook_Success, view);
}


static void
impl_GNOME_Evolution_Addressbook_Book_getChanges (PortableServer_Servant servant,
				 const CORBA_char *change_id,
				 CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASRequest req;

	req.op                    = GetChanges;
	req.get_changes.change_id = change_id;

	g_signal_emit (book, pas_book_signals [REQUEST], 0, &req);
}

static char *
impl_GNOME_Evolution_Addressbook_Book_getStaticCapabilities (PortableServer_Servant servant,
					     CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	char *temp;
	char *ret_val;

	temp = pas_backend_get_static_capabilities (book->priv->backend);
	ret_val = CORBA_string_dup(temp);
	g_free(temp);
	return ret_val;
}

static void
impl_GNOME_Evolution_Addressbook_Book_getSupportedFields (PortableServer_Servant servant,
							  CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASRequest req;

	req.op                         = GetSupportedFields;

	g_signal_emit (book, pas_book_signals [REQUEST], 0, &req);
}

static void
impl_GNOME_Evolution_Addressbook_Book_getSupportedAuthMethods (PortableServer_Servant servant,
							       CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASRequest req;

	req.op                               = GetSupportedAuthMethods;

	g_signal_emit (book, pas_book_signals [REQUEST], 0, &req);
}

static GNOME_Evolution_Addressbook_CallStatus
impl_GNOME_Evolution_Addressbook_Book_cancelOperation (PortableServer_Servant servant,
						       CORBA_Environment *ev)
{
	PASBook *book = PAS_BOOK (bonobo_object (servant));
	PASRequest req;

	req.op                               = CancelOperation;

	g_signal_emit (book, pas_book_signals [REQUEST], 0, &req);
}

/**
 * pas_book_get_backend:
 */
PASBackend *
pas_book_get_backend (PASBook *book)
{
	g_return_val_if_fail (book != NULL,       NULL);
	g_return_val_if_fail (PAS_IS_BOOK (book), NULL);

	return book->priv->backend;
}

/**
 * pas_book_respond_open:
 */
void
pas_book_respond_open (PASBook                           *book,
		       GNOME_Evolution_Addressbook_CallStatus  status)
{
	if (status == GNOME_Evolution_Addressbook_Success)
		_pas_book_factory_send_open_book_response (book->priv->listener,
							   status,
							   bonobo_object_corba_objref (BONOBO_OBJECT (book)));
	else
		_pas_book_factory_send_open_book_response (book->priv->listener,
							   status,
							   CORBA_OBJECT_NIL);
}

/**
 * pas_book_respond_create:
 */
void
pas_book_respond_create (PASBook                                *book,
			 GNOME_Evolution_Addressbook_CallStatus  status,
			 const char                             *id,
			 const char                             *vcard)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	if (status == GNOME_Evolution_Addressbook_Success) {
		/* the card was created, let's let the views know about it */
		EList *views = pas_backend_get_book_views (book->priv->backend);
		EIterator *iter;

		iter = e_list_get_iterator (views);
		while (e_iterator_is_valid (iter)) {
			CORBA_Environment ev;
			PASBookView *view = (PASBookView*)e_iterator_get (iter);

			CORBA_exception_init(&ev);

			bonobo_object_dup_ref(bonobo_object_corba_objref(BONOBO_OBJECT(view)), &ev);

			if (pas_book_view_vcard_matches (view, vcard))
				pas_book_view_notify_add_1 (view,
							    vcard);

			pas_book_view_notify_complete (view, GNOME_Evolution_Addressbook_Success);

			bonobo_object_release_unref(bonobo_object_corba_objref(BONOBO_OBJECT(view)), &ev);

			e_iterator_next (iter);
		}
		g_object_unref (iter);
		g_object_unref (views);
	}

	GNOME_Evolution_Addressbook_BookListener_notifyCardCreated (
		book->priv->listener, status, (char *)id, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_respond_create: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * pas_book_respond_remove:
 */
void
pas_book_respond_remove (PASBook                                *book,
			 GNOME_Evolution_Addressbook_CallStatus  status,
			 GList                                  *ids)
{
	CORBA_Environment ev;
	GList *i;

	CORBA_exception_init (&ev);

	for (i = ids; i; i = i->next) {
		EList *views = pas_backend_get_book_views (book->priv->backend);
		EIterator *iter = e_list_get_iterator (views);
		char *id = i->data;

		while (e_iterator_is_valid (iter)) {
			CORBA_Environment ev;
			PASBookView *view = (PASBookView*)e_iterator_get (iter);
					
			CORBA_exception_init(&ev);

			bonobo_object_dup_ref(bonobo_object_corba_objref(BONOBO_OBJECT(view)), &ev);

			pas_book_view_notify_remove_1 (view, id);

			bonobo_object_release_unref(bonobo_object_corba_objref(BONOBO_OBJECT(view)), &ev);

			e_iterator_next (iter);
		}
		g_object_unref (iter);
		g_object_unref (views);
	}

	GNOME_Evolution_Addressbook_BookListener_notifyCardsRemoved (
		book->priv->listener, status, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_respond_remove: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * pas_book_respond_modify:
 */
void
pas_book_respond_modify (PASBook                                *book,
			 GNOME_Evolution_Addressbook_CallStatus  status,
			 const char                             *old_vcard,
			 const char                             *new_vcard)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	if (status == GNOME_Evolution_Addressbook_Success) {
		/* the card was modified, let's let the views know about it */
		EList *views = pas_backend_get_book_views (book->priv->backend);
		EIterator *iter = e_list_get_iterator (views);
		while (e_iterator_is_valid (iter)) {
			CORBA_Environment ev;
			gboolean old_match, new_match;
			PASBookView *view = (PASBookView*)e_iterator_get (iter);
					
			CORBA_exception_init(&ev);

			bonobo_object_dup_ref(bonobo_object_corba_objref(BONOBO_OBJECT(view)), &ev);

			old_match = pas_book_view_vcard_matches (view,
								 old_vcard);
			new_match = pas_book_view_vcard_matches (view,
								 new_vcard);
			if (old_match && new_match)
				pas_book_view_notify_change_1 (view, new_vcard);
			else if (new_match)
				pas_book_view_notify_add_1 (view, new_vcard);
			else /* if (old_match) */ {
				ECard *card = e_card_new ((char*)old_vcard);
				pas_book_view_notify_remove_1 (view, e_card_get_id (card));
				g_object_unref (card);
			}
			pas_book_view_notify_complete (view, GNOME_Evolution_Addressbook_Success);

			bonobo_object_release_unref(bonobo_object_corba_objref(BONOBO_OBJECT(view)), &ev);

			e_iterator_next (iter);
		}
		g_object_unref (iter);
		g_object_unref (views);
	}

	GNOME_Evolution_Addressbook_BookListener_notifyCardModified (
		book->priv->listener, status, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_respond_modify: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * pas_book_respond_authenticate_user:
 */
void
pas_book_respond_authenticate_user (PASBook                           *book,
				    GNOME_Evolution_Addressbook_CallStatus  status)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookListener_notifyAuthenticationResult (
		book->priv->listener, status, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_respond_authenticate_user: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

void
pas_book_respond_get_supported_fields (PASBook *book,
				       GNOME_Evolution_Addressbook_CallStatus  status,
				       GList   *fields)
{
#if notyet
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_stringlist stringlist;
	int num_fields;
	EIterator *iter;
	int i;

	CORBA_exception_init (&ev);

	num_fields = e_list_length (fields);

	stringlist._buffer = CORBA_sequence_CORBA_string_allocbuf (num_fields);
	stringlist._maximum = num_fields;
	stringlist._length = num_fields;

	iter = e_list_get_iterator (fields);

	for (i = 0; e_iterator_is_valid (iter); e_iterator_next (iter), i ++) {
		stringlist._buffer[i] = CORBA_string_dup (e_iterator_get(iter));
	}

	g_object_unref (iter);
	g_object_unref (fields);

	GNOME_Evolution_Addressbook_BookListener_notifySupportedFields (
			book->priv->listener, 0 /* XXX */, status,
			&stringlist,
			&ev);

	CORBA_exception_free (&ev);

	CORBA_free(stringlist._buffer);
#endif
}

void
pas_book_respond_get_supported_auth_methods (PASBook *book,
					     GNOME_Evolution_Addressbook_CallStatus  status,
					     GList   *auth_methods)
{
#if notyet
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_stringlist stringlist;
	int num_auth_methods;
	EIterator *iter;
	int i;

	CORBA_exception_init (&ev);

	num_auth_methods = e_list_length (auth_methods);

	stringlist._buffer = CORBA_sequence_CORBA_string_allocbuf (num_auth_methods);
	stringlist._maximum = num_auth_methods;
	stringlist._length = num_auth_methods;

	iter = e_list_get_iterator (auth_methods);

	for (i = 0; e_iterator_is_valid (iter); e_iterator_next (iter), i ++) {
		stringlist._buffer[i] = CORBA_string_dup (e_iterator_get(iter));
	}

	g_object_unref (auth_methods);

	GNOME_Evolution_Addressbook_BookListener_notifySupportedAuthMethods (
			book->priv->listener, 0 /* XXX */, status,
			&stringlist,
			&ev);

	CORBA_exception_free (&ev);

	CORBA_free(stringlist._buffer);
#endif
}

static void
view_destroy(gpointer data, GObject *where_object_was)
{
	PASBook           *book = (PASBook *)data;
	EIterator         *iterator;
	gboolean success = FALSE;
	EList *views = pas_backend_get_book_views (book->priv->backend);

	for (iterator = e_list_get_iterator(views);
	     e_iterator_is_valid(iterator);
	     e_iterator_next(iterator)) {
		const PASBookView *view = e_iterator_get(iterator);
		if (view == (PASBookView*)where_object_was) {
			e_iterator_delete(iterator);
			success = TRUE;
			break;
		}
	}
	if (!success)
		g_warning ("Failed to remove from book_views list");
	g_object_unref(iterator);
	g_object_unref(views);
}

/**
 * pas_book_respond_get_book_view:
 */
void
pas_book_respond_get_book_view (PASBook                           *book,
				GNOME_Evolution_Addressbook_CallStatus  status,
				PASBookView                       *book_view)
{
	CORBA_Environment ev;
	CORBA_Object      object = CORBA_OBJECT_NIL;

	printf ("pas_book_respond_get_book_view\n");

	CORBA_exception_init (&ev);

	if (book_view) {
		object = bonobo_object_corba_objref(BONOBO_OBJECT(book_view));

		g_object_weak_ref (G_OBJECT (book_view), view_destroy, book);
	}

	GNOME_Evolution_Addressbook_BookListener_notifyViewRequested (
		book->priv->listener, status, object, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_respond_get_book_view: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

/**
 * pas_book_respond_get_vcard:
 */
void
pas_book_respond_get_vcard (PASBook                           *book,
			    GNOME_Evolution_Addressbook_CallStatus  status,
			    char                              *vcard)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookListener_notifyCardRequested (book->priv->listener,
								      status,
								      vcard,
								      &ev);

	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("could not notify listener of get-vcard response");

	CORBA_exception_free (&ev);
}

/**
 * pas_book_respond_get_card_list:
 */
void
pas_book_respond_get_card_list (PASBook                           *book,
				GNOME_Evolution_Addressbook_CallStatus  status,
				GList                             *card_list)
{
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_stringlist stringlist;
	int num_cards;
	int i;
	GList *l;

	CORBA_exception_init (&ev);

	num_cards = g_list_length (card_list);

	stringlist._buffer = CORBA_sequence_CORBA_string_allocbuf (num_cards);
	stringlist._maximum = num_cards;
	stringlist._length = num_cards;

	for (i = 0, l = card_list; l; l = l->next, i ++)
		stringlist._buffer[i] = CORBA_string_dup (l->data);

	g_list_foreach (card_list, (GFunc)g_free, NULL);
	g_list_free (card_list);


	GNOME_Evolution_Addressbook_BookListener_notifyCardListRequested (book->priv->listener,
									  status,
									  &stringlist,
									  &ev);

	if (ev._major != CORBA_NO_EXCEPTION)
		g_message ("could not notify listener of get-card-list response");

	CORBA_exception_free (&ev);

	CORBA_free(stringlist._buffer);
}

/**
 * pas_book_respond_get_changes:
 */
void
pas_book_respond_get_changes (PASBook                                *book,
			      GNOME_Evolution_Addressbook_CallStatus  status,
			      GList                                  *changes)
{
#if notyet
	CORBA_Environment ev;
	CORBA_Object      object;

	CORBA_exception_init (&ev);
	
	object = bonobo_object_corba_objref(BONOBO_OBJECT(book_view));

	GNOME_Evolution_Addressbook_BookListener_notifyChangesRequested (
		book->priv->listener, 0 /* XXX */, status, object, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_respond_get_changes: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
#endif
}

/**
 * pas_book_report_writable:
 */
void
pas_book_report_writable (PASBook                           *book,
			  gboolean                           writable)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_BookListener_notifyWritable (
		book->priv->listener, (CORBA_boolean) writable, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning ("pas_book_report_writable: Exception "
			   "responding to BookListener!\n");
	}

	CORBA_exception_free (&ev);
}

static void
pas_book_construct (PASBook                *book,
		    PASBackend             *backend,
		    GNOME_Evolution_Addressbook_BookListener listener)
{
	PASBookPrivate *priv;
	CORBA_Environment ev;

	g_return_if_fail (book != NULL);

	priv = book->priv;

	priv->backend   = backend;

	CORBA_exception_init (&ev);
	book->priv->listener = CORBA_Object_duplicate (listener, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_message ("pas_book_construct(): could not duplicate the listener");
		CORBA_exception_free (&ev);
		return;
	}

	CORBA_exception_free (&ev);

	priv->listener = listener;
}

/**
 * pas_book_new:
 */
PASBook *
pas_book_new (PASBackend             *backend,
	      GNOME_Evolution_Addressbook_BookListener listener)
{
	PASBook *book;
	char *caps = pas_backend_get_static_capabilities (backend);

	book = g_object_new (PAS_TYPE_BOOK,
			     "poa", bonobo_poa_get_threaded (ORBIT_THREAD_HINT_PER_REQUEST, NULL),
			     NULL);

	pas_book_construct (book, backend, listener);

	g_free (caps);

	return book;
}

static void
pas_book_dispose (GObject *object)
{
	PASBook *book = PAS_BOOK (object);

	if (book->priv) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		CORBA_Object_release (book->priv->listener, &ev);

		if (ev._major != CORBA_NO_EXCEPTION)
			g_message ("pas_book_construct(): could not release the listener");

		CORBA_exception_free (&ev);

		g_free (book->priv);
		book->priv = NULL;
	}

	if (G_OBJECT_CLASS (pas_book_parent_class)->dispose)
		G_OBJECT_CLASS (pas_book_parent_class)->dispose (object);
}

static void
pas_book_class_init (PASBookClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	POA_GNOME_Evolution_Addressbook_Book__epv *epv;

	pas_book_parent_class = g_type_class_peek_parent (klass);

	pas_book_signals [REQUEST] =
		g_signal_new ("request",
			      G_OBJECT_CLASS_TYPE(object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PASBookClass, request),
			      NULL, NULL,
			      pas_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	object_class->dispose = pas_book_dispose;

	epv = &klass->epv;

	epv->getVCard                = impl_GNOME_Evolution_Addressbook_Book_getVCard;
	epv->getCardList             = impl_GNOME_Evolution_Addressbook_Book_getCardList;
	epv->authenticateUser        = impl_GNOME_Evolution_Addressbook_Book_authenticateUser;
	epv->addCard                 = impl_GNOME_Evolution_Addressbook_Book_addCard;
	epv->removeCards             = impl_GNOME_Evolution_Addressbook_Book_removeCards;
	epv->modifyCard              = impl_GNOME_Evolution_Addressbook_Book_modifyCard;
	epv->getStaticCapabilities   = impl_GNOME_Evolution_Addressbook_Book_getStaticCapabilities;
	epv->getSupportedFields      = impl_GNOME_Evolution_Addressbook_Book_getSupportedFields;
	epv->getSupportedAuthMethods = impl_GNOME_Evolution_Addressbook_Book_getSupportedAuthMethods;
	epv->getBookView             = impl_GNOME_Evolution_Addressbook_Book_getBookView;
	epv->getChanges              = impl_GNOME_Evolution_Addressbook_Book_getChanges;
	epv->cancelOperation         = impl_GNOME_Evolution_Addressbook_Book_cancelOperation;
}

static void
pas_book_init (PASBook *book)
{
	book->priv                = g_new0 (PASBookPrivate, 1);
}

BONOBO_TYPE_FUNC_FULL (
		       PASBook,
		       GNOME_Evolution_Addressbook_Book,
		       BONOBO_TYPE_OBJECT,
		       pas_book);
